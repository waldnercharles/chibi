#include "graph/compiler/behavior_lower_internal.h"
#include "support/array.h"

bool behavior_lower_command(builder *b, node *n)
{
    const node_command *command = n->desc->command;
    int index = acount(b->function->commands),
        first = acount(b->function->command_arguments);
    apush(
        b->function->commands,
        ((behavior_function_command){ command, n->reference_name, first, n->input_count - 1 })
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
        return true;
    }
    for (int i = 1; i < n->input_count; i++) {
        const pin_spec *spec = &n->input_pins[i];
        if (!behavior_lower_scalar(spec->type) ||
            spec->container != PIN_CONTAINER_SINGLE || spec->kind != PIN_DATA) {
            behavior_lower_unsupported(b, n, i, false, "command payload ownership");
            return false;
        }
        int value = behavior_lower_input(b, n, i);
        if (value < 0) {
            return false;
        }
        scalar_value *scalar = behavior_lower_scalar_value(b, value);
        if (!scalar) {
            behavior_lower_unsupported(b, n, i, false, "command payload metadata");
            return false;
        }
        int name = scalar->name;
        int argument = acount(b->function->command_arguments);
        apush(
            b->function->command_arguments,
            ((pin_value){ .type = spec->type, .type_name = spec->type_name })
        );
        behavior_lower_emit(
            b,
            (behavior_function_instruction){ .op = BF_COMMAND_ARGUMENT,
                                             .type = spec->type,
                                             .left = value,
                                             .target = argument,
                                             .offset = offsetof(pin_value, d) }
        );
        behavior_lower_emit(
            b,
            (behavior_function_instruction){ .op = BF_COMMAND_ARGUMENT,
                                             .type = BF_POINTER_TYPE,
                                             .left = name,
                                             .target = argument,
                                             .offset = offsetof(pin_value, type_name) }
        );
    }
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = BF_COMMAND,
                                         .target = index,
                                         .node_id = n->id,
                                         .pin = -1,
                                         .message = command->failure_message }
    );
    return true;
}
