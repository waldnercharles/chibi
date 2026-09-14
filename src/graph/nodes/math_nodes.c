#include "graph/nodes/math_nodes.h"

#include "graph/runtime/random.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

static const node_access pure = { .flags = NODE_EFFECT_PURE };
static const node_access random_access = { .flags = NODE_EFFECT_HOST_SINK };
static pin_spec unary[] = { { .name = "Value", .type = PIN_F64, .type_name = "double" } };
static pin_spec binary[] = { { .name = "A", .type = PIN_F64, .type_name = "double" },
                             { .name = "B", .type = PIN_F64, .type_name = "double" } };
static pin_spec result[] = { { .name = "Result", .type = PIN_F64, .type_name = "double" } };

#define UNARY(NAME, LABEL, FN)                                                 \
    static pin_value NAME##_value(const pin_value *inputs, int count, arena *) \
    {                                                                          \
        assert(count == 1 && inputs[0].type == PIN_F64);                       \
        return (pin_value){ .type = PIN_F64, .d = FN(inputs[0].d) };           \
    }                                                                          \
    node_desc NODE_##NAME = { .name = LABEL,                                   \
                              .inputs = unary,                                 \
                              .input_count = 1,                                \
                              .outputs = result,                               \
                              .output_count = 1,                               \
                              .pure_kind = NODE_PURE_CALL,                     \
                              .fn = NAME##_value,                              \
                              .access = &pure,                                 \
                              .category = "Math/Functions",                    \
                              .foldable = true };
UNARY(Sine, "Sine", sin)
UNARY(Cosine, "Cosine", cos)
UNARY(Tangent, "Tangent", tan)
UNARY(ArcSine, "Arc Sine", asin)
UNARY(ArcCosine, "Arc Cosine", acos)
UNARY(ArcTangent, "Arc Tangent", atan)
UNARY(SquareRoot, "Square Root", sqrt)
UNARY(Exponential, "Exponential", exp)
UNARY(Logarithm, "Logarithm", log)
UNARY(Logarithm10, "Logarithm 10", log10)
UNARY(Floor, "Floor", floor)
UNARY(Ceiling, "Ceiling", ceil)
UNARY(Round, "Round", round)
#undef UNARY

#define BINARY(NAME, LABEL, FN)                                                       \
    static pin_value NAME##_value(const pin_value *inputs, int count, arena *)        \
    {                                                                                 \
        assert(count == 2 && inputs[0].type == PIN_F64 && inputs[1].type == PIN_F64); \
        return (pin_value){ .type = PIN_F64, .d = FN(inputs[0].d, inputs[1].d) };     \
    }                                                                                 \
    node_desc NODE_##NAME = { .name = LABEL,                                          \
                              .inputs = binary,                                       \
                              .input_count = 2,                                       \
                              .outputs = result,                                      \
                              .output_count = 1,                                      \
                              .pure_kind = NODE_PURE_CALL,                            \
                              .fn = NAME##_value,                                     \
                              .access = &pure,                                        \
                              .category = "Math/Functions",                           \
                              .foldable = true };
BINARY(Power, "Power", pow)
BINARY(ArcTangent2, "Arc Tangent 2", atan2)
#undef BINARY

static pin_value random_sample(const pin_value *, int count, arena *)
{
    assert(count == 0);
    return (pin_value){ .type = PIN_F64, .d = (double)rand() / ((double)RAND_MAX + 1) };
}

static const char *random_value(const eval_context *, arena *, pin_value *out)
{
    *out = random_sample(NULL, 0, NULL);
    return NULL;
}

node_desc NODE_RandomReal = { .name = "Random Real",
                              .outputs = result,
                              .output_count = 1,
                              .pure_kind = NODE_PURE_READ,
                              .read = random_value,
                              .fn = random_sample,
                              .access = &random_access,
                              .category = "Math/Random",
                              .keywords = "rand random sample" };

