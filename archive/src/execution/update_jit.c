#include "execution/update_jit.h"

#include <assert.h>
#include <stddef.h>

#if CHIBI_HAVE_SJLIT
#include <sljitLir.h>

/* S0 row, S1 end, S2 world, S3 result, S4 live state, S5 context. All survive
 * body calls. Each guard exits before effects, so the ordinary scheduler can
 * handle observation, active waits, service edits or a different backend. */
static void copy_state(struct sljit_compiler *c, int op, size_t source, size_t destination)
{
    sljit_emit_op1(c, op, SLJIT_MEM1(SLJIT_S5), (sljit_sw)destination, SLJIT_MEM1(SLJIT_S4), (sljit_sw)source);
}

int behavior_update_emit_guard(struct sljit_compiler *c, struct sljit_jump **stop)
{
    int n = 0;
    stop[n++] = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_S0, 0, SLJIT_S1, 0);
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_S5, 0, SLJIT_MEM1(SLJIT_S0), offsetof(execution_update_row, binding));
    stop[n++] = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_S5, 0, SLJIT_IMM, 0);
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_S4, 0, SLJIT_MEM1(SLJIT_S0), offsetof(execution_update_row, state));
    stop[n++] = sljit_emit_cmp(
        c,
        SLJIT_EQUAL | SLJIT_32,
        SLJIT_MEM1(SLJIT_S4),
        offsetof(execution_update_state, alive),
        SLJIT_IMM,
        0
    );
    stop[n++] = sljit_emit_cmp(
        c,
        SLJIT_NOT_EQUAL | SLJIT_32,
        SLJIT_MEM1(SLJIT_S4),
        offsetof(execution_update_state, trace_enabled),
        SLJIT_IMM,
        0
    );
    stop[n++] = sljit_emit_cmp(
        c,
        SLJIT_NOT_EQUAL | SLJIT_32,
        SLJIT_MEM1(SLJIT_S4),
        offsetof(execution_update_state, backend),
        SLJIT_IMM,
        BEHAVIOR_SLJIT
    );
    stop[n++] = sljit_emit_cmp(
        c,
        SLJIT_NOT_EQUAL,
        SLJIT_MEM1(SLJIT_S4),
        offsetof(execution_update_state, profile),
        SLJIT_IMM,
        0
    );
    stop[n++] = sljit_emit_cmp(
        c,
        SLJIT_NOT_EQUAL,
        SLJIT_MEM1(SLJIT_S4),
        offsetof(execution_update_state, generation),
        SLJIT_MEM1(SLJIT_S5),
        offsetof(execution_update_binding, generation)
    );
    stop[n++] = sljit_emit_cmp(
        c,
        SLJIT_NOT_EQUAL,
        SLJIT_MEM1(SLJIT_S4),
        offsetof(execution_update_state, services_revision),
        SLJIT_MEM1(SLJIT_S0),
        offsetof(execution_update_row, services_revision)
    );
    stop[n++] = sljit_emit_cmp(
        c,
        SLJIT_NOT_EQUAL | SLJIT_32,
        SLJIT_MEM1(SLJIT_S4),
        offsetof(execution_update_state, pending),
        SLJIT_IMM,
        0
    );

    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), offsetof(execution_update_row, entity));
    sljit_emit_op2(c, SLJIT_LSHR, SLJIT_R1, 0, SLJIT_R0, 0, SLJIT_IMM, 32);
    sljit_emit_op1(c, SLJIT_MOV_U32, SLJIT_R0, 0, SLJIT_R0, 0);
    stop[n++] = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
    stop[n++] = sljit_emit_cmp(
        c,
        SLJIT_GREATER_EQUAL | SLJIT_32,
        SLJIT_R0,
        0,
        SLJIT_MEM1(SLJIT_S2),
        offsetof(ecs_execution_view, slot_count)
    );
    stop[n++] = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0);
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_S2), offsetof(ecs_execution_view, occupied));
    sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R2, 0, SLJIT_MEM2(SLJIT_R2, SLJIT_R0), 0);
    stop[n++] = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R2, 0, SLJIT_IMM, 0);
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_S2), offsetof(ecs_execution_view, generations));
    stop[n++] = sljit_emit_cmp(c, SLJIT_NOT_EQUAL | SLJIT_32, SLJIT_R1, 0, SLJIT_MEM2(SLJIT_R2, SLJIT_R0), 2);

    return n;
}

