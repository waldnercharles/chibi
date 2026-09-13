#include "graph/compiler/behavior_lower_internal.h"
#include "graph/nodes/math_nodes.h"
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
static int operation(builder *b, behavior_function_op op, int left, int right)
{
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = op,
                                         .type = op >= BF_LT && op <= BF_NE ? PIN_BOOL : PIN_F64,
                                         .left = left,
                                         .right = right }
    );
}
static void require(builder *b, node *n, int condition, const char *message)
{
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = BF_ASSERT,
                                         .left = condition,
                                         .target = BEHAVIOR_FUNCTION_DATA,
                                         .node_id = n->id,
                                         .pin = 0,
                                         .message = message }
    );
}
static void require_finite(builder *b, node *n, int value, const char *message)
{
    int magnitude = operation(b, BF_ABS, value, value);
    require(b, n, operation(b, BF_LE, magnitude, number(b, DBL_MAX)), message);
}
static int spring(builder *b, node *n, int pin, const int *inputs)
{
    int a[6];
    for (int i = 0; i < 6; i++) {
        a[i] = behavior_lower_convert(b, inputs[i], PIN_F64);
        require_finite(b, n, a[i], "Advance Damped Spring needs finite inputs");
    }
    int zero = number(b, 0), one = number(b, 1), two = number(b, 2);
    int delta = operation(b, BF_MAX, a[5], zero);
    int frequency = operation(b, BF_MAX, a[3], number(b, .01));
    int damping = operation(b, BF_MAX, a[4], zero);
    int omega = operation(b, BF_MUL, frequency, number(b, 6.2831853071795864769));
    int omega_squared = operation(b, BF_MUL, omega, omega);
    int damping_term = operation(b, BF_MUL, two, delta);
    damping_term = operation(b, BF_MUL, damping_term, damping);
    damping_term = operation(b, BF_MUL, damping_term, omega);
    int delta_term = operation(b, BF_MUL, delta, delta);
    delta_term = operation(b, BF_MUL, delta_term, omega_squared);
    int damped = operation(b, BF_ADD, one, damping_term);
    int denominator = operation(b, BF_ADD, damped, delta_term);
    int inverse = operation(b, BF_DIV, one, denominator);
    int position = operation(b, BF_MUL, damped, a[0]);
    position = operation(b, BF_ADD, position, operation(b, BF_MUL, delta, a[1]));
    position = operation(b, BF_ADD, position, operation(b, BF_MUL, delta_term, a[2]));
    position = operation(b, BF_MUL, position, inverse);
    int velocity = operation(b, BF_MUL, delta, omega_squared);
    velocity = operation(b, BF_MUL, velocity, operation(b, BF_SUB, a[2], a[0]));
    velocity = operation(b, BF_ADD, a[1], velocity);
    velocity = operation(b, BF_MUL, velocity, inverse);
    int results[] = { behavior_lower_convert(b, position, PIN_F32),
                      behavior_lower_convert(b, velocity, PIN_F32) };
    for (int i = 0; i < 2; i++) {
        require_finite(b, n, behavior_lower_convert(b, results[i], PIN_F64), "Advance Damped Spring produced nonfinite output");
        results[i] = behavior_lower_named_constant(b, results[i], "float");
        if (i != pin) {
            apush(b->outputs, ((output){ n, i, results[i] }));
        }
    }
    return results[pin];
}

int behavior_lower_motion(builder *b, node *n, int pin)
{
    const node_desc *desc = n->desc;
    if (desc != &NODE_Advance_Damped_Spring && desc != &NODE_Quantize_Angle &&
        desc != &NODE_Smooth_Step) {
        return -1;
    }
    if (n->input_count != desc->input_count || n->output_count != desc->output_count) {
        return behavior_lower_unsupported(b, n, pin, true, "motion node signature");
    }
    int inputs[6];
    for (int i = 0; i < n->input_count; i++) {
        if (n->input_pins[i].type != desc->inputs[i].type ||
            n->input_pins[i].container != PIN_CONTAINER_SINGLE) {
            return behavior_lower_unsupported(b, n, i, false, "motion input type");
        }
        inputs[i] = behavior_lower_input(b, n, i);
        if (inputs[i] < 0) {
            return -1;
        }
    }
    if (desc == &NODE_Advance_Damped_Spring) {
        return spring(b, n, pin, inputs);
    }
    const char *message = "data function produced an invalid result";
    int value = inputs[0], zero = number(b, 0);
    require_finite(b, n, value, message);
    if (desc == &NODE_Smooth_Step) {
        value = operation(b, BF_MAX, value, zero);
        value = operation(b, BF_MIN, value, number(b, 1));
        int square = operation(b, BF_MUL, value, value);
        int twice = operation(b, BF_MUL, number(b, 2), value);
        value = operation(b, BF_MUL, square, operation(b, BF_SUB, number(b, 3), twice));
    } else {
        int step = inputs[1];
        require_finite(b, n, step, message);
        require(b, n, operation(b, BF_NE, step, zero), message);
        value = operation(b, BF_DIV, value, step);
        value = operation(b, BF_ROUND, value, value);
        value = operation(b, BF_MUL, value, step);
        require_finite(b, n, value, message);
    }
    return behavior_lower_named_constant(b, value, "double");
}
