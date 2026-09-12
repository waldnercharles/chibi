#include "graph/compiler/behavior_function_jit_internal.h"
#include "graph/runtime/flow.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <math.h>
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
static int *allocate_registers(const behavior_function *f, bool integers, int *saved_count)
{
    int count = acount(f->slots);
    int *registers = xcalloc((size_t)(count + 1), sizeof *registers);
    int *last = xcalloc((size_t)(count + 1), sizeof *last);
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op == BF_BRANCH) {
            last[i->left] = pc;
        }
        if (i->op >= BF_ADD && i->op <= BF_SAVE) {
            last[i->left] = pc;
            if (i->op < BF_STORE) {
                last[i->right] = pc;
            }
        }
    }
    int until[4] = { -1, -1, -1, -1 }, saved_until[4] = { -1, -1, -1, -1 };
    int *next_call = xmalloc((size_t)(acount(f->instructions) + 1) * sizeof *next_call);
    next_call[acount(f->instructions)] = acount(f->instructions);
    for (int pc = acount(f->instructions) - 1; pc >= 0; pc--) {
        next_call[pc] = behavior_function_math(f->instructions[pc].op)
                          ? pc
                          : next_call[pc + 1];
    }
    *saved_count = 0;
    int integer_until[2] = { -1, -1 };
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op < BF_STORE && integers &&
            (behavior_scalar_integer(i->type) || i->type == PIN_BOOL)) {
            if (next_call[pc + 1] >= last[i->output]) {
                for (int r = 0; r < 2; r++) {
                    if (integer_until[r] <= pc) {
                        registers[i->output] = r ? SLJIT_R4 : SLJIT_R2;
                        integer_until[r] = last[i->output];
                        break;
                    }
                }
            }
            continue;
        }
        if (i->op >= BF_STORE || i->type == PIN_BOOL) {
            continue;
        }
        if (next_call[pc + 1] < last[i->output]) {
            /* Values crossing a C ABI call must survive scratch clobbers.
             * SysV x86 has no saved FP registers, so those values spill. */
            for (int r = 0; r < 4 && r < SLJIT_NUMBER_OF_SAVED_FLOAT_REGISTERS; r++) {
                if (saved_until[r] <= pc) {
                    registers[i->output] = SLJIT_FS(r);
                    saved_until[r] = last[i->output];
                    if (r + 1 > *saved_count) {
                        *saved_count = r + 1;
                    }
                    break;
                }
            }
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
    free(next_call);
    free(last);
    return registers;
}
static void store_steps(const behavior_machine *m)
{
    sljit_emit_op1(
        m->compiler,
        SLJIT_MOV,
        SLJIT_MEM1(m->context),
        offsetof(behavior_function_context, steps),
        m->steps,
        0
    );
}
static void bind_here(struct sljit_compiler *c, struct sljit_jump *jump)
{
    struct sljit_label *label = sljit_emit_label(c);
    if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
        sljit_set_label(jump, label);
    }
}
static void enter_locals(struct sljit_compiler *c, const behavior_function *f, int frame, int offset)
{
    for (int i = 0; i < acount(f->locals); i++) {
        if (f->locals[i].frame != frame) {
            continue;
        }
        sljit_emit_op1(
            c,
            SLJIT_MOV,
            SLJIT_MEM1(SLJIT_SP),
            offset + i * sizeof(double),
            SLJIT_MEM0(),
            (sljit_sw)&f->locals[i].initial.d
        );
    }
}
void behavior_jit_failure(
    const behavior_machine *m,
    const behavior_function_instruction *i,
    behavior_function_failure reason
)
{
    struct sljit_compiler *c = m->compiler;
    sljit_emit_op1(
        c,
        SLJIT_MOV,
        SLJIT_MEM1(m->context),
        offsetof(behavior_function_context, error_steps),
        m->steps,
        0
    );
    if (i->frame) {
        sljit_emit_op1(c, SLJIT_MOV, m->steps, 0, SLJIT_MEM1(SLJIT_SP), m->parents);
    }
    store_steps(m);
    sljit_emit_op1(
        c,
        SLJIT_MOV,
        SLJIT_MEM1(m->context),
        offsetof(behavior_function_context, node_id),
        SLJIT_IMM,
        i->node_id
    );
    sljit_emit_op1(
        c,
        SLJIT_MOV32,
        SLJIT_MEM1(m->context),
        offsetof(behavior_function_context, error_frame),
        SLJIT_IMM,
        i->frame
    );
    sljit_emit_op1(
        c,
        SLJIT_MOV32,
        SLJIT_MEM1(m->context),
        offsetof(behavior_function_context, failure),
        SLJIT_IMM,
        reason
    );
    sljit_emit_op1(
        c,
        SLJIT_MOV32,
        SLJIT_MEM1(m->context),
        offsetof(behavior_function_context, error_source),
        SLJIT_IMM,
        i->source
    );
    sljit_emit_op1(
        c,
        SLJIT_MOV32,
        SLJIT_MEM1(m->context),
        offsetof(behavior_function_context, error_pin),
        SLJIT_IMM,
        reason == BEHAVIOR_FUNCTION_DATA ? 0 : -1
    );
    sljit_emit_return(
        c,
        SLJIT_MOV32,
        SLJIT_IMM,
        reason == BEHAVIOR_FUNCTION_NO_RETURN || reason == BEHAVIOR_FUNCTION_DATA
            ? FLOW_ERR_INVALID_GRAPH
            : FLOW_ERR_STEP_BUDGET
    );
}
/* Match entry argument defaults and real-number coercion without a C call. */
static void argument(
    const behavior_machine *m,
    const behavior_function_instruction *i,
    int destination,
    sljit_sw offset
)
{
    struct sljit_compiler *c = m->compiler;
    struct sljit_jump *missing = sljit_emit_cmp(
        c,
        SLJIT_SIG_LESS_EQUAL | SLJIT_32,
        m->argument_count,
        0,
        SLJIT_IMM,
        i->left
    );
    sljit_sw base = i->left * sizeof(pin_value);
    struct sljit_jump *same = sljit_emit_cmp(
        c,
        SLJIT_EQUAL | SLJIT_32,
        SLJIT_MEM1(m->arguments),
        base + offsetof(pin_value, type),
        SLJIT_IMM,
        i->type
    );
    struct sljit_jump *wrong = sljit_emit_cmp(
        c,
        SLJIT_NOT_EQUAL | SLJIT_32,
        SLJIT_MEM1(m->arguments),
        base + offsetof(pin_value, type),
        SLJIT_IMM,
        i->type == PIN_F32 ? PIN_F64 : PIN_F32
    );
    sljit_emit_fop1(
        c,
        i->type == PIN_F32 ? SLJIT_CONV_F32_FROM_F64 : SLJIT_CONV_F64_FROM_F32,
        SLJIT_FR0,
        0,
        SLJIT_MEM1(m->arguments),
        base + offsetof(pin_value, d)
    );
    struct sljit_jump *converted = sljit_emit_jump(c, SLJIT_JUMP);
    bind_here(c, same);
    sljit_emit_fop1(
        c,
        SLJIT_MOV_F64 | width(i->type),
        SLJIT_FR0,
        0,
        SLJIT_MEM1(m->arguments),
        base + offsetof(pin_value, d)
    );
    struct sljit_jump *loaded = sljit_emit_jump(c, SLJIT_JUMP);
    bind_here(c, missing);
    bind_here(c, wrong);
    sljit_emit_fop1(
        c,
        SLJIT_MOV_F64 | width(i->type),
        SLJIT_FR0,
        0,
        SLJIT_MEM0(),
        (sljit_sw)&i->value.d
    );
    bind_here(c, loaded);
    bind_here(c, converted);
    sljit_emit_fop1(c, SLJIT_MOV_F64 | width(i->type), destination, offset, SLJIT_FR0, 0);
}
void *behavior_function_jit(const behavior_function *f)
{
    struct sljit_compiler *c = sljit_create_compiler(NULL);
    if (!c) {
        return NULL;
    }
    bool integers = false;
    for (int i = 0; i < acount(f->slots); i++) {
        integers |= behavior_scalar_integer(f->slots[i]);
    }
    int saved_float = 0;
    int *registers = allocate_registers(f, integers, &saved_float);
    int stack_slots = 0;
    for (int i = 0; i < acount(f->slots); i++) {
        if (!registers[i]) {
            stack_slots = i + 1;
        }
    }
    int locals = stack_slots * sizeof(double);
    int parents = locals + acount(f->locals) * sizeof(double);
    int stack_size = parents + (acount(f->frames) > 1
                                    ? acount(f->frames) * sizeof(sljit_sw)
                                    : 0);
    bool arguments = false, calls = false;
    for (int i = 0; i < acount(f->instructions); i++) {
        arguments |= f->instructions[i].op == BF_ARGUMENT;
        calls |= behavior_function_math(f->instructions[i].op);
    }
    /* Integer divide uses R0/R1, not the C ABI. Keep context in scratch
     * registers when the target has room; save it only across actual calls
     * or on targets with fewer scratch registers. */
    int integer_scratch = arguments ? 10 : 8;
    bool saved = calls ||
                 (integers && SLJIT_NUMBER_OF_SCRATCH_REGISTERS < integer_scratch);
    behavior_machine m = { .compiler = c,
                           .function = f,
                           .registers = registers,
                           .locals = locals,
                           .parents = parents,
                           .context = saved    ? SLJIT_S0
                                    : integers ? SLJIT_R(5)
                                               : SLJIT_R2,
                           .steps = saved    ? SLJIT_S1
                                  : integers ? SLJIT_R(6)
                                             : SLJIT_R1,
                           .variables = saved    ? SLJIT_S2
                                      : integers ? SLJIT_R(7)
                                                 : SLJIT_R0,
                           .arguments = saved    ? SLJIT_S3
                                      : integers ? SLJIT_R(8)
                                                 : SLJIT_R4,
                           .argument_count = saved    ? SLJIT_S4
                                           : integers ? SLJIT_R(9)
                                                      : SLJIT_R5 };
    sljit_emit_enter(
        c,
        0,
        SLJIT_ARGS1(32, P_R),
        (integers              ? (saved ? 5 : integer_scratch)
         : arguments && !calls ? 6
                               : 4) |
            SLJIT_ENTER_FLOAT(6),
        (saved ? (arguments ? 5 : 3) : 0) | SLJIT_ENTER_FLOAT(saved_float),
        stack_size
    );
    sljit_emit_op1(c, SLJIT_MOV_P, m.context, 0, SLJIT_R0, 0);
    sljit_emit_op1(c, SLJIT_MOV_P, m.variables, 0, SLJIT_MEM1(m.context), offsetof(behavior_function_context, variables));
    sljit_emit_op1(c, SLJIT_MOV, m.steps, 0, SLJIT_MEM1(m.context), offsetof(behavior_function_context, steps));
    if (arguments) {
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            m.arguments,
            0,
            SLJIT_MEM1(m.context),
            offsetof(behavior_function_context, arguments)
        );
        sljit_emit_op1(
            c,
            SLJIT_MOV32,
            m.argument_count,
            0,
            SLJIT_MEM1(m.context),
            offsetof(behavior_function_context, argument_count)
        );
    }
    enter_locals(c, f, 0, locals);
    struct sljit_label **labels = xcalloc((size_t)acount(f->instructions), sizeof *labels);
    struct sljit_jump **jumps = xcalloc((size_t)acount(f->instructions), sizeof *jumps);
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        labels[pc] = sljit_emit_label(c);
        int flags = width(i->type), mem = SLJIT_MEM1(SLJIT_SP);
        int destination = registers[i->output] ? registers[i->output] : mem;
        sljit_sw offset = registers[i->output] ? 0 : i->output * sizeof(double);
        if (behavior_jit_integer(&m, i, destination, offset)) {
            continue;
        }
        switch (i->op) {
#define MATH_UNARY(op, name, fn)                                                                \
    case BF_##op:                                                                               \
        load(c, f, registers, i->left, SLJIT_FR0);                                              \
        sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS1(F64, F64), SLJIT_IMM, SLJIT_FUNC_ADDR(fn)); \
        sljit_emit_fop1(c, SLJIT_MOV_F64, destination, offset, SLJIT_FR0, 0);                   \
        break;
            BEHAVIOR_MATH_UNARY(MATH_UNARY)
