#include "ecs/execution.h"
#include "graph/compiler/behavior_lower_internal.h"
#include "runtime/generated.h"
#include "runtime/rhythm.h"
#include "support/array.h"

#include <float.h>

static int number(builder *b, double value)
{
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_F64,
                                         .value = { .type = PIN_F64, .d = value } }
    );
}
static int binary(builder *b, behavior_function_op op, int left, int right)
{
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = op,
                                         .type = op >= BF_LT && op <= BF_NE ? PIN_BOOL : PIN_F64,
                                         .left = left,
                                         .right = right }
    );
}
static void require(builder *b, node *n, int pin, int value, const char *message)
{
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = BF_ASSERT,
                                         .left = value,
                                         .node_id = n->id,
                                         .pin = pin,
                                         .target = BEHAVIOR_FUNCTION_HOST_DATA,
                                         .message = message }
    );
}
static void require_finite(builder *b, node *n, int pin, int value)
{
    int magnitude = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_ABS, .type = PIN_F64, .left = value }
    );
    require(b, n, pin, binary(b, BF_LE, magnitude, number(b, DBL_MAX)), GAME_RHYTHM_INVALID);
}
static int setting(builder *b, int row, int count, size_t offset, float fallback)
{
    int value = behavior_lower_read(b, row, offset, PIN_F32, true);
    value = behavior_lower_convert(b, value, PIN_F64);
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_SELECT,
                                         .type = PIN_F64,
                                         .left = count,
                                         .right = value,
                                         .target = number(b, fallback) }
    );
}
int game_rhythm_lower(builder *b, node *n, int pin)
{
    if (pin < 0 || pin >= 4 || n->input_count || n->output_count != 4) {
        return behavior_lower_unsupported(b, n, pin, true, "Scene Rhythm signature");
    }
    for (int p = 0; p < 4; p++) {
        if (n->output_pins[p].type != PIN_F64 ||
            n->output_pins[p].container != PIN_CONTAINER_SINGLE) {
            return behavior_lower_unsupported(b, n, p, true, "Scene Rhythm output type");
        }
    }
    int world = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_WORLD, .type = BF_POINTER_TYPE }
    );
    require(b, n, pin, world, GAME_RHYTHM_NO_SCENE);
    int clock = behavior_lower_value(
        b,
        (behavior_function_instruction){
            .op = BF_CONTEXT,
            .type = BF_POINTER_TYPE,
            .offset = offsetof(behavior_function_context, instance_time) }
    );
    require(b, n, pin, clock, GAME_RHYTHM_NO_SCENE);
    int time = behavior_lower_read(b, clock, 0, PIN_F64, false);
    int column = behavior_lower_component_column(b, n, pin, world, &GAME_COMPONENT_GameRhythm);
    int count = behavior_lower_read(b, column, offsetof(ecs_execution_column, count), PIN_I32, true);
    int one = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_I32,
                                         .value = { .type = PIN_I32, .i = 1 } }
    );
    require(b, n, pin, binary(b, BF_LE, count, one), GAME_RHYTHM_INVALID);
    int data = behavior_lower_read(b, column, offsetof(ecs_execution_column, data), BF_POINTER_TYPE, true);
    int singleton = behavior_lower_read(b, column, offsetof(ecs_execution_column, singleton), PIN_U32, true);
    int row = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_INDEX,
                                         .type = BF_POINTER_TYPE,
                                         .left = data,
                                         .right = singleton,
                                         .offset = sizeof(GameRhythm) }
    );
    int bpm = setting(b, row, count, offsetof(GameRhythm, bpm), 120);
    int width = setting(b, row, count, offsetof(GameRhythm, pulse_width), .18f);
    require_finite(b, n, pin, time);
    require_finite(b, n, pin, bpm);
    int zero = number(b, 0);
    require(b, n, pin, binary(b, BF_GT, bpm, zero), GAME_RHYTHM_INVALID);
    require_finite(b, n, pin, width);
    require(b, n, pin, binary(b, BF_GT, width, zero), GAME_RHYTHM_INVALID);
    int period = binary(b, BF_DIV, number(b, 60), bpm);
    int phase = binary(b, BF_DIV, time, period);
    int floor = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_FLOOR, .type = PIN_F64, .left = phase }
    );
    phase = binary(b, BF_SUB, phase, floor);
    int duration = binary(b, BF_MIN, width, period);
    int decay = binary(
        b,
        BF_MAX,
        zero,
        binary(b, BF_SUB, number(b, 1), binary(b, BF_DIV, binary(b, BF_MUL, phase, period), duration))
    );
    int values[] = { phase, binary(b, BF_MUL, decay, decay), period, duration };
    for (int p = 0; p < 4; p++) {
        values[p] = behavior_lower_named_constant(b, values[p], "double");
        if (p != pin) {
            apush(b->outputs, ((output){ n, p, values[p] }));
        }
    }
    return values[pin];
}
