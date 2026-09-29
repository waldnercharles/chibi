#include "graph/nodes/math_nodes.h"

#include "graph/compiler/behavior_scalar.h"
#include "graph/runtime/flow.h"
#include "graph/runtime/random.h"
#include "support/value_noise.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

static const node_access pure = { .flags = NODE_EFFECT_PURE };
static const node_access random_access = { .flags = NODE_EFFECT_HOST_SINK };
static pin_spec unary[] = { { .name = "Value", .type = PIN_F64, .type_name = "double" } };
static pin_spec power_inputs[] = {
    { .name = "Base", .type = PIN_F64, .type_name = "double" },
    { .name = "Exponent", .type = PIN_F64, .type_name = "double" }
};
static pin_spec atan2_inputs[] = {
    { .name = "Y", .type = PIN_F64, .type_name = "double" },
    { .name = "X", .type = PIN_F64, .type_name = "double" }
};
static pin_spec result[] = { { .name = "Result", .type = PIN_F64, .type_name = "double" } };

#define UNARY(NAME, LABEL, FN, DESCRIPTION)                                    \
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
                              .description = DESCRIPTION,                      \
                              .foldable = true };
UNARY(Sine, "Sine", sin, "Returns the sine of Value, an angle in radians.")
UNARY(Cosine, "Cosine", cos, "Returns the cosine of Value, an angle in radians.")
UNARY(Tangent, "Tangent", tan, "Returns the tangent of Value, an angle in radians.")
UNARY(ArcSine, "Arc Sine", asin, "Returns the angle in radians, from -pi/2 to pi/2, whose sine is Value; NaN outside -1 to 1.")
UNARY(ArcCosine, "Arc Cosine", acos, "Returns the angle in radians, from 0 to pi, whose cosine is Value; NaN outside -1 to 1.")
UNARY(ArcTangent, "Arc Tangent", atan, "Returns the angle in radians, from -pi/2 to pi/2, whose tangent is Value.")
UNARY(SquareRoot, "Square Root", sqrt, "Returns the non-negative square root of Value; NaN for negative Value.")
UNARY(Exponential, "Exponential", exp, "Returns e raised to the power of Value.")
UNARY(Logarithm, "Logarithm", log, "Returns the natural (base e) logarithm of Value; 0 gives -infinity and negatives NaN.")
UNARY(Logarithm10, "Logarithm 10", log10, "Returns the base-10 logarithm of Value; 0 gives -infinity and negatives NaN.")
UNARY(Floor, "Floor", floor, "Rounds Value down to the nearest whole number.")
UNARY(Ceiling, "Ceiling", ceil, "Rounds Value up to the nearest whole number.")
UNARY(Round, "Round", round, "Rounds Value to the nearest whole number, with halves away from zero.")
UNARY(Fract, "Fract", behavior_fract, "Returns Value minus its floor, from 0 up to 1, so -0.25 gives 0.75.")
UNARY(Sign, "Sign", behavior_sign, "Returns -1, 0 or 1 as Value is negative, zero or positive.")
#undef UNARY

#define BINARY(NAME, LABEL, FN, INPUTS, KEYWORDS, DESCRIPTION)                        \
    static pin_value NAME##_value(const pin_value *inputs, int count, arena *)        \
    {                                                                                 \
        assert(count == 2 && inputs[0].type == PIN_F64 && inputs[1].type == PIN_F64); \
        return (pin_value){ .type = PIN_F64, .d = FN(inputs[0].d, inputs[1].d) };     \
    }                                                                                 \
    node_desc NODE_##NAME = { .name = LABEL,                                          \
                              .inputs = INPUTS,                                       \
                              .input_count = 2,                                       \
                              .outputs = result,                                      \
                              .output_count = 1,                                      \
                              .pure_kind = NODE_PURE_CALL,                            \
                              .fn = NAME##_value,                                     \
                              .access = &pure,                                        \
                              .category = "Math/Functions",                           \
                              .keywords = KEYWORDS,                                   \
                              .description = DESCRIPTION,                             \
                              .foldable = true };
