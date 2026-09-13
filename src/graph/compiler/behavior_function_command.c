#include "graph/compiler/behavior_lower_internal.h"
#include "support/array.h"

static bool argument(builder *b, int value, int target, bool result)
{
    scalar_value *scalar = behavior_lower_scalar_value(b, value);
    if (!scalar) {
        return false;
    }
    int name = scalar->name;
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = result ? BF_COMMAND_OUTPUT : BF_COMMAND_ARGUMENT,
                                         .type = b->function->slots[value],
                                         .left = value,
                                         .target = target,
                                         .offset = offsetof(pin_value, d) }
    );
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = result ? BF_COMMAND_OUTPUT : BF_COMMAND_ARGUMENT,
                                         .type = BF_POINTER_TYPE,
                                         .left = name,
                                         .target = target,
                                         .offset = offsetof(pin_value, type_name) }
    );
    return true;
}

static bool results(builder *b, node *n, int result_first)
{
    for (int pin = 0; pin < n->output_count; pin++) {
        const pin_spec *spec = &n->output_pins[pin];
        if (spec->kind == PIN_EXEC) {
            continue;
        }
        int value = behavior_lower_value(
            b,
            (behavior_function_instruction){ .op = BF_COMMAND_RESULT,
                                             .type = spec->type,
                                             .left = result_first + pin,
                                             .offset = offsetof(pin_value, d) }
        );
        int name = behavior_lower_value(
            b,
            (behavior_function_instruction){ .op = BF_COMMAND_RESULT,
                                             .type = BF_POINTER_TYPE,
                                             .left = result_first + pin,
                                             .offset = offsetof(pin_value, type_name) }
        );
        value = behavior_lower_named_scalar(b, value, name, false);
        int local = behavior_lower_local(b, b->frame, n, pin);
        if (!behavior_lower_store(b, BF_SAVE, value, local)) {
            return false;
        }
    }
    return true;
}

int behavior_lower_command(builder *b, node *n, int after)
{
    const node_command *command = n->desc->command;
    assert((command->invoke != NULL) != (command->begin != NULL));
    bool async = command->begin != NULL;
    int index = acount(b->function->commands),
        first = acount(b->function->command_arguments),
        result_first = acount(b->function->command_results);
    apush(
        b->function->commands,
        ((behavior_function_command){ .command = command,
                                      .reference = n->reference_name,
                                      .first = first,
                                      .count = n->input_count - 1,
                                      .node = n,
                                      .frame = b->frame,
                                      .result_first = result_first })
    );
    int service = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_COMMAND_SERVICE,
                                         .type = BF_POINTER_TYPE,
                                         .target = index }
    );
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = BF_ASSERT,
                                         .left = service,
                                         .target = BEHAVIOR_FUNCTION_HOST_FLOW,
                                         .node_id = n->id,
                                         .pin = -1,
                                         .message = command->missing_service }
    );
    if (command->missing_reference && (!n->reference_name || !n->reference_name[0])) {
        behavior_lower_emit(
            b,
            (behavior_function_instruction){ .op = BF_FAIL,
                                             .left = BEHAVIOR_FUNCTION_HOST_FLOW,
                                             .node_id = n->id,
                                             .pin = -1,
                                             .message = command->missing_reference }
        );
        return behavior_lower_successor(b, n, 0, after);
    }
    for (int i = 1; i < n->input_count; i++) {
        const pin_spec *spec = &n->input_pins[i];
        if ((!behavior_lower_scalar(spec->type) && !behavior_scalar_string(spec->type)) ||
            spec->container != PIN_CONTAINER_SINGLE || spec->kind != PIN_DATA) {
            behavior_lower_unsupported(b, n, i, false, "command payload ownership");
            return -2;
        }
        int value = behavior_lower_input(b, n, i);
        if (value < 0) {
            return -2;
        }
        int target = acount(b->function->command_arguments);
        apush(
            b->function->command_arguments,
            ((pin_value){ .type = spec->type, .type_name = spec->type_name })
        );
        if (!argument(b, value, target, false)) {
            return -2;
        }
    }
    for (int pin = 0; async && pin < n->output_count; pin++) {
        const pin_spec *spec = &n->output_pins[pin];
        apush(b->function->command_results, spec->default_value);
        if (spec->kind == PIN_EXEC) {
            continue;
        }
        if (!behavior_lower_scalar(spec->type) ||
            spec->container != PIN_CONTAINER_SINGLE) {
            behavior_lower_unsupported(b, n, pin, true, "asynchronous result ownership");
            return -2;
        }
        int local = behavior_lower_local(b, b->frame, n, pin);
        if (local < 0) {
            return -2;
        }
        int value = behavior_lower_source(b, BF_LOCAL, local, spec, n, pin);
        if (value < 0 || !argument(b, value, result_first + pin, true)) {
            return -2;
        }
    }
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = async ? BF_COMMAND_BEGIN : BF_COMMAND,
                                         .target = index,
                                         .node_id = n->id,
                                         .pin = -1,
                                         .message = command->failure_message }
    );
    int next = behavior_lower_successor(b, n, 0, after);
    if (!async || next < -1) {
        return next;
    }
    b->function->waits = true;
    if (!results(b, n, result_first)) {
        return -2;
    }
    behavior_lower_capture(b, n, false);
    int stack = b->stack;
    b->stack = next >= 0 ? b->tasks[next].stack : stack;
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = BF_SUSPEND, .node_id = n->id, .pin = -1 }
    );
    b->stack = stack;
    return results(b, n, result_first) ? next : -2;
}
