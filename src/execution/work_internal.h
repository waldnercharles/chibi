#ifndef CHIBI_EXECUTION_WORK_INTERNAL_H
#define CHIBI_EXECUTION_WORK_INTERNAL_H

#include "execution/function.h"
#include "execution/status.h"

#define EXECUTION_WORK_UNSUPPORTED                                             \
    "Request work policy requires canonical execution checkpoints"

/* Refuse a prepaid body that cannot supply canonical checkpoints. */
static inline int behavior_function_reject_work(behavior_function_context *context)
{
    context->error_status = FLOW_ERR_STATE;
    context->failure = BEHAVIOR_FUNCTION_STATE;
    context->error_message = EXECUTION_WORK_UNSUPPORTED;
    context->node_id = 0;
    context->error_frame = 0;
    context->error_source = context->error_stack = context->error_pin = -1;
    context->error_steps = context->steps;
    return FLOW_ERR_STATE;
}

#endif
