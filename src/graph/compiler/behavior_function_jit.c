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
static void store_steps(const behavior_machine *m, size_t offset)
{
    if (m->countdown) {
        sljit_emit_op2(
            m->compiler,
            SLJIT_SUB,
            SLJIT_MEM1(m->context),
            offset,
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, budget),
            m->steps,
            0
        );
        return;
    }
    sljit_emit_op1(m->compiler, SLJIT_MOV, SLJIT_MEM1(m->context), offset, m->steps, 0);
}
static void bind_here(struct sljit_compiler *c, struct sljit_jump *jump)
{
    struct sljit_label *label = sljit_emit_label(c);
    if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
        sljit_set_label(jump, label);
    }
}
static void enter_locals(const behavior_machine *m, int frame)
{
    struct sljit_compiler *c = m->compiler;
    const behavior_function *f = m->function;
    for (int i = 0; i < acount(f->locals); i++) {
        if (f->locals[i].frame != frame) {
            continue;
        }
        pin_type type = f->locals[i].initial.type;
        if (m->local_registers[i] && (type == PIN_F32 || type == PIN_F64)) {
            sljit_emit_fop1(
                c,
                SLJIT_MOV_F64 | width(type),
                m->local_registers[i],
                0,
                SLJIT_MEM0(),
                (sljit_sw)&f->locals[i].initial.d
            );
            continue;
        }
        sljit_emit_op1(
            c,
            SLJIT_MOV,
            m->local_registers[i] ? m->local_registers[i] : SLJIT_MEM1(SLJIT_SP),
            m->local_registers[i] ? 0 : m->locals + i * sizeof(double),
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
    if (i->op != BF_QUERY) {
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, error_message),
            SLJIT_IMM,
            (sljit_sw)i->message
        );
    }
    store_steps(m, offsetof(behavior_function_context, error_steps));
    if (i->frame) {
        sljit_emit_op1(c, SLJIT_MOV, m->steps, 0, SLJIT_MEM1(SLJIT_SP), m->parents);
    }
    store_steps(m, offsetof(behavior_function_context, steps));
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
        offsetof(behavior_function_context, error_stack),
        SLJIT_IMM,
        i->stack
    );
    sljit_emit_op1(
        c,
        SLJIT_MOV32,
        SLJIT_MEM1(m->context),
        offsetof(behavior_function_context, error_pin),
        SLJIT_IMM,
        reason >= BEHAVIOR_FUNCTION_DATA ? i->pin : -1
    );
    sljit_emit_return(
        c,
        SLJIT_MOV32,
        reason == BEHAVIOR_FUNCTION_COMMAND ? SLJIT_MEM1(m->context) : SLJIT_IMM,
        reason == BEHAVIOR_FUNCTION_COMMAND
            ? offsetof(behavior_function_context, error_status)
        : reason == BEHAVIOR_FUNCTION_STATE     ? FLOW_ERR_STATE
        : reason == BEHAVIOR_FUNCTION_HOST_FLOW ? FLOW_ERR_NODE
        : reason >= BEHAVIOR_FUNCTION_NO_RETURN ? FLOW_ERR_INVALID_GRAPH
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
        integers |= behavior_scalar_integer(f->slots[i]) || f->slots[i] == PIN_HANDLE;
    }
    int saved_float = 0;
    int *registers = behavior_jit_registers(f, integers, &saved_float);
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
    bool arguments = false, calls = false, loop = false;
    for (int i = 0; i < acount(f->instructions); i++) {
        arguments |= f->instructions[i].op == BF_ARGUMENT ||
                     ((f->instructions[i].op == BF_PRESENT ||
                       f->instructions[i].op == BF_TYPE_NAME ||
                       f->instructions[i].op == BF_ARRAY) &&
                      f->instructions[i].target == BF_ARGUMENT);
        calls |= behavior_function_call(f->instructions[i].op);
        loop |= (f->instructions[i].op == BF_JUMP || f->instructions[i].op == BF_BRANCH) &&
                f->instructions[i].target <= i;
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
    int scratch = integers            ? (saved ? 5 : integer_scratch)
                : arguments && !calls ? 6
                                      : 4;
    int saved_integer = saved ? (arguments ? 5 : 3) : 0, saved_locals = 0;
    int *local_registers = behavior_jit_local_registers(f, saved_integer, scratch, &saved_locals, &saved_float);
    m.local_registers = local_registers;
    behavior_jit_alias_locals(f, registers, local_registers);
    behavior_jit_coalesce_saves(f, registers, local_registers);
    sljit_emit_enter(
        c,
        0,
        SLJIT_ARGS1(32, P_R),
        scratch | SLJIT_ENTER_FLOAT(6),
        (saved_integer + saved_locals) | SLJIT_ENTER_FLOAT(saved_float),
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
    enter_locals(&m, 0);
    /* A loop keeps remaining budget in a register. Parent frames store the
     * same representation; only returns and failures materialize step counts. */
    if (loop) {
        assert(f->instructions[0].op == BF_CHECK && !f->root_steps);
        struct sljit_jump *valid = sljit_emit_cmp(
            c,
            SLJIT_LESS_EQUAL,
            m.steps,
            0,
            SLJIT_MEM1(m.context),
            offsetof(behavior_function_context, budget)
        );
        behavior_jit_failure(&m, &f->instructions[0], BEHAVIOR_FUNCTION_BUDGET);
        bind_here(c, valid);
        sljit_emit_op2(
            c,
            SLJIT_SUB,
            m.steps,
            0,
            SLJIT_MEM1(m.context),
            offsetof(behavior_function_context, budget),
            m.steps,
            0
        );
        m.countdown = true;
    }
    struct sljit_label **labels = xcalloc((size_t)acount(f->instructions), sizeof *labels);
    struct sljit_jump **failures = xcalloc((size_t)acount(f->instructions), sizeof *failures);
    struct sljit_jump **jumps = xcalloc((size_t)acount(f->instructions), sizeof *jumps);
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        labels[pc] = sljit_emit_label(c);
        int flags = width(i->type), mem = SLJIT_MEM1(SLJIT_SP);
        int destination = registers[i->output] ? registers[i->output] : mem;
        sljit_sw offset = registers[i->output] ? 0 : i->output * sizeof(double);
        int fields = behavior_jit_result_fields(&m, pc);
        if (fields) {
            for (int k = 1; k < fields; k++) {
                labels[pc + k] = labels[pc];
            }
            pc += fields - 1;
            continue;
        }
        struct sljit_jump *branch = NULL;
        if (behavior_jit_fused_branch(&m, pc, &branch)) {
            labels[pc + 1] = labels[pc];
            jumps[++pc] = branch;
            continue;
        }
        if (behavior_jit_component(&m, i, destination, offset) ||
            behavior_jit_memory(&m, i, destination, offset) ||
            behavior_jit_integer(&m, i, destination, offset)) {
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
            MATH_BINARY(MOD_F64, Modulo, fmod)
            MATH_BINARY(HYPOT, Length, hypot)
#undef MATH_BINARY
            case BF_ATAN2_F32:
            case BF_MOD_F32:
                load(c, f, registers, i->left, SLJIT_FR0);
                load(c, f, registers, i->right, SLJIT_FR1);
                sljit_emit_icall(
                    c,
                    SLJIT_CALL,
                    SLJIT_ARGS2(F32, F32, F32),
                    SLJIT_IMM,
                    i->op == BF_ATAN2_F32 ? SLJIT_FUNC_ADDR(atan2f)
                                          : SLJIT_FUNC_ADDR(fmodf)
                );
                sljit_emit_fop1(c, SLJIT_MOV_F32, destination, offset, SLJIT_FR0, 0);
                break;
            case BF_CHECK:
                if (m.countdown) {
                    sljit_emit_op2(c, SLJIT_SUB | SLJIT_SET_CARRY, m.steps, 0, m.steps, 0, SLJIT_IMM, 1);
                    failures[pc] = sljit_emit_jump(c, SLJIT_CARRY);
                    break;
                }
                failures[pc] = sljit_emit_cmp(
                    c,
                    SLJIT_GREATER_EQUAL,
                    m.steps,
                    0,
                    SLJIT_MEM1(m.context),
                    offsetof(behavior_function_context, budget)
                );
                sljit_emit_op2(c, SLJIT_ADD, m.steps, 0, m.steps, 0, SLJIT_IMM, 1);
                break;
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
                sljit_emit_op1(
                    c,
                    SLJIT_MOV,
                    m.steps,
                    0,
                    m.countdown ? SLJIT_MEM1(m.context) : SLJIT_IMM,
                    m.countdown ? offsetof(behavior_function_context, budget) : 0
                );
                enter_locals(&m, i->target);
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
                store_steps(&m, offsetof(behavior_function_context, steps));
                sljit_emit_return(c, SLJIT_MOV32, SLJIT_IMM, 0);
                break;
            case BF_CONSTANT:
                sljit_emit_fop1(
                    c,
                    SLJIT_MOV_F64 | flags,
                    destination,
                    offset,
                    SLJIT_MEM0(),
                    (sljit_sw)&i->value.d
                );
                break;
            case BF_LOCAL:
            case BF_VARIABLE: {
                int local = i->op == BF_LOCAL ? local_registers[i->left] : 0;
                if (local) {
                    if (destination != local) {
                        sljit_emit_fop1(c, SLJIT_MOV_F64 | flags, destination, offset, local, 0);
                    }
                    break;
                }
                int base = i->op == BF_LOCAL ? SLJIT_SP : m.variables;
                sljit_sw address = i->op == BF_LOCAL
                                     ? locals + i->left * sizeof(double)
                                     : i->left * sizeof(pin_value) +
                                           offsetof(pin_value, d);
                sljit_emit_fop1(c, SLJIT_MOV_F64 | flags, destination, offset, SLJIT_MEM1(base), address);
                break;
            }
            case BF_SAVE: {
                int source = registers[i->left], local = local_registers[i->target];
                if (!source || source != local) {
                    sljit_emit_fop1(
                        c,
                        SLJIT_MOV_F64 | flags,
                        local ? local : mem,
                        local ? 0 : locals + i->target * sizeof(double),
                        source ? source : mem,
                        source ? 0 : i->left * sizeof(double)
                    );
                }
                break;
            }
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
                int source = registers[i->left];
                sljit_emit_fop1(
                    c,
                    SLJIT_MOV_F64 | flags,
                    SLJIT_MEM1(base),
                    i->target * sizeof(pin_value) + offsetof(pin_value, d),
                    source ? source : mem,
                    source ? 0 : i->left * sizeof(double)
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
            case BF_ADD:
            case BF_SUB:
            case BF_MUL:
            case BF_DIV:
            case BF_NEG:
            case BF_ABS: {
                int left = registers[i->left], right = registers[i->right];
                if (i->op == BF_NEG || i->op == BF_ABS) {
                    sljit_emit_fop1(
                        c,
                        (i->op == BF_NEG ? SLJIT_NEG_F64 : SLJIT_ABS_F64) | flags,
                        destination,
                        offset,
                        left ? left : mem,
                        left ? 0 : i->left * sizeof(double)
                    );
                    break;
                }
                int op = i->op == BF_ADD ? SLJIT_ADD_F64
                       : i->op == BF_SUB ? SLJIT_SUB_F64
                       : i->op == BF_MUL ? SLJIT_MUL_F64
                                         : SLJIT_DIV_F64;
                sljit_emit_fop2(
                    c,
                    op | flags,
                    destination,
                    offset,
                    left ? left : mem,
                    left ? 0 : i->left * sizeof(double),
                    right ? right : mem,
                    right ? 0 : i->right * sizeof(double)
                );
                break;
            }
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
                    assert(false);
                }
            }
        }
    }
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        if (failures[pc]) {
            bind_here(c, failures[pc]);
            if (m.countdown) {
                sljit_emit_op2(c, SLJIT_ADD, m.steps, 0, m.steps, 0, SLJIT_IMM, 1);
            }
            behavior_jit_failure(&m, &f->instructions[pc], BEHAVIOR_FUNCTION_BUDGET);
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
    free(local_registers);
    free(labels);
    free(jumps);
    free(failures);
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