void behavior_update_emit_bind(struct sljit_compiler *c)
{
    static_assert(offsetof(execution_update_binding, context) == 0);
    copy_state(
        c,
        SLJIT_MOV_P,
        offsetof(execution_update_state, variables),
        offsetof(behavior_function_context, variables)
    );
    copy_state(
        c,
        SLJIT_MOV_P,
        offsetof(execution_update_state, node_states),
        offsetof(behavior_function_context, states)
    );
    copy_state(
        c,
        SLJIT_MOV_P,
        offsetof(execution_update_state, variable_storage),
        offsetof(behavior_function_context, variable_storage)
    );
    copy_state(
        c,
        SLJIT_MOV,
        offsetof(execution_update_state, owner) + offsetof(pin_value, u64),
        offsetof(behavior_function_context, owner)
    );
    copy_state(
        c,
        SLJIT_MOV_P,
        offsetof(execution_update_state, owner) + offsetof(pin_value, type_name),
        offsetof(behavior_function_context, owner_type_name)
    );
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S4), offsetof(execution_update_state, clock));
    sljit_emit_fop1(
        c,
        SLJIT_MOV_F64,
        SLJIT_MEM1(SLJIT_S5),
        offsetof(behavior_function_context, delta_seconds),
        SLJIT_MEM1(SLJIT_R0),
        offsetof(behavior_clock, delta_seconds)
    );
    sljit_emit_op1(
        c,
        SLJIT_MOV_P,
        SLJIT_MEM1(SLJIT_S5),
        offsetof(behavior_function_context, value_owner),
        SLJIT_MEM1(SLJIT_S4),
        offsetof(execution_update_state, value_owner)
    );
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(SLJIT_S5), offsetof(behavior_function_context, steps), SLJIT_IMM, 0);
    sljit_emit_op2(
        c,
        SLJIT_ADD,
        SLJIT_MEM1(SLJIT_S5),
        offsetof(behavior_function_context, instance_time),
        SLJIT_R0,
        0,
        SLJIT_IMM,
        offsetof(behavior_clock, current_time)
    );
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S5), offsetof(behavior_function_context, value_storage));
    struct sljit_jump *no_storage = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
    sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS1V(P), SLJIT_IMM, (sljit_sw)arena_reset);
    sljit_set_label(no_storage, sljit_emit_label(c));
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S5, 0);
}

void *behavior_update_compile()
{
    struct sljit_compiler *c = sljit_create_compiler(NULL);
    if (!c) {
        return NULL;
    }
    sljit_emit_enter(c, 0, SLJIT_ARGS4(P, P, P, P, P), 4 | SLJIT_ENTER_FLOAT(1), 6, 0);
    sljit_emit_op1(c, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S3), 0, SLJIT_IMM, 0);
    struct sljit_label *loop = sljit_emit_label(c);
    struct sljit_jump *stop[20];
    int n = 0;
    n += behavior_update_emit_guard(c, stop + n);
    behavior_update_emit_bind(c);
    sljit_emit_icall(
        c,
        SLJIT_CALL,
        SLJIT_ARGS1(32, P),
        SLJIT_MEM1(SLJIT_S5),
        offsetof(execution_update_binding, code)
    );
    struct sljit_jump *failed = sljit_emit_cmp(c, SLJIT_NOT_EQUAL | SLJIT_32, SLJIT_R0, 0, SLJIT_IMM, 0);
    sljit_emit_op2(c, SLJIT_ADD, SLJIT_S0, 0, SLJIT_S0, 0, SLJIT_IMM, sizeof(execution_update_row));
    struct sljit_jump *again = sljit_emit_jump(c, SLJIT_JUMP);
    struct sljit_label *failure = sljit_emit_label(c);
    sljit_emit_op1(c, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S3), 0, SLJIT_R0, 0);
    struct sljit_label *done = sljit_emit_label(c);
    sljit_emit_return(c, SLJIT_MOV_P, SLJIT_S0, 0);
    void *code = NULL;
    assert(n <= (int)(sizeof stop / sizeof *stop));
    if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
        sljit_set_label(failed, failure);
        sljit_set_label(again, loop);
        for (int i = 0; i < n; i++) {
            sljit_set_label(stop[i], done);
        }
        code = sljit_generate_code(c, 0, NULL);
    }
    sljit_free_compiler(c);
    return code;
}

void behavior_update_free_code(void *code)
{
    if (code) {
        sljit_free_code(code, NULL);
    }
}
#else
void *behavior_update_compile()
{
    return NULL;
}
void behavior_update_free_code(void *) {}
#endif
