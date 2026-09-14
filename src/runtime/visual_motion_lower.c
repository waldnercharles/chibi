#include "graph/compiler/behavior_lower_internal.h"
#include "runtime/generated.h"
#include "runtime/visual_motion.h"
#include "support/array.h"

#include <float.h>
#include <string.h>

static int number(builder *b, float value)
{
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_F32,
                                         .value = { .type = PIN_F32, .f = value } }
    );
}
static int compare(builder *b, behavior_function_op op, int left, int right)
{
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = op, .type = PIN_BOOL, .left = left, .right = right }
    );
}
static void require(builder *b, node *n, int value, behavior_function_failure reason, const char *message)
{
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = BF_ASSERT,
                                         .left = value,
                                         .node_id = n->id,
                                         .pin = -1,
                                         .target = reason,
                                         .message = message }
    );
}
static void require_finite(builder *b, node *n, int value, const char *message)
{
    int magnitude = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_ABS, .type = PIN_F32, .left = value }
    );
    require(b, n, compare(b, BF_LE, magnitude, number(b, FLT_MAX)), BEHAVIOR_FUNCTION_ARGUMENT, message);
}
static void store_float(builder *b, int row, int value, size_t offset)
{
    behavior_lower_emit(
        b,
        (
            behavior_function_instruction
        ){ .op = BF_WRITE, .type = PIN_F32, .left = value, .right = row, .offset = offset }
    );
}
int game_visual_reaction_lower(builder *b, node *n, int pin)
{
    if (pin != -1 || n->input_count != 4 || n->output_count != 1) {
        return behavior_lower_unsupported(b, n, pin, false, "visual reaction signature");
    }
    int inputs[3];
    for (int i = 1; i < 4; i++) {
        const pin_spec *p = &n->input_pins[i], *declared = &n->desc->inputs[i];
        if (p->type != declared->type || p->container != PIN_CONTAINER_SINGLE ||
            (p->type != PIN_F32 &&
             (!p->type_name || strcmp(p->type_name, declared->type_name)))) {
            return behavior_lower_unsupported(b, n, i, false, "visual reaction input type");
        }
        inputs[i - 1] = behavior_lower_input(b, n, i);
        if (inputs[i - 1] < 0) {
            return -1;
        }
    }
    aggregate *rotation = behavior_lower_aggregate(b, inputs[0]);
    if (!rotation || rotation->type->size != sizeof(CF_V3) ||
        acount(rotation->fields) != 3) {
        return behavior_lower_unsupported(b, n, 1, false, "visual reaction rotation layout");
    }
    aggregate value = *rotation;
    for (int i = 0; i < 3; i++) {
        if (value.fields[i].type != PIN_F32 ||
            value.fields[i].offset != (size_t)i * sizeof(float)) {
            return behavior_lower_unsupported(b, n, 1, false, "visual reaction rotation layout");
        }
    }
    const char
        *input_error = "Trigger Visual Reaction needs finite rotation and lift";
    require(b, n, value.present, BEHAVIOR_FUNCTION_ARGUMENT, input_error);
    require_finite(b, n, inputs[1], input_error);
    int world = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_WORLD, .type = BF_POINTER_TYPE }
    );
    require(b, n, world, BEHAVIOR_FUNCTION_HOST_FLOW, "Trigger Visual Reaction requires an ECS world");
    int row = behavior_lower_component_target(b, n, world, inputs[2], &GAME_COMPONENT_GameVisualMotion);
    require(b, n, row, BEHAVIOR_FUNCTION_STATE, "Trigger Visual Reaction target has no GameVisualMotion");
    const char
        *state_error = "Trigger Visual Reaction needs finite motion state";
    for (int i = 0; i < 3; i++) {
        require_finite(b, n, value.fields[i].slot, state_error);
    }
    int duration = behavior_lower_value(
        b,
        (behavior_function_instruction){
            .op = BF_LOAD,
            .type = PIN_F32,
            .left = row,
            .offset = offsetof(GameVisualMotion, reaction_duration) }
    );
    require_finite(b, n, duration, state_error);
    int zero = number(b, 0);
    require(b, n, compare(b, BF_GE, duration, zero), BEHAVIOR_FUNCTION_ARGUMENT, state_error);
    apush(b->function->component_writes, GAME_COMPONENT_GameVisualMotion.id);
    for (int i = 0; i < 3; i++) {
        store_float(
            b,
            row,
            value.fields[i].slot,
            offsetof(GameVisualMotion, reaction_rotation) + value.fields[i].offset
        );
    }
    store_float(b, row, inputs[1], offsetof(GameVisualMotion, reaction_lift));
    store_float(b, row, zero, offsetof(GameVisualMotion, reaction_age));
    return 0;
}