BINARY(Power, "Power", pow, power_inputs, "exponent pow", "Raises Base to the power of Exponent.")
BINARY(Atan2, "Atan2", atan2, atan2_inputs, "arc tangent angle direction radians", "Returns the angle in radians, from -pi to pi, of the point (X, Y) from the positive x axis.")
#undef BINARY

/* step(edge, x): 0 below the edge, 1 from it on (GLSL). */
static pin_spec step_inputs[] = {
    { .name = "Value", .type = PIN_F64, .type_name = "double" },
    { .name = "Edge", .type = PIN_F64, .type_name = "double" }
};
static pin_value step_value(const pin_value *inputs, int count, arena *)
{
    assert(count == 2 && inputs[0].type == PIN_F64 && inputs[1].type == PIN_F64);
    return (pin_value){ .type = PIN_F64, .d = inputs[0].d < inputs[1].d ? 0 : 1 };
}
node_desc NODE_Step = {
    .name = "Step",
    .inputs = step_inputs,
    .input_count = 2,
    .outputs = result,
    .output_count = 1,
    .pure_kind = NODE_PURE_CALL,
    .fn = step_value,
    .access = &pure,
    .category = "Math/Functions",
    .keywords = "step edge threshold",
    .description = "Returns 0 when Value is below Edge, otherwise 1.",
    .foldable = true
};

/* mix(a, b, alpha): a at 0, b at 1 (GLSL; Unreal's Lerp). */
static pin_spec mix_inputs[] = {
    { .name = "A", .type = PIN_F64, .type_name = "double" },
    { .name = "B", .type = PIN_F64, .type_name = "double" },
    { .name = "Alpha", .type = PIN_F64, .type_name = "double" }
};
static pin_value mix_value(const pin_value *inputs, int count, arena *)
{
    assert(
        count == 3 && inputs[0].type == PIN_F64 && inputs[1].type == PIN_F64 &&
        inputs[2].type == PIN_F64
    );
    /* Separate steps, as every backend computes it, never a fused multiply-add. */
    double span = inputs[1].d - inputs[0].d;
    double offset = span * inputs[2].d;
    return (pin_value){ .type = PIN_F64, .d = inputs[0].d + offset };
}
node_desc NODE_Mix = { .name = "Mix",
                       .inputs = mix_inputs,
                       .input_count = 3,
                       .outputs = result,
                       .output_count = 1,
                       .pure_kind = NODE_PURE_CALL,
                       .fn = mix_value,
                       .access = &pure,
                       .category = "Math/Functions",
                       .keywords = "mix lerp interpolate blend",
                       .description = "Blends from A at Alpha 0 to B at Alpha "
                                      "1; Alpha outside 0 to 1 extrapolates.",
                       .foldable = true };

static pin_value random_sample(const pin_value *, int count, arena *)
{
    assert(count == 0);
    return (pin_value){ .type = PIN_F64, .d = behavior_random_unit() };
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
                              .keywords = "rand random sample",
                              .description = "Returns a new random number from "
                                             "0 up to, but not including, 1." };

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
    .keywords = "rand random range sample min max",
    .description = "Returns a random number between Min and Max, which default "
                   "to 0 and 1; reversed bounds swap."
};

static pin_spec lattice_inputs[] = {
    { .name = "X", .type = PIN_F64, .type_name = "double" },
    { .name = "Y", .type = PIN_F64, .type_name = "double" },
    { .name = "Seed", .type = PIN_F64, .type_name = "double" },
};

/* The field graph's lattice with Seed as its whole seed: truncated to an
 * integer as the field truncates its Seed Offset. */
