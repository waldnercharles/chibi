#include "execution/jit_internal.h"
#include "execution/random.h"
#include "execution/status.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
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
static bool hardware_math(
    const behavior_machine *m,
    const behavior_function_instruction *i,
    int destination,
    sljit_sw offset
)
{
    if (!behavior_jit_hardware_math(i->op)) {
        return false;
    }
#if defined(SLJIT_CONFIG_ARM_64) && SLJIT_CONFIG_ARM_64
    if (i->op == BF_FMA) {
        load(m->compiler, m->function, m->registers, i->left, SLJIT_FR0);
        load(m->compiler, m->function, m->registers, i->right, SLJIT_FR1);
        load(m->compiler, m->function, m->registers, i->target, SLJIT_FR2);
        sljit_u32 a = sljit_get_register_index(SLJIT_FLOAT_REGISTER, SLJIT_FR0);
        sljit_u32 b = sljit_get_register_index(SLJIT_FLOAT_REGISTER, SLJIT_FR1);
        sljit_u32 add = sljit_get_register_index(SLJIT_FLOAT_REGISTER, SLJIT_FR2);
        assert(a < 32 && b < 32 && add < 32 && i->type == PIN_F64);
        /* FMADD Dd, Dn, Dm, Da: one rounded multiply-add. FR2 is reserved
         * by the allocator only for functions containing this operation. */
        sljit_u32 instruction = 0x1f400000 | a | (a << 5) | (add << 10) | (b << 16);
        sljit_emit_op_custom(m->compiler, &instruction, sizeof instruction);
        sljit_emit_fop1(m->compiler, SLJIT_MOV_F64, destination, offset, SLJIT_FR0, 0);
        return true;
    }
    /* AArch64 scalar double instructions: FSQRT, FRINTM, FRINTP, FRINTA.
     * FRINTA implements the language's halfway-away-from-zero Round. These
     * operations preserve NZCV and touch only the reserved FR0 temporary. */
    const sljit_u32 instructions[] = { 0x1e61c000, 0x1e654000, 0x1e64c000, 0x1e664000 };
    sljit_s32 reg = sljit_get_register_index(SLJIT_FLOAT_REGISTER, SLJIT_FR0);
    assert(reg >= 0 && reg < 32 && i->type == PIN_F64);
    sljit_u32 instruction = instructions[i->op - BF_SQRT] | (sljit_u32)reg |
                            ((sljit_u32)reg << 5);
    load(m->compiler, m->function, m->registers, i->left, SLJIT_FR0);
    sljit_emit_op_custom(m->compiler, &instruction, sizeof instruction);
    sljit_emit_fop1(m->compiler, SLJIT_MOV_F64, destination, offset, SLJIT_FR0, 0);
#else
    (void)m;
    (void)destination;
    (void)offset;
#endif
    return true;
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
    if (f->waits) {
        behavior_jit_wake(m, frame, true);
    }
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
static void failure(
    const behavior_machine *m,
    const behavior_function_instruction *i,
    behavior_function_failure reason,
    bool preserve_message
)
{
    struct sljit_compiler *c = m->compiler;
    if (!preserve_message && i->op != BF_COMMAND_BEGIN && i->op != BF_HOST_CALL) {
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
        : reason == BEHAVIOR_FUNCTION_ARGUMENT  ? FLOW_ERR_ARGUMENT
        : reason == BEHAVIOR_FUNCTION_STATE     ? FLOW_ERR_STATE
        : reason == BEHAVIOR_FUNCTION_HOST_FLOW ? FLOW_ERR_NODE
        : reason >= BEHAVIOR_FUNCTION_NO_RETURN ? FLOW_ERR_INVALID_GRAPH
                                                : FLOW_ERR_STEP_BUDGET
    );
}
void behavior_jit_failure(
    const behavior_machine *m,
    const behavior_function_instruction *i,
    behavior_function_failure reason
)
{
    failure(m, i, reason, false);
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
typedef struct
{
    behavior_machine machine;
    int scratch, saved, floats, stack;
    bool arguments, loop;
} behavior_layout;

static behavior_layout layout(const behavior_function *f, bool inspected, bool scoped)
{
    bool integers = acount(f->suspensions) > 0;
    for (int i = 0; i < acount(f->slots); i++) {
        integers |= behavior_scalar_integer(f->slots[i]) ||
                    f->slots[i] == PIN_BOOL || f->slots[i] == PIN_HANDLE ||
                    f->slots[i] == PIN_TASK || f->slots[i] == PIN_UNION ||
                    f->slots[i] == PIN_POINTER || f->slots[i] == PIN_QUERY ||
                    behavior_scalar_string(f->slots[i]);
    }
    int saved_float = 0;
    bool *invariants = xcalloc((size_t)(acount(f->slots) + 1), sizeof *invariants);
    int *registers = scoped
                       ? xcalloc((size_t)(acount(f->slots) + 1), sizeof *registers)
                       : behavior_jit_registers(f, inspected, &saved_float, invariants);
    bool arguments = false, calls = scoped, loop = false;
    for (int i = 0; i < acount(f->instructions); i++) {
        if (!inspected && !behavior_function_runtime_keeps(f, i)) {
            continue;
        }
        arguments |= f->instructions[i].op == BF_ARGUMENT ||
                     ((f->instructions[i].op == BF_PRESENT ||
                       f->instructions[i].op == BF_TYPE_NAME ||
                       f->instructions[i].op == BF_ARRAY) &&
                      f->instructions[i].target == BF_ARGUMENT);
        calls |= behavior_jit_pointer_comparison(f, &f->instructions[i]) ||
                 behavior_jit_call(f->instructions[i].op) ||
                 (inspected &&
                  behavior_function_inspection_call(f->instructions[i].op));
        loop |= (f->instructions[i].op == BF_JUMP || f->instructions[i].op == BF_BRANCH) &&
                f->instructions[i].target <= i;
    }
    /* Integer divide uses R0/R1, not the C ABI. Keep context in scratch
     * registers when the target has room; save it only across actual calls
     * or on targets with fewer scratch registers. */
    int integer_scratch = arguments ? 10 : 8;
    bool saved = calls ||
                 (integers && SLJIT_NUMBER_OF_SCRATCH_REGISTERS < integer_scratch);
    behavior_machine m = { .compiler = NULL,
                           .inspected = inspected,
                           .scoped = scoped,
                           .function = f,
                           .registers = registers,
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
    int saved_integer = saved ? (arguments ? 5 : 3) : 0;
    int saved_locals = 0;
    int *local_registers = scoped
                             ? xcalloc((size_t)(acount(f->locals) + 1), sizeof *local_registers)
                             : behavior_jit_local_registers(f, saved_integer, scratch, &saved_locals, &saved_float);
    m.local_registers = local_registers;
    if (!scoped) {
        saved_locals += behavior_jit_invariant_registers(
            f,
            registers,
            invariants,
            saved_integer + saved_locals,
            scratch
        );
        behavior_jit_alias_locals(f, registers, local_registers, invariants);
        if (integers) {
            behavior_jit_integer_registers(f, inspected, registers, invariants);
        }
        behavior_jit_coalesce_saves(f, registers, local_registers);
    }
    free(invariants);
    /* DIV/MOD publish their secondary result to the stack. A reused target
     * may already have acquired a register from an earlier definition or a
     * local alias. Keep every definition and use of that slot on the stack;
     * writing an aliased register would also overwrite the persistent local. */
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if ((i->op == BF_DIV || i->op == BF_MOD) &&
            (i->type == PIN_I32 || i->type == PIN_I64) && i->target >= 0) {
            registers[i->target] = 0;
        }
    }
    int stack_slots = behavior_jit_stack_slots(f, registers);
    int locals = stack_slots * sizeof(double);
    int parents = locals + acount(f->locals) * sizeof(double);
    int stack_size = parents + (acount(f->frames) > 1
                                    ? acount(f->frames) * sizeof(sljit_sw)
                                    : 0);
    m.locals = locals;
    m.parents = parents;
    return (behavior_layout){
        m,           scratch,    saved_integer + saved_locals,
        saved_float, stack_size, arguments,
        loop
    };
}

typedef struct
{
    struct sljit_label **resumes;
} behavior_emission;

static behavior_emission emit_body(behavior_layout *layout, struct sljit_compiler *c, int budget_proven)
{
    behavior_machine m = layout->machine;
    m.compiler = c;
    const behavior_function *f = m.function;
    int *uses = NULL;
    bool *targets = NULL;
    if (!m.scoped) {
        behavior_jit_usage(f, &uses, &targets);
        m.single_uses = uses;
        m.branch_targets = targets;
    }
    bool inspected = m.inspected, arguments = layout->arguments, loop = layout->loop;
    const int *registers = m.registers, *local_registers = m.local_registers;
    int locals = m.locals, parents = m.parents;
    sljit_emit_op1(c, SLJIT_MOV_P, m.context, 0, SLJIT_R0, 0);
    sljit_emit_op1(c, SLJIT_MOV_P, m.variables, 0, SLJIT_MEM1(m.context), offsetof(behavior_function_context, variables));
    behavior_jit_load_variables(&m);
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
    /* A loop keeps remaining budget in a register. Parent frames store the
     * same representation; only returns and failures materialize step counts. */
    int check = f->instructions[0].op == BF_POINT ? 1 : 0;
    if (!m.scoped && loop && f->instructions[check].op == BF_CHECK && !f->root_steps) {
        struct sljit_jump *valid = sljit_emit_cmp(
            c,
            SLJIT_LESS_EQUAL,
            m.steps,
            0,
            SLJIT_MEM1(m.context),
            offsetof(behavior_function_context, budget)
        );
        behavior_jit_failure(&m, &f->instructions[check], BEHAVIOR_FUNCTION_BUDGET);
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
        if (budget_proven >= 0) {
            /* The entry guard proved the complete node count. Account once;
             * no accepted instruction observes steps before successful exit. */
            sljit_emit_op2(c, SLJIT_SUB, m.steps, 0, m.steps, 0, SLJIT_MEM1(SLJIT_SP), budget_proven);
        }
    }
    assert(budget_proven < 0 || m.countdown);
    struct sljit_label **labels = xcalloc((size_t)acount(f->instructions), sizeof *labels);
    struct sljit_jump **failures = xcalloc((size_t)acount(f->instructions), sizeof *failures);
    struct sljit_jump **jumps = xcalloc((size_t)acount(f->instructions), sizeof *jumps);
    struct sljit_label **resume_labels = xcalloc((size_t)(acount(f->suspensions) + 1), sizeof *resume_labels);
    behavior_jit_resume(&m, jumps, resume_labels);
    enter_locals(&m, 0);
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (m.scoped && !(pc % 256) && behavior_execution_scope_cancelled()) {
            sljit_set_compiler_memory_error(c);
            break;
        }
        labels[pc] = sljit_emit_label(c);
        if (!inspected && !behavior_function_runtime_keeps(f, pc)) {
            continue;
        }
        if (m.scoped) {
            sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS0(P), SLJIT_IMM, (sljit_sw)behavior_execution_scope_step);
            struct sljit_jump *ok = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
            sljit_emit_op1(
                c,
                SLJIT_MOV_P,
                SLJIT_MEM1(m.context),
                offsetof(behavior_function_context, error_message),
                SLJIT_R0,
                0
            );
            failure(&m, i, BEHAVIOR_FUNCTION_BUDGET, true);
            bind_here(c, ok);
        }
        if (i->op == BF_SAVE && behavior_function_inspectable(f) &&
            f->locals[i->target].parameter) {
            behavior_jit_parameter(&m, i);
        }
        int flags = width(i->type), mem = SLJIT_MEM1(SLJIT_SP);
        int destination = registers[i->output] ? registers[i->output] : mem;
        sljit_sw offset = registers[i->output] ? 0 : i->output * sizeof(double);
        int fields = m.scoped ? 0 : behavior_jit_result_fields(&m, pc);
        if (fields) {
            for (int k = 1; k < fields; k++) {
                labels[pc + k] = labels[pc];
            }
            pc += fields - 1;
            continue;
        }
        struct sljit_jump *branch = NULL;
        int compared = m.scoped ? 0 : behavior_jit_field_branch(&m, pc, &branch);
        if (compared) {
            for (int k = 1; k < compared; k++) {
                labels[pc + k] = labels[pc];
            }
            pc += compared - 1;
            jumps[pc] = branch;
            continue;
        }
        int predicate = m.scoped ? 0 : behavior_jit_fused_branch(&m, pc, &branch);
        if (predicate) {
            for (int k = 1; k < predicate; k++) {
                labels[pc + k] = labels[pc];
            }
            pc += predicate - 1;
            if (f->instructions[pc].op == BF_ASSERT) {
                behavior_jit_failure(
                    &m,
                    &f->instructions[pc],
                    f->instructions[pc].target
                );
                bind_here(c, branch);
            } else {
                jumps[pc] = branch;
            }
            continue;
        }
        if (behavior_jit_host_call(&m, i, destination, offset) ||
            behavior_jit_memory(&m, i, destination, offset) ||
            behavior_jit_integer(&m, i, destination, offset)) {
            continue;
        }
        if (behavior_jit_watch(&m, i)) {
            continue;
        }
        if (hardware_math(&m, i, destination, offset)) {
            continue;
        }
        switch (i->op) {
            case BF_FMA:
                load(c, f, registers, i->left, SLJIT_FR0);
                load(c, f, registers, i->right, SLJIT_FR1);
                load(c, f, registers, i->target, SLJIT_FR2);
                sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS3(F64, F64, F64, F64), SLJIT_IMM, SLJIT_FUNC_ADDR(fma));
                sljit_emit_fop1(c, SLJIT_MOV_F64, destination, offset, SLJIT_FR0, 0);
                break;
            case BF_RANDOM:
                sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS0(32), SLJIT_IMM, SLJIT_FUNC_ADDR(rand));
                sljit_emit_op1(c, SLJIT_MOV32, destination, offset, SLJIT_R0, 0);
                break;
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
                MATH_BINARY(RANDOM_RANGE, RandomRange, behavior_random_range)
#undef MATH_BINARY
            case BF_MOD_F32:
                load(c, f, registers, i->left, SLJIT_FR0);
                load(c, f, registers, i->right, SLJIT_FR1);
                sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS2(F32, F32, F32), SLJIT_IMM, SLJIT_FUNC_ADDR(fmodf));
                sljit_emit_fop1(c, SLJIT_MOV_F32, destination, offset, SLJIT_FR0, 0);
                break;
            case BF_SOURCE_WATCH: {
                int reg = registers[i->left];
                int source = reg ? reg : SLJIT_MEM1(SLJIT_SP);
                sljit_sw offset = reg ? 0 : i->left * sizeof(double);
                if (i->type == PIN_F32 || i->type == PIN_F64) {
                    sljit_emit_fop1(
                        c,
                        i->type == PIN_F32 ? SLJIT_MOV_F32 : SLJIT_MOV_F64,
                        SLJIT_MEM1(m.context),
                        offsetof(behavior_function_context, watched.d),
                        source,
                        offset
                    );
                } else {
                    sljit_emit_op1(
                        c,
                        behavior_jit_integer_move(i->type),
                        SLJIT_MEM1(m.context),
                        offsetof(behavior_function_context, watched.d),
                        source,
                        offset
                    );
                }
                sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, m.context, 0);
                sljit_emit_op1(c, SLJIT_MOV32, SLJIT_R1, 0, SLJIT_IMM, i->target);
                sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS2V(P, 32), SLJIT_IMM, SLJIT_FUNC_ADDR(behavior_source_capture));
                break;
            }
            case BF_SOURCE_POINT: {
                sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, m.context, 0);
                sljit_emit_op1(c, SLJIT_MOV32, SLJIT_R1, 0, SLJIT_IMM, i->left);
                sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS2(32, P, 32), SLJIT_IMM, SLJIT_FUNC_ADDR(behavior_source_pause));
                struct sljit_jump *
                    proceed = sljit_emit_cmp(c, SLJIT_EQUAL | SLJIT_32, SLJIT_R0, 0, SLJIT_IMM, 0);
                behavior_jit_suspend(&m, pc);
                bind_here(c, proceed);
                break;
            }
            case BF_POINT: {
                if (!inspected) {
                    break;
                }
                struct sljit_jump *disabled = sljit_emit_cmp(
                    c,
                    SLJIT_EQUAL,
                    SLJIT_MEM1(m.context),
                    offsetof(behavior_function_context, inspect),
                    SLJIT_IMM,
                    0
                );
                sljit_emit_op1(
                    c,
                    SLJIT_MOV_P,
                    SLJIT_R0,
                    0,
                    SLJIT_MEM1(m.context),
                    offsetof(behavior_function_context, inspection_user)
                );
                sljit_emit_op2(
                    c,
                    SLJIT_ADD,
                    SLJIT_R1,
                    0,
                    SLJIT_MEM1(m.context),
                    offsetof(behavior_function_context, points),
                    SLJIT_IMM,
                    i->left * sizeof(behavior_function_point)
                );
                sljit_emit_icall(
                    c,
                    SLJIT_CALL,
                    SLJIT_ARGS2(32, P, P),
                    SLJIT_MEM1(m.context),
                    offsetof(behavior_function_context, inspect)
                );
                struct sljit_jump *
                    proceed = sljit_emit_cmp(c, SLJIT_EQUAL | SLJIT_32, SLJIT_R0, 0, SLJIT_IMM, 0);
                behavior_jit_suspend(&m, pc);
                bind_here(c, proceed);
                bind_here(c, disabled);
                break;
            }
            case BF_DELAY: {
                load(c, f, registers, i->left, SLJIT_FR0);
                sljit_emit_fop2(
                    c,
                    SLJIT_ADD_F64,
                    SLJIT_FR0,
                    0,
                    SLJIT_FR0,
                    0,
                    SLJIT_MEM1(m.context),
                    offsetof(behavior_function_context, now)
                );
                sljit_emit_fop1(
                    c,
                    SLJIT_MOV_F64,
                    SLJIT_MEM1(m.context),
                    offsetof(behavior_function_context, wake_time),
                    SLJIT_FR0,
                    0
                );
                struct sljit_jump *proceed = sljit_emit_fcmp(
                    c,
                    SLJIT_ORDERED_LESS_EQUAL,
                    SLJIT_FR0,
                    0,
                    SLJIT_MEM1(m.context),
                    offsetof(behavior_function_context, now)
                );
                behavior_jit_wake(&m, i->frame, false);
                behavior_jit_suspend(&m, pc);
                bind_here(c, proceed);
                break;
            }
            case BF_INVOKE:
            case BF_SUSPEND: behavior_jit_suspend(&m, pc); break;
            case BF_CHECK:
                if (budget_proven >= 0) {
                    break;
                }
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
                if (behavior_function_inspectable(f)) {
                    behavior_jit_frame_steps(&m, i->frame);
                }
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
                if (behavior_function_inspectable(f)) {
                    behavior_jit_frame_steps(&m, i->target);
                }
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
                    SLJIT_MOV_U8,
                    SLJIT_MEM1(m.context),
                    offsetof(behavior_function_context, returned),
                    SLJIT_IMM,
                    1
                );
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
                behavior_jit_flush_variables(&m);
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
            case BF_CONVERT: {
                int source = registers[i->left];
                sljit_emit_fop1(
                    c,
                    i->type == PIN_F32 ? SLJIT_CONV_F32_FROM_F64 : SLJIT_CONV_F64_FROM_F32,
                    destination,
                    offset,
                    source ? source : mem,
                    source ? 0 : i->left * sizeof(double)
                );
                break;
            }
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
            case BF_MIN:
            case BF_MAX: {
                int left = registers[i->left], right = registers[i->right];
                if (!right) {
                    load(c, f, registers, i->right, SLJIT_FR1);
                    right = SLJIT_FR1;
                }
                int condition = i->op == BF_MIN ? SLJIT_ORDERED_LESS
                                                : SLJIT_ORDERED_GREATER;
                sljit_emit_fop1(
                    c,
                    SLJIT_CMP_F64 | flags | SLJIT_SET(condition),
                    left ? left : mem,
                    left ? 0 : i->left * sizeof(double),
                    right,
                    0
                );
                /* Equality and unordered comparisons select the right value,
                 * preserving the language's signed-zero and NaN behavior. */
                sljit_emit_fselect(
                    c,
                    condition | flags,
                    registers[i->output] ? destination : SLJIT_FR0,
                    left ? left : mem,
                    left ? 0 : i->left * sizeof(double),
                    right
                );
                if (!registers[i->output]) {
                    sljit_emit_fop1(c, SLJIT_MOV_F64 | flags, destination, offset, SLJIT_FR0, 0);
                }
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
    if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
        for (int pc = 0; pc < acount(f->instructions); pc++) {
            if (jumps[pc]) {
                sljit_set_label(
                    jumps[pc],
                    labels
                        [(f->instructions[pc].op == BF_SUSPEND ||
                          f->instructions[pc].op == BF_DELAY ||
                          f->instructions[pc].op == BF_POINT ||
                          f->instructions[pc].op == BF_SOURCE_POINT ||
                          f->instructions[pc].op == BF_INVOKE)
                             ? pc + 1
                             : f->instructions[pc].target]
                );
            }
        }
    }
    free(labels);
    free(jumps);
    free(failures);
    free(uses);
    free(targets);
    return (behavior_emission){ resume_labels };
}

