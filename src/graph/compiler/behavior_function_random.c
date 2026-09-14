#include "graph/compiler/behavior_lower_internal.h"
#include "graph/nodes/math_nodes.h"

#include <float.h>
#include <stdlib.h>

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
static int select_value(builder *b, int condition, int yes, int no)
{
    return behavior_lower_value(
        b,
        (
            behavior_function_instruction
        ){ .op = BF_SELECT, .type = PIN_F64, .left = condition, .right = yes, .target = no }
    );
}
static void check(builder *b, node *n, int pin, int valid, const char *message)
{
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = BF_ASSERT,
                                         .left = valid,
                                         .target = BEHAVIOR_FUNCTION_DATA,
                                         .node_id = n->id,
                                         .pin = pin,
                                         .message = message }
    );
}
static void require_finite(builder *b, node *n, int value)
{
    int magnitude = operation(b, BF_ABS, value, value);
    int valid = operation(b, BF_LE, magnitude, number(b, DBL_MAX));
    check(b, n, 0, valid, "data function produced an invalid result");
}
static int sample(builder *b)
{
    int value = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_RANDOM, .type = PIN_I32 }
    );
    value = behavior_lower_value(
        b,
        (
            behavior_function_instruction
        ){ .op = BF_CAST, .type = PIN_F64, .left = value, .right = value }
    );
    return operation(b, BF_DIV, value, number(b, (double)RAND_MAX + 1));
}
static int range(builder *b, int min, int max, bool real)
{
    int reversed = operation(b, BF_LT, max, min);
    int low = select_value(b, reversed, max, min);
    int high = select_value(b, reversed, min, max);
#if LDBL_MANT_DIG > DBL_MANT_DIG
    if (real) {
        return operation(b, BF_RANDOM_RANGE, low, high);
    }
#endif
    int fraction = sample(b);
    int complement = operation(b, BF_SUB, number(b, 1), fraction);
    int right = operation(b, BF_MUL, high, fraction);
    int value = behavior_lower_value(
        b,
        (
            behavior_function_instruction
        ){ .op = BF_FMA, .type = PIN_F64, .left = low, .right = complement, .target = right }
    );
    if (real) {
        /* Comparisons preserve equal signed-zero bounds exactly as the oracle. */
        value = select_value(b, operation(b, BF_LT, value, low), low, value);
        value = select_value(b, operation(b, BF_GT, value, high), high, value);
    }
    return value;
}
int behavior_lower_random(builder *b, node *n, int pin)
{
    const node_desc *desc = n->desc;
    if (desc != &NODE_RandomReal && desc != &NODE_RandomRangeReal &&
        desc->jit_kind != NODE_JIT_RANDOM_VECTOR) {
        return -1;
    }
    if (n->input_count != desc->input_count || pin != 0) {
        return behavior_lower_unsupported(b, n, pin, true, "random node signature");
    }
    if (desc == &NODE_RandomReal) {
        return behavior_lower_named_constant(b, sample(b), NULL);
    }
    int first = behavior_lower_input(b, n, 0), last = behavior_lower_input(b, n, 1);
    if (first < 0 || last < 0) {
        return -1;
    }
    if (desc == &NODE_RandomRangeReal) {
        require_finite(b, n, first);
        require_finite(b, n, last);
        return behavior_lower_named_constant(b, range(b, first, last, true), NULL);
    }
    aggregate *min = behavior_lower_aggregate(b, first),
              *max = behavior_lower_aggregate(b, last);
    if (!min || !max || acount(min->fields) != 3 || acount(max->fields) != 3) {
        return -1;
    }
    check(b, n, 0, min->present, "data input has no struct value");
    check(b, n, 1, max->present, "data input has no struct value");
    int low[3], high[3];
    for (int i = 0; i < 3; i++) {
        low[i] = behavior_lower_convert(b, min->fields[i].slot, PIN_F64);
        high[i] = behavior_lower_convert(b, max->fields[i].slot, PIN_F64);
        require_finite(b, n, low[i]);
        require_finite(b, n, high[i]);
    }
    aggregate result = behavior_lower_shape("CF_V3");
    result.present = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_BOOL,
                                         .value = { .type = PIN_BOOL, .b = true } }
    );
    for (int i = 0; i < 3; i++) {
        result.fields[i].slot = behavior_lower_convert(b, range(b, low[i], high[i], false), PIN_F32);
    }
    return behavior_lower_aggregate_value(b, result);
}