static bool lattice_point(double x, double y, double seed)
{
    return fabs(x) < VALUE_NOISE_LIMIT && fabs(y) < VALUE_NOISE_LIMIT &&
           fabs(seed) < VALUE_NOISE_LIMIT;
}
static double lattice_hash(double x, double y, double seed)
{
    if (!lattice_point(x, y, seed)) {
        return NAN;
    }
    return value_hash((unsigned)(int)seed, (int)floorf((float)x), (int)floorf((float)y));
}
static double lattice_noise(double x, double y, double seed)
{
    if (!lattice_point(x, y, seed)) {
        return NAN;
    }
    return value_noise((unsigned)(int)seed, (float)x, (float)y);
}

#define LATTICE(NAME, LABEL, FN, KEYWORDS, DESCRIPTION)                                         \
    static pin_value NAME##_value(const pin_value *inputs, int count, arena *)                  \
    {                                                                                           \
        assert(count == 3);                                                                     \
        return (pin_value){ .type = PIN_F64,                                                    \
                            .d = FN(inputs[0].d, inputs[1].d, inputs[2].d) };                   \
    }                                                                                           \
    static const char *NAME##_call(arena *, const pin_value *inputs, int count, pin_value *out) \
    {                                                                                           \
        *out = NAME##_value(inputs, count, NULL);                                               \
        return NULL;                                                                            \
    }                                                                                           \
    node_desc NODE_##NAME = { .name = LABEL,                                                    \
                              .inputs = lattice_inputs,                                         \
                              .input_count = 3,                                                 \
                              .outputs = result,                                                \
                              .output_count = 1,                                                \
                              .pure_kind = NODE_PURE_CALL,                                      \
                              .fn = NAME##_value,                                               \
                              .host_call = &(const node_host_call){ .value = NAME##_call },     \
                              .access = &pure,                                                  \
                              .category = "Math/Random",                                        \
                              .keywords = KEYWORDS,                                             \
                              .description = DESCRIPTION,                                       \
                              .foldable = true };
LATTICE(Hash, "Hash", lattice_hash, "random cell lattice seed deterministic field", "Returns a value from -1 to 1 fixed for Seed and the unit cell holding (X, Y), matching the field graph's hash.")
LATTICE(Noise, "Noise", lattice_noise, "value noise smooth lattice seed field", "Returns smooth value noise from -1 to 1 at (X, Y) for Seed, one lattice cell per unit, matching the field graph's noise.")
#undef LATTICE

static const char *seed_call(void *, const pin_value *inputs, pin_value *)
{
    behavior_srand((uint32_t)inputs[0].i);
    return NULL;
}
static int set_random_seed(node *, flow_context *ctx)
{
    pin_value seed;
    int result = flow_context_eval_data(ctx, 1, &seed);
    if (result < 0) {
        return result;
    }
    seed_call(NULL, &seed, NULL);
    return flow_context_continue(ctx, 0);
}
static pin_spec seed_inputs[] = {
    { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Seed", .type = PIN_I32, .type_name = "int" },
};
static pin_spec seed_outputs[] = { { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC } };
node_desc NODE_SetRandomSeed = {
    .name = "Set Random Seed",
    .inputs = seed_inputs,
    .input_count = 2,
    .outputs = seed_outputs,
    .output_count = 1,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = set_random_seed,
    .host_call = &(const node_host_call){ .invoke = seed_call },
    .display_class = NODE_DISPLAY_FLOW,
    .access = &random_access,
    .category = "Math/Random",
    .keywords = "rand random seed reseed deterministic replay",
    .description = "Restarts this world's random stream from Seed, so later "
                   "random draws repeat for the same Seed.",
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
    .description = "Steps Position and Velocity toward Target by Delta "
                   "seconds; Frequency is in Hz and Damping 1 is critical.",
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
    .description = "Rounds Angle to the nearest multiple of Step, in radians; "
                   "Step defaults to a quarter turn.",
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
    .description = "Clamps Value to 0 to 1, then eases it along an S-curve "
                   "that is flat at both ends.",
};
