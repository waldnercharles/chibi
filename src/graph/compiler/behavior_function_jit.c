#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stddef.h>
#include <stdlib.h>
#if CHIBI_HAVE_SJLIT
#include <sljitLir.h>

static int width(pin_type type)
{
    return type == PIN_F32 ? SLJIT_32 : 0;
}
static void load(struct sljit_compiler *c, const behavior_function *f, const int *registers, int slot, int reg)
{
    sljit_emit_fop1(
        c,
        SLJIT_MOV_F64 | width(f->slots[slot]),
        reg,
        0,
        registers[slot] ? registers[slot] : SLJIT_MEM1(SLJIT_SP),
        registers[slot] ? 0 : slot * sizeof(double)
    );
}
/* Slots are defined before their uses within one lowered expression. Allocate
 * four scratch registers by live interval; excess live values spill to the
 * activation stack. FR0/FR1 remain emitter temporaries. */
static int *allocate_registers(const behavior_function *f)
{
    int count = acount(f->slots);
    int *registers = xcalloc((size_t)(count + 1), sizeof *registers);
    int *last = xcalloc((size_t)(count + 1), sizeof *last);
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op >= BF_ADD && i->op <= BF_STORE) {
            last[i->left] = pc;
            if (i->op != BF_STORE) {
                last[i->right] = pc;
            }
        }
    }
    int until[4] = { -1, -1, -1, -1 };
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op >= BF_STORE || i->type == PIN_BOOL) {
            continue;
        }
        for (int r = 0; r < 4; r++) {
            if (until[r] <= pc) {
                registers[i->output] = SLJIT_FR2 + r;
                until[r] = last[i->output];
                break;
            }
        }
    }
    free(last);
    return registers;
}
static void store_steps(struct sljit_compiler *c)
{
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(SLJIT_R2), offsetof(behavior_function_context, steps), SLJIT_R1, 0);
}
void *behavior_function_jit(const behavior_function *f)
{
    struct sljit_compiler *c = sljit_create_compiler(NULL);
    if (!c) {
        return NULL;
    }
    int *registers = allocate_registers(f);
    int stack_slots = 0;
    for (int i = 0; i < acount(f->slots); i++) {
        if (!registers[i]) {
            stack_slots = i + 1;
        }
    }
    sljit_emit_enter(c, 0, SLJIT_ARGS1(32, P_R), 4 | SLJIT_ENTER_FLOAT(6), 0, stack_slots * sizeof(double));
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R2, 0, SLJIT_R0, 0);
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_R2), offsetof(behavior_function_context, variables));
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_R2), offsetof(behavior_function_context, steps));
    struct sljit_label **labels = xcalloc((size_t)acount(f->instructions), sizeof *labels);
    struct sljit_jump **jumps = xcalloc((size_t)acount(f->instructions), sizeof *jumps);
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        labels[pc] = sljit_emit_label(c);
        int flags = width(i->type), mem = SLJIT_MEM1(SLJIT_SP);
        int destination = registers[i->output] ? registers[i->output] : mem;
        sljit_sw offset = registers[i->output] ? 0 : i->output * sizeof(double);
        switch (i->op) {
            case BF_CHECK: {
                struct sljit_jump *ok = sljit_emit_cmp(
                    c,
                    SLJIT_LESS,
                    SLJIT_R1,
                    0,
                    SLJIT_MEM1(SLJIT_R2),
                    offsetof(behavior_function_context, budget)
                );
                store_steps(c);
                sljit_emit_op1(
                    c,
                    SLJIT_MOV,
                    SLJIT_MEM1(SLJIT_R2),
                    offsetof(behavior_function_context, node_id),
                    SLJIT_IMM,
                    i->node_id
                );
                sljit_emit_return(c, SLJIT_MOV32, SLJIT_IMM, FLOW_ERR_STEP_BUDGET);
                struct sljit_label *label = sljit_emit_label(c);
                if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
                    sljit_set_label(ok, label);
                }
                sljit_emit_op2(c, SLJIT_ADD, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, 1);
                break;
            }
            case BF_JUMP:
                if (i->target != pc + 1) {
                    jumps[pc] = sljit_emit_jump(c, SLJIT_JUMP);
                }
                break;
            case BF_BRANCH:
                sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R3, 0, mem, i->left * sizeof(double));
                jumps[pc] = sljit_emit_cmp(c, SLJIT_NOT_EQUAL | SLJIT_32, SLJIT_R3, 0, SLJIT_IMM, 0);
                break;
            case BF_END:
                store_steps(c);
                sljit_emit_return(c, SLJIT_MOV32, SLJIT_IMM, 0);
                break;
            case BF_CONSTANT:
                if (i->type == PIN_BOOL) {
                    sljit_emit_op1(
                        c,
                        SLJIT_MOV_U8,
                        mem,
                        offset,
                        SLJIT_IMM,
                        i->value.b
                    );
                } else {
                    sljit_emit_fop1(
                        c,
                        SLJIT_MOV_F64 | flags,
                        SLJIT_FR0,
                        0,
                        SLJIT_MEM0(),
                        (sljit_sw)&i->value.d
                    );
                    sljit_emit_fop1(c, SLJIT_MOV_F64 | flags, destination, offset, SLJIT_FR0, 0);
                }
                break;
            case BF_VARIABLE:
                if (i->type == PIN_BOOL) {
                    sljit_emit_op1(
                        c,
                        SLJIT_MOV_U8,
                        SLJIT_R3,
                        0,
                        SLJIT_MEM1(SLJIT_R0),
                        i->left * sizeof(pin_value) + offsetof(pin_value, b)
                    );
                    sljit_emit_op1(c, SLJIT_MOV_U8, mem, offset, SLJIT_R3, 0);
                } else {
                    sljit_emit_fop1(
                        c,
                        SLJIT_MOV_F64 | flags,
                        SLJIT_FR0,
                        0,
                        SLJIT_MEM1(SLJIT_R0),
                        i->left * sizeof(pin_value) + offsetof(pin_value, d)
                    );
                    sljit_emit_fop1(c, SLJIT_MOV_F64 | flags, destination, offset, SLJIT_FR0, 0);
                }
                break;
            case BF_STORE:
                if (i->type == PIN_BOOL) {
                    sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R3, 0, mem, i->left * sizeof(double));
                    sljit_emit_op1(
                        c,
                        SLJIT_MOV_U8,
                        SLJIT_MEM1(SLJIT_R0),
                        i->target * sizeof(pin_value) + offsetof(pin_value, b),
                        SLJIT_R3,
                        0
                    );
                } else {
                    load(c, f, registers, i->left, SLJIT_FR0);
                    sljit_emit_fop1(
                        c,
                        SLJIT_MOV_F64 | flags,
                        SLJIT_MEM1(SLJIT_R0),
                        i->target * sizeof(pin_value) + offsetof(pin_value, d),
                        SLJIT_FR0,
                        0
                    );
                }
                break;
            case BF_DELTA:
                sljit_emit_fop1(
                    c,
                    SLJIT_CONV_F32_FROM_F64,
                    SLJIT_FR0,
                    0,
                    SLJIT_MEM1(SLJIT_R2),
                    offsetof(behavior_function_context, delta_seconds)
                );
                sljit_emit_fop1(c, SLJIT_MOV_F32, destination, offset, SLJIT_FR0, 0);
                break;
            default: {
                load(c, f, registers, i->left, SLJIT_FR0);
                load(c, f, registers, i->right, SLJIT_FR1);
                if (i->op >= BF_LT && i->op <= BF_NE) {
                    int condition = (int[]){
                        SLJIT_ORDERED_LESS,    SLJIT_ORDERED_LESS_EQUAL,
                        SLJIT_ORDERED_GREATER, SLJIT_ORDERED_GREATER_EQUAL,
                        SLJIT_ORDERED_EQUAL,   SLJIT_UNORDERED_OR_NOT_EQUAL
                    }[i->op - BF_LT];
                    struct sljit_jump *yes = sljit_emit_fcmp(
                        c,
                        condition | width(f->slots[i->left]),
                        SLJIT_FR0,
                        0,
                        SLJIT_FR1,
                        0
                    );
                    sljit_emit_op1(c, SLJIT_MOV_U8, mem, offset, SLJIT_IMM, 0);
                    struct sljit_jump *done = sljit_emit_jump(c, SLJIT_JUMP);
                    struct sljit_label *label = sljit_emit_label(c);
                    if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
                        sljit_set_label(yes, label);
                    }
                    sljit_emit_op1(c, SLJIT_MOV_U8, mem, offset, SLJIT_IMM, 1);
                    label = sljit_emit_label(c);
                    if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
                        sljit_set_label(done, label);
                    }
                } else {
                    int op = i->op == BF_ADD ? SLJIT_ADD_F64
                           : i->op == BF_SUB ? SLJIT_SUB_F64
                           : i->op == BF_MUL ? SLJIT_MUL_F64
                                             : SLJIT_DIV_F64;
                    if (i->op == BF_NEG || i->op == BF_ABS) {
                        sljit_emit_fop1(
                            c,
                            (i->op == BF_NEG ? SLJIT_NEG_F64 : SLJIT_ABS_F64) | flags,
                            SLJIT_FR0,
                            0,
                            SLJIT_FR0,
                            0
                        );
                    } else {
                        sljit_emit_fop2(c, op | flags, SLJIT_FR0, 0, SLJIT_FR0, 0, SLJIT_FR1, 0);
                    }
                    sljit_emit_fop1(c, SLJIT_MOV_F64 | flags, destination, offset, SLJIT_FR0, 0);
                }
            }
        }
    }
    void *code = NULL;
    if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
        for (int pc = 0; pc < acount(f->instructions); pc++) {
            if (jumps[pc]) {
                sljit_set_label(jumps[pc], labels[f->instructions[pc].target]);
            }
        }
        code = sljit_generate_code(c, 0, NULL);
    }
    free(registers);
    free(labels);
    free(jumps);
    sljit_free_compiler(c);
    return code;
}
#else
void *behavior_function_jit(const behavior_function *)
{
    return NULL;
}
#endif