/* The guard uses only expression scratch registers before body entry. R3
 * retains the incoming context for both the unchecked and original paths. */
static void loop_input(struct sljit_compiler *c, const behavior_function_instruction *i, int reg)
{
    if (i->op == BF_CONSTANT) {
        sljit_emit_op1(c, SLJIT_MOV, reg, 0, SLJIT_IMM, i->value.i);
        return;
    }
    struct sljit_jump *missing = sljit_emit_cmp(
        c,
        SLJIT_SIG_LESS_EQUAL | SLJIT_32,
        SLJIT_MEM1(SLJIT_R3),
        offsetof(behavior_function_context, argument_count),
        SLJIT_IMM,
        i->left
    );
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R4, 0, SLJIT_MEM1(SLJIT_R3), offsetof(behavior_function_context, arguments));
    struct sljit_jump *wrong = sljit_emit_cmp(
        c,
        SLJIT_NOT_EQUAL | SLJIT_32,
        SLJIT_MEM1(SLJIT_R4),
        i->left * sizeof(pin_value) + offsetof(pin_value, type),
        SLJIT_IMM,
        PIN_I32
    );
    sljit_emit_op1(
        c,
        SLJIT_MOV_S32,
        reg,
        0,
        SLJIT_MEM1(SLJIT_R4),
        i->left * sizeof(pin_value) + offsetof(pin_value, i)
    );
    struct sljit_jump *done = sljit_emit_jump(c, SLJIT_JUMP);
    bind_here(c, missing);
    bind_here(c, wrong);
    sljit_emit_op1(c, SLJIT_MOV, reg, 0, SLJIT_IMM, i->value.i);
    bind_here(c, done);
}
static struct sljit_jump **loop_budget_guard(
    struct sljit_compiler *c,
    const behavior_function *f,
    const behavior_loop_budget *proof,
    int cost_slot,
    const int *variables
)
{
    struct sljit_jump **slow = NULL;
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_R0, 0);
    loop_input(c, &f->instructions[proof->first], SLJIT_R1);
    loop_input(c, &f->instructions[proof->last], SLJIT_R2);
    sljit_emit_op2(c, SLJIT_SUB, SLJIT_R0, 0, SLJIT_R2, 0, SLJIT_R1, 0);
    struct sljit_jump *positive = sljit_emit_cmp(c, SLJIT_SIG_GREATER_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
    sljit_emit_op2(c, SLJIT_SUB, SLJIT_R0, 0, SLJIT_IMM, 0, SLJIT_R0, 0);
    bind_here(c, positive);
    /* The difference is at most UINT32_MAX and each cost is bounded by
     * the IR's int instruction count. Their product and sum fit in U64. */
    sljit_emit_op2(c, SLJIT_MUL, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, proof->iteration_steps);
    sljit_emit_op2(c, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, proof->entry_steps);
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_R3), offsetof(behavior_function_context, steps));
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_R3), offsetof(behavior_function_context, budget));
    apush(slow, sljit_emit_cmp(c, SLJIT_GREATER, SLJIT_R1, 0, SLJIT_R2, 0));
    sljit_emit_op2(c, SLJIT_SUB, SLJIT_R2, 0, SLJIT_R2, 0, SLJIT_R1, 0);
    apush(slow, sljit_emit_cmp(c, SLJIT_LESS, SLJIT_R2, 0, SLJIT_R0, 0));
    if (variables) {
        /* Result storage may be caller-owned and alias variables. The checked
         * body retains publication order in that case; promotion cannot. */
        for (int pc = 0; pc < acount(f->instructions); pc++) {
            const behavior_function_instruction *i = &f->instructions[pc];
            if (i->op != BF_VARIABLE || !variables[i->left]) {
                continue;
            }
            sljit_emit_op1(
                c,
                SLJIT_MOV_P,
                SLJIT_R1,
                0,
                SLJIT_MEM1(SLJIT_R3),
                offsetof(behavior_function_context, variables)
            );
            sljit_emit_op2(c, SLJIT_ADD, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, i->left * sizeof(pin_value));
            for (int at = 0; at < acount(f->instructions); at++) {
                const behavior_function_instruction *result = &f->instructions[at];
                if (result->op != BF_RESULT) {
                    continue;
                }
                sljit_emit_op1(
                    c,
                    SLJIT_MOV_P,
                    SLJIT_R2,
                    0,
                    SLJIT_MEM1(SLJIT_R3),
                    offsetof(behavior_function_context, results)
                );
                sljit_emit_op2(c, SLJIT_ADD, SLJIT_R2, 0, SLJIT_R2, 0, SLJIT_IMM, result->target * sizeof(pin_value));
                apush(slow, sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_R2, 0));
            }
        }
    }
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(SLJIT_SP), cost_slot, SLJIT_R0, 0);
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_R3, 0);
    return slow;
}

