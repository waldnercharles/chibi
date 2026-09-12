#include "graph/compiler/behavior_function_jit_internal.h"
#if CHIBI_HAVE_SJLIT
#include "support/array.h"

#include <stddef.h>

static void bind(struct sljit_compiler *c, struct sljit_jump *jump)
{
    struct sljit_label *label = sljit_emit_label(c);
    if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
        sljit_set_label(jump, label);
    }
}
static void move(const behavior_machine *m, pin_type type, int dest, sljit_sw offset, int source, sljit_sw address)
{
    if (type == PIN_F32 || type == PIN_F64) {
        sljit_emit_fop1(m->compiler, type == PIN_F32 ? SLJIT_MOV_F32 : SLJIT_MOV_F64, dest, offset, source, address);
        return;
    }
    sljit_emit_op1(m->compiler, behavior_jit_integer_move(type), dest, offset, source, address);
}
static void constant(const behavior_machine *m, pin_type type, int dest, sljit_sw offset, pin_value value)
{
    if (type == PIN_F32 || type == PIN_F64) {
        if (type == PIN_F32) {
            sljit_emit_fset32(m->compiler, SLJIT_FR0, value.f);
        } else {
            sljit_emit_fset64(m->compiler, SLJIT_FR0, value.d);
        }
        move(m, type, dest, offset, SLJIT_FR0, 0);
        return;
    }
    move(m, type, dest, offset, SLJIT_IMM, (sljit_sw)behavior_scalar_bits(value));
}
static void slot(const behavior_machine *m, int index, int dest, sljit_sw offset)
{
    int reg = m->registers[index];
    move(
        m,
        m->function->slots[index],
        dest,
        offset,
        reg ? reg : SLJIT_MEM1(SLJIT_SP),
        reg ? 0 : index * sizeof(double)
    );
}
static void read_pointer(
    const behavior_machine *m,
    const behavior_function_instruction *i,
    int dest,
    sljit_sw offset
)
{
    struct sljit_compiler *c = m->compiler;
    if (i->op == BF_PRESENT) {
        sljit_emit_op2u(c, SLJIT_SUB | SLJIT_SET_Z, SLJIT_R3, 0, SLJIT_IMM, 0);
        sljit_emit_op_flags(c, SLJIT_MOV32, dest, offset, SLJIT_NOT_EQUAL);
        return;
    }
    constant(m, i->type, dest, offset, (pin_value){ .type = i->type });
    struct sljit_jump *empty = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R3, 0, SLJIT_IMM, 0);
    move(m, i->type, dest, offset, SLJIT_MEM1(SLJIT_R3), i->offset);
    bind(c, empty);
}
/* Reuse one owned destination across contiguous scalar field stores. */
int behavior_jit_result_fields(const behavior_machine *m, int pc)
{
    const behavior_function_instruction *first = &m->function->instructions[pc];
    if ((first->op != BF_RESULT && first->op != BF_STORE) || !first->indirect) {
        return 0;
    }
    struct sljit_compiler *c = m->compiler;
    bool variable = first->op == BF_STORE;
    sljit_emit_op1(
        c,
        SLJIT_MOV_P,
        SLJIT_R3,
        0,
        SLJIT_MEM1(m->context),
        variable ? offsetof(behavior_function_context, variable_storage)
                 : offsetof(behavior_function_context, results)
    );
    sljit_emit_op1(
        c,
        SLJIT_MOV_P,
        SLJIT_R3,
        0,
        SLJIT_MEM1(SLJIT_R3),
        variable ? first->target * sizeof(behavior_variable_storage) +
                       offsetof(behavior_variable_storage, data)
                 : first->target * sizeof(pin_value) + offsetof(pin_value, ptr)
    );
    struct sljit_jump *empty = variable
                                 ? NULL
                                 : sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R3, 0, SLJIT_IMM, 0);
    int end = pc;
    while (end < acount(m->function->instructions)) {
        const behavior_function_instruction *i = &m->function->instructions[end];
        if (i->op != first->op || !i->indirect || i->target != first->target) {
            break;
        }
        slot(m, i->left, SLJIT_MEM1(SLJIT_R3), i->offset);
        end++;
    }
    if (empty) {
        bind(c, empty);
    }
    return end - pc;
}
bool behavior_jit_memory(
    const behavior_machine *m,
    const behavior_function_instruction *i,
    int dest,
    sljit_sw offset
)
{
    struct sljit_compiler *c = m->compiler;
    if (i->op == BF_STORAGE_NAME) {
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_R3,
            0,
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, variable_storage)
        );
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            dest,
            offset,
            SLJIT_MEM1(SLJIT_R3),
            i->left * sizeof(behavior_variable_storage) +
                offsetof(behavior_variable_storage, type_name)
        );
        return true;
    }
    if (i->op == BF_OWN_NAME) {
        slot(m, i->left, SLJIT_R1, 0);
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_R0,
            0,
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, value_owner)
        );
        sljit_emit_icall(
            c,
            SLJIT_CALL,
            SLJIT_ARGS2(P, P, P),
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, own_type_name)
        );
        sljit_emit_op1(c, SLJIT_MOV_P, dest, offset, SLJIT_R0, 0);
        return true;
    }
    if (i->op == BF_STORE_HEADER) {
        sljit_sw base = i->target * sizeof(pin_value);
        slot(m, i->left, SLJIT_MEM1(m->variables), base + offsetof(pin_value, type_name));
        sljit_emit_op1(
            c,
            SLJIT_MOV32,
            SLJIT_MEM1(m->variables),
            base + offsetof(pin_value, type),
            SLJIT_IMM,
            PIN_STRUCT
        );
        sljit_emit_op1(
            c,
            SLJIT_MOV32,
            SLJIT_MEM1(m->variables),
            base + offsetof(pin_value, container),
            SLJIT_IMM,
            PIN_CONTAINER_SINGLE
        );
        slot(m, i->right, SLJIT_R3, 0);
        struct sljit_jump *missing = sljit_emit_cmp(c, SLJIT_EQUAL | SLJIT_32, SLJIT_R3, 0, SLJIT_IMM, 0);
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_R3,
            0,
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, variable_storage)
        );
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_R3,
            0,
            SLJIT_MEM1(SLJIT_R3),
            i->target * sizeof(behavior_variable_storage) +
                offsetof(behavior_variable_storage, data)
        );
        bind(c, missing);
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_MEM1(m->variables), base + offsetof(pin_value, ptr), SLJIT_R3, 0);
        return true;
    }
    if (i->op == BF_RESULT_TYPE_NAME) {
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(m->context), offsetof(behavior_function_context, results));
        slot(m, i->left, SLJIT_MEM1(SLJIT_R3), i->target * sizeof(pin_value) + offsetof(pin_value, type_name));
        return true;
    }
    if (i->op == BF_TYPE_NAME) {
        if (i->target == BF_CONSTANT) {
            sljit_emit_op1(
                c,
                SLJIT_MOV_P,
                dest,
                offset,
                SLJIT_IMM,
                (sljit_sw)i->value.str
            );
            return true;
        }
        struct sljit_jump *missing = NULL, *wrong = NULL;
        bool argument = i->target == BF_ARGUMENT;
        if (argument) {
            missing = sljit_emit_cmp(
                c,
                SLJIT_SIG_LESS_EQUAL | SLJIT_32,
                m->argument_count,
                0,
                SLJIT_IMM,
                i->left
            );
            wrong = sljit_emit_cmp(
                c,
                SLJIT_NOT_EQUAL | SLJIT_32,
                SLJIT_MEM1(m->arguments),
                i->left * sizeof(pin_value) + offsetof(pin_value, type),
                SLJIT_IMM,
                PIN_STRUCT
            );
        }
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            dest,
            offset,
            SLJIT_MEM1(argument ? m->arguments : m->variables),
            i->left * sizeof(pin_value) + offsetof(pin_value, type_name)
        );
        if (argument) {
            struct sljit_jump *done = sljit_emit_jump(c, SLJIT_JUMP);
            bind(c, missing);
            bind(c, wrong);
            sljit_emit_op1(
                c,
                SLJIT_MOV_P,
                dest,
                offset,
                SLJIT_IMM,
                (sljit_sw)i->value.str
            );
            bind(c, done);
        }
        return true;
    }
    if (i->op == BF_SELECT) {
        slot(m, i->left, SLJIT_R3, 0);
        struct sljit_jump *no = sljit_emit_cmp(c, SLJIT_EQUAL | SLJIT_32, SLJIT_R3, 0, SLJIT_IMM, 0);
        slot(m, i->right, dest, offset);
        struct sljit_jump *done = sljit_emit_jump(c, SLJIT_JUMP);
        bind(c, no);
        slot(m, i->target, dest, offset);
        bind(c, done);
        return true;
    }
    if (i->op == BF_REQUIRE || i->op == BF_RESULT_PRESENT) {
        slot(m, i->left, SLJIT_R3, 0);
        struct sljit_jump *
            present = sljit_emit_cmp(c, SLJIT_NOT_EQUAL | SLJIT_32, SLJIT_R3, 0, SLJIT_IMM, 0);
        if (i->op == BF_REQUIRE) {
            behavior_jit_failure(m, i, BEHAVIOR_FUNCTION_DATA);
        } else {
            sljit_emit_op1(
                c,
                SLJIT_MOV_P,
                SLJIT_R3,
                0,
                SLJIT_MEM1(m->context),
                offsetof(behavior_function_context, results)
            );
            sljit_emit_op1(
                c,
                SLJIT_MOV_P,
                SLJIT_MEM1(SLJIT_R3),
                i->target * sizeof(pin_value) + offsetof(pin_value, ptr),
                SLJIT_IMM,
                0
            );
        }
        bind(c, present);
        return true;
    }
    if (!i->indirect && i->op != BF_PRESENT) {
        return false;
    }
    if (i->op == BF_VARIABLE || (i->op == BF_PRESENT && i->target == BF_VARIABLE)) {
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_R3,
            0,
            SLJIT_MEM1(m->variables),
            i->left * sizeof(pin_value) + offsetof(pin_value, ptr)
        );
        read_pointer(m, i, dest, offset);
        return true;
    }
    assert(i->op == BF_ARGUMENT || (i->op == BF_PRESENT && i->target == BF_ARGUMENT));
    struct sljit_jump *missing = sljit_emit_cmp(
        c,
        SLJIT_SIG_LESS_EQUAL | SLJIT_32,
        m->argument_count,
        0,
        SLJIT_IMM,
        i->left
    );
    struct sljit_jump *wrong = sljit_emit_cmp(
        c,
        SLJIT_NOT_EQUAL | SLJIT_32,
        SLJIT_MEM1(m->arguments),
        i->left * sizeof(pin_value) + offsetof(pin_value, type),
        SLJIT_IMM,
        PIN_STRUCT
    );
    sljit_emit_op1(
        c,
        SLJIT_MOV_P,
        SLJIT_R3,
        0,
        SLJIT_MEM1(m->arguments),
        i->left * sizeof(pin_value) + offsetof(pin_value, ptr)
    );
    read_pointer(m, i, dest, offset);
    struct sljit_jump *done = sljit_emit_jump(c, SLJIT_JUMP);
    bind(c, missing);
    bind(c, wrong);
    constant(m, i->type, dest, offset, i->value);
    bind(c, done);
    return true;
}
#endif
