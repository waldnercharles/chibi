#include "execution/borrow.h"
#include "execution/intrinsics.h"
#include "execution/jit_internal.h"
#include "support/array.h"

#if CHIBI_HAVE_SJLIT
bool behavior_jit_host_call(
    const behavior_machine *m,
    const behavior_function_instruction *i,
    int destination,
    sljit_sw offset
)
{
    if (i->op != BF_HOST_SLOT && i->op != BF_HOST_CALL && i->op != BF_HOST_SERVICE) {
        return false;
    }
    struct sljit_compiler *c = m->compiler;
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(m->context), offsetof(behavior_function_context, data));
    if (i->op == BF_HOST_SERVICE) {
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_R3), offsetof(behavior_data_storage, calls));
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            destination,
            offset,
            SLJIT_MEM1(SLJIT_R3),
            i->target * sizeof(behavior_data_binding) +
                offsetof(behavior_data_binding, service)
        );
        return true;
    }
    if (i->op == BF_HOST_SLOT) {
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_R3,
            0,
            SLJIT_MEM1(SLJIT_R3),
            i->target ? offsetof(behavior_data_storage, buffers)
                      : offsetof(behavior_data_storage, values)
        );
        if (i->target) {
            sljit_emit_op1(
                c,
                SLJIT_MOV_P,
                destination,
                offset,
                SLJIT_MEM1(SLJIT_R3),
                i->left * sizeof(void *)
            );
        } else {
            sljit_emit_op2(c, SLJIT_ADD, destination, offset, SLJIT_R3, 0, SLJIT_IMM, i->left * sizeof(pin_value));
        }
        return true;
    }
    assert(i->target >= 0 && i->target < acount(m->function->host_calls));
    const behavior_function_host_call *spec = &m->function->host_calls[i->target];
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, m->context, 0);
    sljit_emit_op1(c, SLJIT_MOV32, SLJIT_R1, 0, SLJIT_IMM, i->target);
    sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS2(P, P, 32), SLJIT_IMM, (sljit_sw)behavior_function_host_arguments);
    sljit_emit_op1(
        c,
        SLJIT_MOV_P,
        SLJIT_MEM1(m->context),
        offsetof(behavior_function_context, error_message),
        SLJIT_R0,
        0
    );
    struct sljit_jump *arguments_ok = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
    behavior_jit_failure(m, i, BEHAVIOR_FUNCTION_DATA);
    struct sljit_label *arguments_label = sljit_emit_label(c);
    if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
        sljit_set_label(arguments_ok, arguments_label);
    }
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(m->context), offsetof(behavior_function_context, data));
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_R3), offsetof(behavior_data_storage, values));
    sljit_emit_op2(c, SLJIT_ADD, SLJIT_R2, 0, SLJIT_R1, 0, SLJIT_IMM, spec->result_first * sizeof(pin_value));
    sljit_emit_op2(c, SLJIT_ADD, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, spec->first * sizeof(pin_value));
    if (spec->call->action) {
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, m->context, 0);
        sljit_emit_op1(c, SLJIT_MOV32, SLJIT_R1, 0, SLJIT_IMM, i->target);
        sljit_emit_op1(c, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)i->node_id);
        sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS3(P, P, 32, W), SLJIT_IMM, (sljit_sw)behavior_function_host_action);
    } else if (spec->call->context) {
        sljit_emit_op1(
            c,
            SLJIT_MOV,
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, node_id),
            SLJIT_IMM,
            (sljit_sw)i->node_id
        );
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_R2, 0);
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R2, 0, SLJIT_R1, 0);
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R1, 0, SLJIT_MEM1(m->context), offsetof(behavior_function_context, data));
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_R1), offsetof(behavior_data_storage, calls));
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_R1,
            0,
            SLJIT_MEM1(SLJIT_R1),
            i->target * sizeof(behavior_data_binding) +
                offsetof(behavior_data_binding, service)
        );
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, m->context, 0);
        sljit_emit_icall(
            c,
            SLJIT_CALL,
            SLJIT_ARGS4(P, P, P, P, P),
            SLJIT_IMM,
            (sljit_sw)spec->call->context
        );
    } else if (i->right == BF_HOST_BORROW_ARRAY) {
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, m->context, 0);
        sljit_emit_op1(c, SLJIT_MOV32, SLJIT_R1, 0, SLJIT_IMM, i->target);
        sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS2(P, P, 32), SLJIT_IMM, (sljit_sw)behavior_function_borrow_snapshot);
    } else if (spec->call->value) {
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, m->context, 0);
        sljit_emit_op1(c, SLJIT_MOV32, SLJIT_R1, 0, SLJIT_IMM, i->target);
        sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS2(P, P, 32), SLJIT_IMM, (sljit_sw)behavior_function_value_call);
    } else {
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_R3), offsetof(behavior_data_storage, calls));
        sljit_sw at = i->target * sizeof(behavior_data_binding);
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_R3), at + offsetof(behavior_data_binding, service));
        sljit_emit_icall(
            c,
            SLJIT_CALL,
            SLJIT_ARGS3(P, P, P, P),
            SLJIT_MEM1(SLJIT_R3),
            at + offsetof(behavior_data_binding, invoke)
        );
    }
    sljit_emit_op1(
        c,
        SLJIT_MOV_P,
        SLJIT_MEM1(m->context),
        offsetof(behavior_function_context, error_message),
        SLJIT_R0,
        0
    );
    struct sljit_jump *ok = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
    behavior_jit_failure(m, i, i->left);
    struct sljit_label *label = sljit_emit_label(c);
    if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
        sljit_set_label(ok, label);
    }
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, m->context, 0);
    sljit_emit_op1(c, SLJIT_MOV32, SLJIT_R1, 0, SLJIT_IMM, i->target);
    sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS2(P, P, 32), SLJIT_IMM, (sljit_sw)behavior_function_host_results);
    sljit_emit_op1(
        c,
        SLJIT_MOV_P,
        SLJIT_MEM1(m->context),
        offsetof(behavior_function_context, error_message),
        SLJIT_R0,
        0
    );
    struct sljit_jump *results_ok = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
    behavior_jit_failure(m, i, i->left);
    struct sljit_label *results_label = sljit_emit_label(c);
    if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
        sljit_set_label(results_ok, results_label);
    }
    return true;
}
#endif
