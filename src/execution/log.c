#include "execution/log.h"

#include <assert.h>

const char *execution_print_string(
    const behavior_function_context *context,
    void *service,
    const pin_value *arguments,
    pin_value *
)
{
    if (arguments[0].type != PIN_STR || arguments[0].container != PIN_CONTAINER_SINGLE) {
        return "Print String input is not a string";
    }
    execution_log_service *log = service;
    if (!log) {
        return NULL;
    }
    assert(log->emit);
    const execution_log_message message = {
        .owner = context->owner,
        .node_id = context->node_id,
        .now = context->now,
        .program_name = context->program_name,
        .text = arguments[0].str ? arguments[0].str : "",
    };
    log->emit(log->user, &message);
    return NULL;
}
const node_host_call EXECUTION_PrintString = {
    .service = EXECUTION_LOG_SERVICE,
    .context = execution_print_string,
    .optional_service = true,
};