#undef MATH_UNARY
#define MATH_BINARY(op, name, fn)                                                                    \
    case BF_##op:                                                                                    \
        load(c, f, registers, i->left, SLJIT_FR0);                                                   \
        load(c, f, registers, i->right, SLJIT_FR1);                                                  \
        sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS2(F64, F64, F64), SLJIT_IMM, SLJIT_FUNC_ADDR(fn)); \
        sljit_emit_fop1(c, SLJIT_MOV_F64, destination, offset, SLJIT_FR0, 0);                        \
        break;
            BEHAVIOR_MATH_BINARY(MATH_BINARY)
#undef MATH_BINARY
            case BF_CHECK: {
                struct sljit_jump *ok = sljit_emit_cmp(
                    c,
                    SLJIT_LESS,
                    m.steps,
                    0,
                    SLJIT_MEM1(m.context),
                    offsetof(behavior_function_context, budget)
                );
                behavior_jit_failure(&m, i, BEHAVIOR_FUNCTION_BUDGET);
                struct sljit_label *label = sljit_emit_label(c);
                if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
                    sljit_set_label(ok, label);
                }
                sljit_emit_op2(c, SLJIT_ADD, m.steps, 0, m.steps, 0, SLJIT_IMM, 1);
                break;
            }
            case BF_CALL_CHECK: {
                int limit = i->left - f->frames[i->frame].depth;
                struct sljit_jump *ok = limit > 0
                                          ? sljit_emit_cmp(
                                                c,
                                                SLJIT_LESS | SLJIT_32,
                                                SLJIT_MEM1(m.context),
                                                offsetof(behavior_function_context, call_depth),
                                                SLJIT_IMM,
                                                limit
                                            )
                                          : NULL;
                behavior_jit_failure(&m, i, BEHAVIOR_FUNCTION_DEPTH);
                if (ok) {
                    bind_here(c, ok);
                }
                break;
            }
            case BF_FAIL: behavior_jit_failure(&m, i, i->left); break;
            case BF_ENTER:
                sljit_emit_op1(c, SLJIT_MOV, mem, parents + i->frame * sizeof(sljit_sw), m.steps, 0);
                sljit_emit_op1(c, SLJIT_MOV, m.steps, 0, SLJIT_IMM, 0);
                enter_locals(c, f, i->target, locals);
                break;
            case BF_LEAVE:
                sljit_emit_op1(
                    c,
                    SLJIT_MOV,
                    m.steps,
                    0,
                    mem,
                    parents + f->frames[i->target].source.parent * sizeof(sljit_sw)
                );
                break;
            case BF_JUMP:
                if (i->target != pc + 1) {
                    jumps[pc] = sljit_emit_jump(c, SLJIT_JUMP);
                }
                break;
            case BF_BRANCH:
                sljit_emit_op1(
                    c,
                    SLJIT_MOV_U8,
                    SLJIT_R3,
                    0,
                    registers[i->left] ? registers[i->left] : mem,
                    registers[i->left] ? 0 : i->left * sizeof(double)
                );
                jumps[pc] = sljit_emit_cmp(c, SLJIT_NOT_EQUAL | SLJIT_32, SLJIT_R3, 0, SLJIT_IMM, 0);
                break;
            case BF_RETURN:
                sljit_emit_op1(
                    c,
                    SLJIT_MOV,
                    SLJIT_MEM1(m.context),
                    offsetof(behavior_function_context, returned_node_id),
                    SLJIT_IMM,
                    i->node_id
                );
                /* fall through */
            case BF_END:
                if (f->root_steps) {
                    sljit_emit_op2(c, SLJIT_ADD, m.steps, 0, m.steps, 0, SLJIT_IMM, f->root_steps);
                }
                store_steps(&m);
                sljit_emit_return(c, SLJIT_MOV32, SLJIT_IMM, 0);
                break;
            case BF_CONSTANT:
                sljit_emit_fop1(
                    c,
                    SLJIT_MOV_F64 | flags,
                    SLJIT_FR0,
                    0,
                    SLJIT_MEM0(),
                    (sljit_sw)&i->value.d
                );
                sljit_emit_fop1(c, SLJIT_MOV_F64 | flags, destination, offset, SLJIT_FR0, 0);
                break;
            case BF_LOCAL:
            case BF_VARIABLE: {
                int base = i->op == BF_LOCAL ? SLJIT_SP : m.variables;
                sljit_sw address = i->op == BF_LOCAL
                                     ? locals + i->left * sizeof(double)
                                     : i->left * sizeof(pin_value) +
                                           offsetof(pin_value, d);
                sljit_emit_fop1(c, SLJIT_MOV_F64 | flags, SLJIT_FR0, 0, SLJIT_MEM1(base), address);
                sljit_emit_fop1(c, SLJIT_MOV_F64 | flags, destination, offset, SLJIT_FR0, 0);
                break;
            }
            case BF_SAVE:
                load(c, f, registers, i->left, SLJIT_FR0);
                sljit_emit_fop1(c, SLJIT_MOV_F64 | flags, mem, locals + i->target * sizeof(double), SLJIT_FR0, 0);
                break;
            case BF_RESULT:
            case BF_STORE: {
                int base = m.variables;
                if (i->op == BF_RESULT) {
                    base = SLJIT_R3;
                    sljit_emit_op1(
                        c,
                        SLJIT_MOV_P,
                        base,
                        0,
                        SLJIT_MEM1(m.context),
                        offsetof(behavior_function_context, results)
                    );
                }
                load(c, f, registers, i->left, SLJIT_FR0);
                sljit_emit_fop1(
                    c,
                    SLJIT_MOV_F64 | flags,
                    SLJIT_MEM1(base),
                    i->target * sizeof(pin_value) + offsetof(pin_value, d),
                    SLJIT_FR0,
                    0
                );
                break;
            }
            case BF_ARGUMENT: argument(&m, i, destination, offset); break;
            case BF_CONVERT:
                load(c, f, registers, i->left, SLJIT_FR0);
                sljit_emit_fop1(
                    c,
                    i->type == PIN_F32 ? SLJIT_CONV_F32_FROM_F64 : SLJIT_CONV_F64_FROM_F32,
                    SLJIT_FR0,
                    0,
                    SLJIT_FR0,
                    0
                );
                sljit_emit_fop1(c, SLJIT_MOV_F64 | flags, destination, offset, SLJIT_FR0, 0);
                break;
            case BF_DELTA:
                sljit_emit_fop1(
                    c,
                    SLJIT_CONV_F32_FROM_F64,
                    SLJIT_FR0,
                    0,
                    SLJIT_MEM1(m.context),
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
                    sljit_emit_op1(c, SLJIT_MOV_U8, destination, offset, SLJIT_IMM, 0);
                    struct sljit_jump *done = sljit_emit_jump(c, SLJIT_JUMP);
                    struct sljit_label *label = sljit_emit_label(c);
                    if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
                        sljit_set_label(yes, label);
                    }
                    sljit_emit_op1(c, SLJIT_MOV_U8, destination, offset, SLJIT_IMM, 1);
                    label = sljit_emit_label(c);
                    if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
                        sljit_set_label(done, label);
                    }
                } else if (i->op == BF_MIN || i->op == BF_MAX) {
                    struct sljit_jump *left = sljit_emit_fcmp(
                        c,
                        (i->op == BF_MIN ? SLJIT_ORDERED_LESS : SLJIT_ORDERED_GREATER) | flags,
                        SLJIT_FR0,
                        0,
                        SLJIT_FR1,
                        0
                    );
                    sljit_emit_fop1(c, SLJIT_MOV_F64 | flags, SLJIT_FR0, 0, SLJIT_FR1, 0);
                    bind_here(c, left);
                    sljit_emit_fop1(c, SLJIT_MOV_F64 | flags, destination, offset, SLJIT_FR0, 0);
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
void *behavior_function_guard(const behavior_function *f)
{
    struct sljit_compiler *c = sljit_create_compiler(NULL);
    if (!c) {
        return NULL;
    }
    sljit_emit_enter(c, 0, SLJIT_ARGS1(32, P_R), 4, 0, 0);
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_R0), offsetof(behavior_function_context, budget));
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_R0), offsetof(behavior_function_context, steps));
    struct sljit_jump *slow[4];
    int count = 0;
    slow[count++] = sljit_emit_cmp(c, SLJIT_LESS, SLJIT_R1, 0, SLJIT_R3, 0);
    sljit_emit_op2(c, SLJIT_SUB, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_R3, 0);
    slow[count++] = sljit_emit_cmp(
        c,
        SLJIT_LESS,
        SLJIT_R1,
        0,
        SLJIT_IMM,
        f->fast->root_steps
    );
    if (f->fast->child_budget > f->fast->root_steps) {
        slow[count++] = sljit_emit_cmp(
            c,
            SLJIT_LESS,
            SLJIT_MEM1(SLJIT_R0),
            offsetof(behavior_function_context, budget),
            SLJIT_IMM,
            f->fast->child_budget
        );
    }
    if (f->fast->depth_limit) {
        slow[count++] = sljit_emit_cmp(
            c,
            SLJIT_GREATER_EQUAL | SLJIT_32,
            SLJIT_MEM1(SLJIT_R0),
            offsetof(behavior_function_context, call_depth),
            SLJIT_IMM,
            f->fast->depth_limit
        );
    }
    sljit_emit_icall(
        c,
        SLJIT_CALL | SLJIT_CALL_RETURN,
        SLJIT_ARGS1(32, P),
        SLJIT_IMM,
        (sljit_sw)f->fast->code
    );
    struct sljit_label *checked = sljit_emit_label(c);
    if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
        for (int i = 0; i < count; i++) {
            sljit_set_label(slow[i], checked);
        }
    }
    sljit_emit_icall(
        c,
        SLJIT_CALL | SLJIT_CALL_RETURN,
        SLJIT_ARGS1(32, P),
        SLJIT_IMM,
        (sljit_sw)f->code
    );
    void *code = sljit_get_compiler_error(c) == SLJIT_SUCCESS
                   ? sljit_generate_code(c, 0, NULL)
                   : NULL;
    sljit_free_compiler(c);
    return code;
}
#else
void *behavior_function_jit(const behavior_function *)
{
    return NULL;
}
void *behavior_function_guard(const behavior_function *)
{
    return NULL;
}
#endif
