#include "ecs/execution.h"
#include "execution/jit_internal.h"
#include "types/text.h"
#if CHIBI_HAVE_SJLIT
#include "support/array.h"

#include <stddef.h>
#include <string.h>

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
/* Bound code growth and stop on each mismatching byte, including NUL. Wide
 * loads could cross the end of a shorter input into an unmapped page. */
static bool short_string_equal(
    const behavior_machine *m,
    const behavior_function_instruction *i,
    int dest,
    sljit_sw offset
)
{
    enum
    {
        MAX_BYTES = 8
    };
    const char *text = NULL;
    for (int pc = 0; pc < acount(m->function->instructions); pc++) {
        const behavior_function_instruction *constant = &m->function->instructions[pc];
        if (constant->op == BF_CONSTANT && constant->output == i->right &&
            constant->type == PIN_NAME) {
            text = constant->value.str ? constant->value.str : "";
            break;
        }
    }
    if (!text || strnlen(text, MAX_BYTES + 1) > MAX_BYTES) {
        return false;
    }
    struct sljit_compiler *c = m->compiler;
    struct sljit_jump *misses[MAX_BYTES + 2];
    int count = 0;
    slot(m, i->left, SLJIT_R0, 0);
    struct sljit_jump *empty = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
    if (*text) {
        misses[count++] = empty;
    }
    for (int j = 0;; j++) {
        sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_R0), j);
        misses[count++] = sljit_emit_cmp(c, SLJIT_NOT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, (unsigned char)text[j]);
        if (!text[j]) {
            break;
        }
    }
    if (!*text) {
        bind(c, empty);
    }
    sljit_emit_op1(c, SLJIT_MOV_U8, dest, offset, SLJIT_IMM, 1);
    struct sljit_jump *done = sljit_emit_jump(c, SLJIT_JUMP);
    for (int j = 0; j < count; j++) {
        bind(c, misses[j]);
    }
    sljit_emit_op1(c, SLJIT_MOV_U8, dest, offset, SLJIT_IMM, 0);
    bind(c, done);
    return true;
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
    if (i->op == BF_STATE_SELECT) {
        slot(m, i->right, SLJIT_R3, 0);
        slot(m, i->left, SLJIT_R2, 0);
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_R0,
            0,
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, value_owner)
        );
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_R1,
            0,
            SLJIT_IMM,
            (sljit_sw)&m->function->state_selections[i->target]
        );
        sljit_emit_icall(
            c,
            SLJIT_CALL,
            SLJIT_ARGS4(32, P, P, 32, 32),
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, select_state)
        );
        move(m, PIN_I32, dest, offset, SLJIT_R0, 0);
        return true;
    }
    if (i->op == BF_RETRIGGER) {
        slot(m, i->left, SLJIT_FR0, 0);
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, m->context, 0);
        sljit_emit_op1(c, SLJIT_MOV32, SLJIT_R1, 0, SLJIT_IMM, i->frame);
        sljit_emit_op1(c, SLJIT_MOV32, SLJIT_R2, 0, SLJIT_IMM, i->target);
        sljit_emit_icall(
            c,
            SLJIT_CALL,
            SLJIT_ARGS4(32, P, 32, 32, F64),
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, retrigger)
        );
        move(m, PIN_BOOL, dest, offset, SLJIT_R0, 0);
        return true;
    }
    if (i->op == BF_COMMAND_SERVICE) {
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(m->context), offsetof(behavior_function_context, commands));
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            dest,
            offset,
            SLJIT_MEM1(SLJIT_R3),
            i->target * sizeof(behavior_command_binding) +
                offsetof(behavior_command_binding, service)
        );
        return true;
    }
    if (i->op == BF_COMMAND_RESULT) {
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_R3,
            0,
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, command_results)
        );
        move(m, i->type, dest, offset, SLJIT_MEM1(SLJIT_R3), i->left * sizeof(pin_value) + i->offset);
        return true;
    }
    if (i->op == BF_COMMAND_ARGUMENT || i->op == BF_COMMAND_OUTPUT) {
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_R3,
            0,
            SLJIT_MEM1(m->context),
            i->op == BF_COMMAND_OUTPUT
                ? offsetof(behavior_function_context, command_results)
                : offsetof(behavior_function_context, command_arguments)
        );
        slot(m, i->left, SLJIT_MEM1(SLJIT_R3), i->target * sizeof(pin_value) + i->offset);
        return true;
    }
    if (i->op == BF_COMMAND) {
        const behavior_function_command *command = &m->function->commands[i->target];
        sljit_sw at = i->target * sizeof(behavior_command_binding);
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(m->context), offsetof(behavior_function_context, commands));
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_R0,
            0,
            SLJIT_MEM1(SLJIT_R3),
            at + offsetof(behavior_command_binding, service)
        );
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_R1,
            0,
            SLJIT_MEM1(SLJIT_R3),
            at + offsetof(behavior_command_binding, reference)
        );
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_R2,
            0,
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, command_arguments)
        );
        sljit_emit_op2(c, SLJIT_ADD, SLJIT_R2, 0, SLJIT_R2, 0, SLJIT_IMM, command->first * sizeof(pin_value));
        sljit_emit_op1(c, SLJIT_MOV32, SLJIT_R3, 0, SLJIT_IMM, command->count);
        sljit_emit_icall(
            c,
            SLJIT_CALL,
            SLJIT_ARGS4(32, P, P, P, 32),
            SLJIT_IMM,
            SLJIT_FUNC_ADDR(command->command->invoke)
        );
        sljit_emit_op1(
            c,
            SLJIT_MOV32,
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, error_status),
            SLJIT_R0,
            0
        );
        struct sljit_jump *
            ok = sljit_emit_cmp(c, SLJIT_SIG_GREATER_EQUAL | SLJIT_32, SLJIT_R0, 0, SLJIT_IMM, 0);
        behavior_jit_failure(m, i, BEHAVIOR_FUNCTION_COMMAND);
        bind(c, ok);
        return true;
    }
    if (i->op == BF_COMMAND_BEGIN) {
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, m->context, 0);
        sljit_emit_op1(c, SLJIT_MOV32, SLJIT_R1, 0, SLJIT_IMM, i->target);
        sljit_emit_icall(
            c,
            SLJIT_CALL,
            SLJIT_ARGS2(32, P, 32),
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, begin_command)
        );
        sljit_emit_op1(
            c,
            SLJIT_MOV32,
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, error_status),
            SLJIT_R0,
            0
        );
        struct sljit_jump *
            ok = sljit_emit_cmp(c, SLJIT_SIG_GREATER_EQUAL | SLJIT_32, SLJIT_R0, 0, SLJIT_IMM, 0);
        behavior_jit_failure(m, i, BEHAVIOR_FUNCTION_COMMAND);
        bind(c, ok);
        return true;
    }
    if (i->op == BF_QUERY) {
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(m->context), offsetof(behavior_function_context, queries));
        sljit_sw at = i->target * sizeof(behavior_query_binding);
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_R0,
            0,
            SLJIT_MEM1(SLJIT_R3),
            at + offsetof(behavior_query_binding, service)
        );
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_R3), at + offsetof(behavior_query_binding, key));
        sljit_emit_op2(c, SLJIT_ADD, SLJIT_R2, 0, SLJIT_R3, 0, SLJIT_IMM, at + offsetof(behavior_query_binding, snapshot));
        sljit_emit_icall(
            c,
            SLJIT_CALL,
            SLJIT_ARGS3(P, P, P, P),
            SLJIT_MEM1(SLJIT_R3),
            at + offsetof(behavior_query_binding, read)
        );
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, error_message),
            SLJIT_R0,
            0
        );
        struct sljit_jump *ok = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
        behavior_jit_failure(m, i, BEHAVIOR_FUNCTION_HOST_DATA);
        bind(c, ok);
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(m->context), offsetof(behavior_function_context, queries));
        sljit_emit_op2(c, SLJIT_ADD, dest, offset, SLJIT_R3, 0, SLJIT_IMM, at + offsetof(behavior_query_binding, snapshot));
        return true;
    }
    if (i->op == BF_READ) {
        slot(m, i->left, SLJIT_R3, 0);
        read_pointer(m, i, dest, offset);
        return true;
    }
    if (i->op == BF_ARRAY) {
        bool argument = i->target == BF_ARGUMENT;
        struct sljit_jump *missing = NULL, *wrong = NULL;
        constant(
            m,
            BF_POINTER_TYPE,
            dest,
            offset,
            (pin_value){ .type = BF_POINTER_TYPE, .ptr = i->value.ptr }
        );
        if (i->target == BF_CONSTANT) {
            return true;
        }
        int base = argument ? m->arguments : m->variables;
        sljit_sw at = i->left * sizeof(pin_value);
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
                SLJIT_MEM1(base),
                at + offsetof(pin_value, type),
                SLJIT_IMM,
                i->value.type
            );
        }
        constant(m, BF_POINTER_TYPE, dest, offset, (pin_value){ .type = BF_POINTER_TYPE });
        struct sljit_jump *container = sljit_emit_cmp(
            c,
            SLJIT_NOT_EQUAL | SLJIT_32,
            SLJIT_MEM1(base),
            at + offsetof(pin_value, container),
            SLJIT_IMM,
            i->value.container
        );
        sljit_emit_op1(c, SLJIT_MOV_P, dest, offset, SLJIT_MEM1(base), at + offsetof(pin_value, ptr));
        bind(c, container);
        if (argument) {
            bind(c, missing);
            bind(c, wrong);
        }
        return true;
    }
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
    if (i->op == BF_STORE_VALUE) {
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R2, 0, SLJIT_MEM1(m->context), offsetof(behavior_function_context, data));
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_R2), offsetof(behavior_data_storage, values));
        sljit_emit_op2(c, SLJIT_ADD, SLJIT_R2, 0, SLJIT_R2, 0, SLJIT_IMM, i->left * sizeof(pin_value));
        sljit_emit_op1(c, SLJIT_MOV32, SLJIT_R1, 0, SLJIT_IMM, i->target);
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
            SLJIT_ARGS3V(P, 32, P),
            SLJIT_MEM1(m->context),
            i->offset == BF_STORE_VALUE_SET_ADD
                ? offsetof(behavior_function_context, add_set_value)
                : offsetof(behavior_function_context, store_value)
        );
        return true;
    }
    if (i->op == BF_INDEX) {
        slot(m, i->right, SLJIT_R0, 0);
        sljit_emit_op1(c, SLJIT_MOV_S32, SLJIT_R0, 0, SLJIT_R0, 0);
        assert(i->offset > 0);
        bool power_two = !(i->offset & (i->offset - 1));
        int shift = 0;
        for (size_t value = i->offset; value > 1; value >>= 1) {
            shift++;
        }
        sljit_emit_op2(
            c,
            power_two ? SLJIT_SHL : SLJIT_MUL,
            SLJIT_R0,
            0,
            SLJIT_R0,
            0,
            SLJIT_IMM,
            power_two ? shift : (sljit_sw)i->offset
        );
        slot(m, i->left, SLJIT_R1, 0);
        sljit_emit_op2(c, SLJIT_ADD, dest, offset, SLJIT_R1, 0, SLJIT_R0, 0);
        return true;
    }
    if (i->op == BF_STRING_EQUAL) {
        if (short_string_equal(m, i, dest, offset)) {
            return true;
        }
        slot(m, i->left, SLJIT_R0, 0);
        slot(m, i->right, SLJIT_R1, 0);
        sljit_emit_icall(
            c,
            SLJIT_CALL,
            SLJIT_ARGS2(32, P, P),
            SLJIT_IMM,
            m->function->slots[i->left] == PIN_STR ? (sljit_sw)behavior_text_equal
                                                   : (sljit_sw)behavior_string_equal
        );
        sljit_emit_op1(c, SLJIT_MOV_U8, dest, offset, SLJIT_R0, 0);
        return true;
    }
    if (i->op == BF_COPY_ARRAY) {
        slot(m, i->left, SLJIT_R1, 0);
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, m->context, 0);
        sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS2(P, P, P), SLJIT_IMM, (sljit_sw)behavior_function_copy_array);
        struct sljit_jump *ok = sljit_emit_cmp(c, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
        behavior_jit_failure(m, i, BEHAVIOR_FUNCTION_HOST_DATA);
        bind(c, ok);
        sljit_emit_op1(c, SLJIT_MOV_P, dest, offset, SLJIT_R0, 0);
        return true;
    }
    if (i->op == BF_COPY_STRING || i->op == BF_INTERN_STRING) {
        slot(m, i->left, SLJIT_R1, 0);
        if (i->op == BF_INTERN_STRING && i->type == PIN_STR) {
            slot(m, i->left, SLJIT_R2, 0);
            slot(m, i->right, SLJIT_R1, 0);
            sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, m->context, 0);
            sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS3(P, P, P, P), SLJIT_IMM, (sljit_sw)behavior_function_intern_text);
            struct sljit_jump *ok = sljit_emit_cmp(c, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
            behavior_jit_failure(m, i, BEHAVIOR_FUNCTION_HOST_DATA);
            bind(c, ok);
        } else if (i->op == BF_INTERN_STRING) {
            slot(m, i->right, SLJIT_R0, 0);
            sljit_emit_icall(
                c,
                SLJIT_CALL,
                SLJIT_ARGS2(P, P, P),
                SLJIT_MEM1(SLJIT_R0),
                offsetof(ecs_execution_view, intern_string)
            );
        } else {
            const void *fallback = i->type == PIN_STR ? (const void *)i->value.text
                                                      : (const void *)i->value.str;
            if (fallback) {
                struct sljit_jump *present = sljit_emit_cmp(c, SLJIT_NOT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0);
                sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)fallback);
                bind(c, present);
            }
            sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_IMM, 0);
            struct sljit_jump *empty = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0);
            sljit_emit_op1(
                c,
                SLJIT_MOV_P,
                SLJIT_R0,
                0,
                SLJIT_MEM1(m->context),
                offsetof(behavior_function_context, value_storage)
            );
            sljit_emit_icall(
                c,
                SLJIT_CALL,
                SLJIT_ARGS2(P, P, P),
                SLJIT_MEM1(m->context),
                i->type == PIN_STR ? offsetof(behavior_function_context, copy_text)
                                   : offsetof(behavior_function_context, copy_string)
            );
            bind(c, empty);
        }
        sljit_emit_op1(c, SLJIT_MOV_P, dest, offset, SLJIT_R0, 0);
        return true;
    }
    if (i->op == BF_OWN_NAME) {
        slot(m, i->left, SLJIT_R1, 0);
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_IMM, 0);
        struct sljit_jump *empty = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0);
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
        bind(c, empty);
        sljit_emit_op1(c, SLJIT_MOV_P, dest, offset, SLJIT_R0, 0);
        return true;
    }
    if (i->op == BF_STORE_TYPE_NAME) {
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_MEM1(m->variables),
            i->target * sizeof(pin_value) + offsetof(pin_value, record),
            SLJIT_IMM,
            0
        );
        slot(
            m,
            i->left,
            SLJIT_MEM1(m->variables),
            i->target * sizeof(pin_value) + offsetof(pin_value, type_name)
        );
        return true;
    }
    if (i->op == BF_STORE_HEADER) {
        sljit_sw base = i->target * sizeof(pin_value);
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_MEM1(m->variables),
            base + offsetof(pin_value, record),
            SLJIT_IMM,
            0
        );
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
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_MEM1(SLJIT_R3),
            i->target * sizeof(pin_value) + offsetof(pin_value, record),
            SLJIT_IMM,
            0
        );
        slot(m, i->left, SLJIT_MEM1(SLJIT_R3), i->target * sizeof(pin_value) + offsetof(pin_value, type_name));
        return true;
    }
    if (i->op == BF_RESULT_ARRAY) {
        sljit_sw at = i->target * sizeof(pin_value);
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(m->context), offsetof(behavior_function_context, results));
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_MEM1(SLJIT_R3),
            i->target * sizeof(pin_value) + offsetof(pin_value, record),
            SLJIT_IMM,
            0
        );
        sljit_emit_op1(c, SLJIT_MOV32, SLJIT_MEM1(SLJIT_R3), at + offsetof(pin_value, type), SLJIT_IMM, i->type);
        sljit_emit_op1(
            c,
            SLJIT_MOV32,
            SLJIT_MEM1(SLJIT_R3),
            at + offsetof(pin_value, container),
            SLJIT_IMM,
            i->value.container
        );
        slot(m, i->left, SLJIT_MEM1(SLJIT_R3), at + offsetof(pin_value, ptr));
        slot(m, i->right, SLJIT_MEM1(SLJIT_R3), at + offsetof(pin_value, type_name));
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
        if (i->target == BF_OWNER) {
            sljit_emit_op1(
                c,
                SLJIT_MOV_P,
                dest,
                offset,
                SLJIT_MEM1(m->context),
                offsetof(behavior_function_context, owner_type_name)
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
                i->value.type
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