static void *emit(const behavior_function *f, bool inspected, bool scoped)
{
    struct sljit_compiler *c = sljit_create_compiler(NULL);
    if (!c) {
        return NULL;
    }
    behavior_function runtime = { 0 };
    const behavior_function *body = f;
    if (!inspected && body->runtime_body) {
        runtime = *body;
        runtime.instructions = body->runtime_body;
        body = &runtime;
    }
    behavior_layout layouts = layout(body, inspected, scoped);
    int scratch = layouts.scratch, saved = layouts.saved,
        floats = layouts.floats, stack = layouts.stack;
    behavior_loop_budget proof;
    bool guarded = !inspected && sizeof(sljit_sw) == 8 &&
                   behavior_function_loop_budget(body, &proof);
    behavior_layout fast_layout = layouts;
    if (guarded) {
        bool immutable_arguments = true;
        for (int pc = 0; pc < acount(body->instructions); pc++) {
            if (body->instructions[pc].op == BF_ARGUMENT && pc != proof.first &&
                pc != proof.last) {
                immutable_arguments = false;
            }
        }
        if (immutable_arguments) {
            int promoted = 0;
            fast_layout.machine
                .variable_registers = behavior_jit_variable_registers(body, saved, scratch, &promoted);
            saved += promoted;
            size_t register_size = (size_t)(acount(body->slots) + 1) * sizeof(int);
            int *registers = xmalloc(register_size);
            memcpy(registers, fast_layout.machine.registers, register_size);
            behavior_jit_alias_variables(body, registers, fast_layout.machine.variable_registers);
            fast_layout.machine.registers = registers;
        }
    }
    int cost_slot = stack;
    if (guarded) {
        stack += sizeof(sljit_sw);
    }
    assert(scratch + saved <= SLJIT_NUMBER_OF_REGISTERS);
    sljit_emit_enter(
        c,
        0,
        SLJIT_ARGS1(32, P_R),
        scratch | SLJIT_ENTER_FLOAT(SLJIT_NUMBER_OF_SCRATCH_FLOAT_REGISTERS),
        saved | SLJIT_ENTER_FLOAT(floats),
        stack
    );
    if (guarded) {
        struct sljit_jump **slow = loop_budget_guard(
            c,
            body,
            &proof,
            cost_slot,
            fast_layout.machine.variable_registers
        );
        behavior_emission fast = emit_body(&fast_layout, c, cost_slot);
        free(fast.resumes);
        if (fast_layout.machine.registers != layouts.machine.registers) {
            free((void *)fast_layout.machine.registers);
        }
        free((void *)fast_layout.machine.variable_registers);
        struct sljit_label *checked = sljit_emit_label(c);
        if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
            for (int i = 0; i < acount(slow); i++) {
                sljit_set_label(slow[i], checked);
            }
        }
        afree(slow);
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_R3, 0);
    }
    behavior_emission emission = emit_body(&layouts, c, -1);
    void *code = NULL;
    if (scoped && behavior_execution_scope_cancelled()) {
        sljit_set_compiler_memory_error(c);
    }
    if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
        code = sljit_generate_code(c, 0, NULL);
        if (code) {
            for (int j = 0; j < acount(f->suspensions); j++) {
                (scoped      ? f->artifact->scoped_addresses
                 : inspected ? f->artifact->inspection_addresses
                             : f->artifact->resume_addresses)
                    [f->suspensions[j].pc + 1] = sljit_get_label_addr(emission.resumes[j]);
            }
        }
    }
    free((void *)layouts.machine.registers);
    free((void *)layouts.machine.local_registers);
    free(emission.resumes);
    sljit_free_compiler(c);
    return code;
}
void *behavior_function_jit(const behavior_function *f, bool inspected)
{
    return emit(f, inspected, false);
}
void *behavior_function_jit_scoped(const behavior_function *f)
{
    return emit(f, true, true);
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
        (sljit_sw)f->fast->artifact->code
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
        (sljit_sw)f->artifact->code
    );
    void *code = sljit_get_compiler_error(c) == SLJIT_SUCCESS
                   ? sljit_generate_code(c, 0, NULL)
                   : NULL;
    sljit_free_compiler(c);
    return code;
}

#else
void *behavior_function_jit_scoped(const behavior_function *)
{
    return NULL;
}
void *behavior_function_jit(const behavior_function *, bool)
{
    return NULL;
}
void *behavior_function_guard(const behavior_function *)
{
    return NULL;
}
#endif
