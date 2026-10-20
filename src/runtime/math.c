#include "execution/random.h"
#include "execution/scalar.h"
#include "runtime/math_native.h"
#include "support/cos_sin.h"
#include "types/array.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

#define UNARY(KEY, NAME, SOURCE, FN, DESCRIPTION)                              \
    pin_value native_math_##KEY(const pin_value *inputs, int count, arena *)   \
    {                                                                          \
        assert(count == 1 && inputs[0].type == PIN_F64);                       \
        (void)count;                                                           \
        return (pin_value){ .type = PIN_F64, .d = FN(inputs[0].d) };           \
    }
NATIVE_MATH_UNARIES(UNARY)
#undef UNARY
#define UNARY(KEY, NAME, SOURCE, FN, DESCRIPTION)                                \
    pin_value native_math_F32_##KEY(const pin_value *inputs, int count, arena *) \
    {                                                                            \
        assert(count == 1 && inputs[0].type == PIN_F32);                         \
        (void)count;                                                             \
        return (pin_value){ .type = PIN_F32, .f = FN##f(inputs[0].f) };          \
    }
NATIVE_MATH_UNARIES(UNARY)
#undef UNARY
#define BINARY(KEY, NAME, SOURCE, FN, INPUTS, KEYWORDS, DESCRIPTION)                  \
    pin_value native_math_##KEY(const pin_value *inputs, int count, arena *)          \
    {                                                                                 \
        assert(count == 2 && inputs[0].type == PIN_F64 && inputs[1].type == PIN_F64); \
        (void)count;                                                                  \
        return (pin_value){ .type = PIN_F64, .d = FN(inputs[0].d, inputs[1].d) };     \
    }
NATIVE_MATH_BINARIES(BINARY)
#undef BINARY
#define BINARY(KEY, NAME, SOURCE, FN, INPUTS, KEYWORDS, DESCRIPTION)                  \
    pin_value native_math_F32_##KEY(const pin_value *inputs, int count, arena *)      \
    {                                                                                 \
        assert(count == 2 && inputs[0].type == PIN_F32 && inputs[1].type == PIN_F32); \
        (void)count;                                                                  \
        return (pin_value){ .type = PIN_F32, .f = FN##f(inputs[0].f, inputs[1].f) };  \
    }
NATIVE_MATH_BINARIES(BINARY)
#undef BINARY

pin_value native_math_F32_Step(const pin_value *inputs, int count, arena *)
{
    assert(count == 2 && inputs[0].type == PIN_F32 && inputs[1].type == PIN_F32);
    (void)count;
    return (pin_value){ .type = PIN_F32, .f = inputs[0].f < inputs[1].f ? 0 : 1 };
}
pin_value native_math_F32_Mix(const pin_value *inputs, int count, arena *)
{
    assert(
        count == 3 && inputs[0].type == PIN_F32 && inputs[1].type == PIN_F32 &&
        inputs[2].type == PIN_F32
    );
    (void)count;
    float span = inputs[1].f - inputs[0].f;
    float offset = span * inputs[2].f;
    return (pin_value){ .type = PIN_F32, .f = inputs[0].f + offset };
}
pin_value native_math_F32_Quantize_Angle(const pin_value *inputs, int count, arena *)
{
    assert(count == 2 && inputs[0].type == PIN_F32 && inputs[1].type == PIN_F32);
    (void)count;
    float angle = inputs[0].f, step = inputs[1].f;
    if (!isfinite(angle) || !isfinite(step) || step == 0) {
        return (pin_value){ .type = PIN_FLOW };
    }
    float result = roundf(angle / step) * step;
    return isfinite(result) ? (pin_value){ .type = PIN_F32, .f = result }
                            : (pin_value){ .type = PIN_FLOW };
}
pin_value native_math_F32_Smooth_Step(const pin_value *inputs, int count, arena *)
{
    assert(count == 1 && inputs[0].type == PIN_F32);
    (void)count;
    float value = inputs[0].f;
    if (!isfinite(value)) {
        return (pin_value){ .type = PIN_FLOW };
    }
    value = fminf(fmaxf(value, 0), 1);
    return (pin_value){ .type = PIN_F32, .f = value * value * (3 - 2 * value) };
}

pin_value native_math_Step(const pin_value *inputs, int count, arena *)
{
    assert(count == 2 && inputs[0].type == PIN_F64 && inputs[1].type == PIN_F64);
    (void)count;
    return (pin_value){ .type = PIN_F64, .d = inputs[0].d < inputs[1].d ? 0 : 1 };
}

pin_value native_math_Mix(const pin_value *inputs, int count, arena *)
{
    assert(
        count == 3 && inputs[0].type == PIN_F64 && inputs[1].type == PIN_F64 &&
        inputs[2].type == PIN_F64
    );
    (void)count;
    /* Separate steps, as every backend computes it, never a fused multiply-add. */
    double span = inputs[1].d - inputs[0].d;
    double offset = span * inputs[2].d;
    return (pin_value){ .type = PIN_F64, .d = inputs[0].d + offset };
}

pin_value native_math_RandomReal(const pin_value *, int count, arena *)
{
    assert(count == 0);
    (void)count;
    return (pin_value){ .type = PIN_F64, .d = (double)rand() / ((double)RAND_MAX + 1) };
}

pin_value native_math_RandomRangeReal(const pin_value *inputs, int count, arena *)
{
    assert(count == 2 && inputs[0].type == PIN_F64 && inputs[1].type == PIN_F64);
    (void)count;
    double min = inputs[0].d;
    double max = inputs[1].d;
    if (!isfinite(min) || !isfinite(max)) {
        return (pin_value){ .type = PIN_FLOW };
    }
    if (max < min) {
        double swap = min;
        min = max;
        max = swap;
    }
    return (pin_value){
        .type = PIN_F64,
        .d = behavior_random_range(min, max),
    };
}

pin_value native_math_Quantize_Angle(const pin_value *inputs, int count, arena *)
{
    assert(count == 2 && inputs[0].type == PIN_F64 && inputs[1].type == PIN_F64);
    (void)count;
    double angle = inputs[0].d;
    double step = inputs[1].d;
    if (!isfinite(angle) || !isfinite(step) || step == 0) {
        return (pin_value){ .type = PIN_FLOW };
    }
    double result = round(angle / step) * step;
    return isfinite(result)
             ? (pin_value){ .type = PIN_F64, .type_name = "double", .d = result }
             : (pin_value){ .type = PIN_FLOW };
}

pin_value native_math_Smooth_Step(const pin_value *inputs, int count, arena *)
{
    assert(count == 1 && inputs[0].type == PIN_F64);
    (void)count;
    double value = inputs[0].d;
    if (!isfinite(value)) {
        return (pin_value){ .type = PIN_FLOW };
    }
    value = fmin(fmax(value, 0), 1);
    return (pin_value){ .type = PIN_F64,
                        .type_name = "double",
                        .d = value * value * (3 - 2 * value) };
}

const char *native_math_spring(const pin_value *inputs, pin_value *out)
{
    assert(
        inputs[0].type == PIN_F32 && inputs[1].type == PIN_F32 &&
        inputs[2].type == PIN_F32 && inputs[3].type == PIN_F32 &&
        inputs[4].type == PIN_F32 && inputs[5].type == PIN_F32
    );
    double position = inputs[0].f;
    double velocity = inputs[1].f;
    double target = inputs[2].f;
    double frequency = inputs[3].f;
    double damping = inputs[4].f;
    double delta = inputs[5].f;
    if (!isfinite(position) || !isfinite(velocity) || !isfinite(target) ||
        !isfinite(frequency) || !isfinite(damping) || !isfinite(delta)) {
        return "Advance Damped Spring needs finite inputs";
    }
    delta = fmax(delta, 0);
    frequency = fmax(frequency, .01);
    damping = fmax(damping, 0);
    double omega = frequency * 6.2831853071795864769;
    double omega_squared = omega * omega;
    double damping_term = 2 * delta * damping * omega;
    double delta_term = delta * delta * omega_squared;
    double denominator = 1 + damping_term + delta_term;
    double inverse = 1 / denominator;
    double next_position = ((1 + damping_term) * position + delta * velocity +
                            delta_term * target) *
                           inverse;
    double next_velocity = (velocity + delta * omega_squared * (target - position)) *
                           inverse;
    float next_position_f = (float)next_position;
    float next_velocity_f = (float)next_velocity;
    if (!isfinite(next_position_f) || !isfinite(next_velocity_f)) {
        return "Advance Damped Spring produced nonfinite output";
    }
    out[0] = (pin_value){ .type = PIN_F32, .type_name = "float", .f = next_position_f };
    out[1] = (pin_value){ .type = PIN_F32, .type_name = "float", .f = next_velocity_f };
    return NULL;
}

#define ADAPTER(KEY)                                                                              \
    static const char *KEY##_call(arena *storage, const pin_value *in, int count, pin_value *out) \
    {                                                                                             \
        pin_value value = native_math_##KEY(in, count, storage);                                  \
        if (value.type == PIN_FLOW) return "Native value operation failed";                       \
        return pin_value_snapshot_to_arena(storage, out, &value)                                  \
                 ? "Cannot retain native value result"                                            \
                 : NULL;                                                                          \
    }                                                                                             \
    const node_host_call NATIVE_MATH_##KEY##_CALL = { .value = KEY##_call };
#define UNARY(KEY, NAME, SOURCE, FN, DESCRIPTION)                              \
    ADAPTER(KEY) ADAPTER(F32_##KEY)
#define BINARY(KEY, NAME, SOURCE, FN, INPUTS, KEYWORDS, DESCRIPTION)           \
    ADAPTER(KEY) ADAPTER(F32_##KEY)
#define VALUE(KEY, NAME, SOURCE, INPUTS, RANDOM) ADAPTER(KEY)
NATIVE_MATH_UNARIES(UNARY)
NATIVE_MATH_BINARIES(BINARY)
NATIVE_MATH_VALUES(VALUE)
#define FLOAT_VALUE(KEY, NAME, SOURCE, INPUTS) ADAPTER(F32_##KEY)
NATIVE_MATH_F32_VALUES(FLOAT_VALUE)
#undef FLOAT_VALUE
#undef UNARY
#undef BINARY
#undef VALUE
#undef ADAPTER
static const char *spring_call(void *, const pin_value *in, pin_value *out)
{
    pin_value values[2];
    const char *error = native_math_spring(in, values);
    if (!error) {
        out[0].f = values[0].f;
        out[1].f = values[1].f;
    }
    return error;
}
const node_host_call NATIVE_MATH_Advance_Damped_Spring_CALL = { .invoke = spring_call };
const node_access NATIVE_MATH_ACCESS = { .flags = NODE_EFFECT_PURE };
const node_access NATIVE_MATH_RANDOM_ACCESS = { .flags = NODE_EFFECT_HOST_SINK |
                                                         NODE_EFFECT_RANDOM };