static pin_spec random_range_real_inputs[] = {
    { .name = "Min",
      .type = PIN_F64,
      .type_name = "double",
      .default_value = { .type = PIN_F64, .d = 0 } },
    { .name = "Max",
      .type = PIN_F64,
      .type_name = "double",
      .default_value = { .type = PIN_F64, .d = 1 } },
};

static pin_value random_range_real(const pin_value *inputs, int count, arena *)
{
    assert(count == 2 && inputs[0].type == PIN_F64 && inputs[1].type == PIN_F64);
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

node_desc NODE_RandomRangeReal = {
    .name = "Random Range",
    .inputs = random_range_real_inputs,
    .input_count = 2,
    .outputs = result,
    .output_count = 1,
    .pure_kind = NODE_PURE_CALL,
    .fn = random_range_real,
    .access = &random_access,
    .category = "Math/Random",
    .keywords = "rand random range sample min max"
};

static pin_spec damped_spring_inputs[] = {
    { .name = "Position",
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 0 } },
    { .name = "Velocity",
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 0 } },
    { .name = "Target",
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 0 } },
    { .name = "Frequency",
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 1 } },
    { .name = "Damping",
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 1 } },
    { .name = "Delta",
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 0 } },
};
static pin_spec damped_spring_outputs[] = {
    { .name = "Position", .type = PIN_F32, .type_name = "float" },
    { .name = "Velocity", .type = PIN_F32, .type_name = "float" },
};

static const char *advance_damped_spring(
    const eval_context *,
    const pin_value *inputs,
    arena *,
    pin_value *out
)
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

node_desc NODE_Advance_Damped_Spring = {
    .name = "Advance Damped Spring",
    .input_count = 6,
    .output_count = 2,
    .inputs = damped_spring_inputs,
    .outputs = damped_spring_outputs,
    .pure_kind = NODE_PURE_READ,
    .read_inputs = advance_damped_spring,
    .access = &pure,
    .category = "Math/Motion",
    .keywords = "spring damped response settle position velocity target "
                "frequency damping delta",
};

static pin_spec quantize_angle_inputs[] = {
    { .name = "Angle",
      .type = PIN_F64,
      .type_name = "double",
      .default_value = { .type = PIN_F64, .d = 0 } },
    { .name = "Step",
      .type = PIN_F64,
      .type_name = "double",
      .default_value = { .type = PIN_F64, .d = 1.5707963267948966 } },
};
static pin_value quantize_angle(const pin_value *inputs, int count, arena *)
{
    assert(count == 2 && inputs[0].type == PIN_F64 && inputs[1].type == PIN_F64);
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

node_desc NODE_Quantize_Angle = {
    .name = "Quantize Angle",
    .input_count = 2,
    .output_count = 1,
    .inputs = quantize_angle_inputs,
    .outputs = result,
    .pure_kind = NODE_PURE_CALL,
    .fn = quantize_angle,
    .access = &pure,
    .category = "Math/Angles",
    .keywords = "angle quantize snap step radians rotation",
};

static pin_spec smooth_step_inputs[] = {
    { .name = "Value", .type = PIN_F64, .type_name = "double" },
};
static pin_value smooth_step(const pin_value *inputs, int count, arena *)
{
    assert(count == 1 && inputs[0].type == PIN_F64);
    double value = inputs[0].d;
    if (!isfinite(value)) {
        return (pin_value){ .type = PIN_FLOW };
    }
    value = fmin(fmax(value, 0), 1);
    return (pin_value){ .type = PIN_F64,
                        .type_name = "double",
                        .d = value * value * (3 - 2 * value) };
}

node_desc NODE_Smooth_Step = {
    .name = "Smooth Step",
    .input_count = 1,
    .output_count = 1,
    .inputs = smooth_step_inputs,
    .outputs = result,
    .pure_kind = NODE_PURE_CALL,
    .fn = smooth_step,
    .access = &pure,
    .category = "Math/Easing",
    .keywords = "smooth step ease interpolate normalized clamp",
};
