#include "graph/nodes/math_nodes.h"

#include "graph/model/native_definition.h"
#include "runtime/math_native.h"

static pin_spec unary[] = { NATIVE_MATH_UNARY_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec result[] = { NATIVE_MATH_RESULT_OUTPUTS(GRAPH_NATIVE_PORT) };
static pin_spec number_result[] = { NATIVE_MATH_F32_OUTPUTS(GRAPH_NATIVE_PORT) };

#define UNARY(NAME, LABEL, SOURCE, FN, DESCRIPTION)                            \
    node_desc NODE_##NAME = { .name = LABEL,                                   \
                              .inputs = unary,                                 \
                              .input_count = 1,                                \
                              .outputs = result,                               \
                              .output_count = 1,                               \
                              .pure_kind = NODE_PURE_CALL,                     \
                              .fn = native_math_##NAME,                        \
                              .execution_call = &NATIVE_MATH_##NAME##_CALL,    \
                              .access = &NATIVE_MATH_ACCESS,                   \
                              .category = "Math/Functions",                    \
                              .description = DESCRIPTION,                      \
                              .foldable = true };
NATIVE_MATH_UNARIES(UNARY)
#undef UNARY

#define BINARY(NAME, LABEL, SOURCE, FN, INPUTS, KEYWORDS, DESCRIPTION)         \
    static pin_spec NAME##_inputs[] = { INPUTS(GRAPH_NATIVE_PORT) };           \
    node_desc NODE_##NAME = { .name = LABEL,                                   \
                              .inputs = NAME##_inputs,                         \
                              .input_count = 2,                                \
                              .outputs = result,                               \
                              .output_count = 1,                               \
                              .pure_kind = NODE_PURE_CALL,                     \
                              .fn = native_math_##NAME,                        \
                              .execution_call = &NATIVE_MATH_##NAME##_CALL,    \
                              .access = &NATIVE_MATH_ACCESS,                   \
                              .category = "Math/Functions",                    \
                              .keywords = KEYWORDS,                            \
                              .description = DESCRIPTION,                      \
                              .foldable = true };
NATIVE_MATH_BINARIES(BINARY)
#undef BINARY

/* step(edge, x): 0 below the edge, 1 from it on (GLSL). */
static pin_spec step_inputs[] = { NATIVE_MATH_STEP_INPUTS(GRAPH_NATIVE_PORT) };
node_desc NODE_Step = {
    .name = NATIVE_MATH_Step_NAME,
    .inputs = step_inputs,
    .input_count = 2,
    .outputs = result,
    .output_count = 1,
    .pure_kind = NODE_PURE_CALL,
    .fn = native_math_Step,
    .execution_call = &NATIVE_MATH_Step_CALL,
    .access = &NATIVE_MATH_ACCESS,
    .category = "Math/Functions",
    .keywords = "step edge threshold",
    .description = "Returns 0 when Value is below Edge, otherwise 1.",
    .foldable = true
};

/* mix(a, b, alpha): a at 0, b at 1 (GLSL; Unreal's Lerp). */
static pin_spec mix_inputs[] = { NATIVE_MATH_MIX_INPUTS(GRAPH_NATIVE_PORT) };
node_desc NODE_Mix = { .name = NATIVE_MATH_Mix_NAME,
                       .inputs = mix_inputs,
                       .input_count = 3,
                       .outputs = result,
                       .output_count = 1,
                       .pure_kind = NODE_PURE_CALL,
                       .fn = native_math_Mix,
                       .execution_call = &NATIVE_MATH_Mix_CALL,
                       .access = &NATIVE_MATH_ACCESS,
                       .category = "Math/Functions",
                       .keywords = "mix lerp interpolate blend",
                       .description = "Blends from A at Alpha 0 to B at Alpha "
                                      "1; Alpha outside 0 to 1 extrapolates.",
                       .foldable = true };

static const char *random_value(const eval_context *, arena *, pin_value *out)
{
    *out = native_math_RandomReal(NULL, 0, NULL);
    return NULL;
}

node_desc NODE_RandomReal = { .name = NATIVE_MATH_RandomReal_NAME,
                              .source_name = NATIVE_MATH_RandomReal_SOURCE,
                              .outputs = number_result,
                              .output_count = 1,
                              .pure_kind = NODE_PURE_READ,
                              .read = random_value,
                              .fn = native_math_RandomReal,
                              .execution_call = &NATIVE_MATH_RandomReal_CALL,
                              .access = &NATIVE_MATH_RANDOM_ACCESS,
                              .category = "Math/Random",
                              .keywords = "rand random sample",
                              .description = "Returns a new random number from "
                                             "0 up to, but not including, 1." };

static pin_spec random_range_real_inputs[] = { NATIVE_MATH_RANGE_INPUTS(GRAPH_NATIVE_PORT) };

node_desc NODE_RandomRangeReal = {
    .name = NATIVE_MATH_RandomRangeReal_NAME,
    .inputs = random_range_real_inputs,
    .input_count = 2,
    .outputs = result,
    .output_count = 1,
    .pure_kind = NODE_PURE_CALL,
    .fn = native_math_RandomRangeReal,
    .execution_call = &NATIVE_MATH_RandomRangeReal_CALL,
    .access = &NATIVE_MATH_RANDOM_ACCESS,
    .category = "Math/Random",
    .keywords = "rand random range sample min max",
    .description = "Returns a random number between Min and Max, which default "
                   "to 0 and 1; reversed bounds swap."
};

static pin_spec damped_spring_inputs[] = { NATIVE_MATH_SPRING_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec damped_spring_outputs[] = { NATIVE_MATH_SPRING_OUTPUTS(GRAPH_NATIVE_PORT) };

static const char *advance_damped_spring(
    const eval_context *,
    const pin_value *inputs,
    arena *,
    pin_value *out
)
{
    return native_math_spring(inputs, out);
}

node_desc NODE_Advance_Damped_Spring = {
    .name = NATIVE_MATH_Advance_Damped_Spring_NAME,
    .input_count = 6,
    .output_count = 2,
    .inputs = damped_spring_inputs,
    .outputs = damped_spring_outputs,
    .pure_kind = NODE_PURE_READ,
    .read_inputs = advance_damped_spring,
    .execution_call = &NATIVE_MATH_Advance_Damped_Spring_CALL,
    .access = &NATIVE_MATH_ACCESS,
    .category = "Math/Motion",
    .keywords = "spring damped response settle position velocity target "
                "frequency damping delta",
    .description = "Steps Position and Velocity toward Target by Delta "
                   "seconds; Frequency is in Hz and Damping 1 is critical.",
};

static pin_spec quantize_angle_inputs[] = { NATIVE_MATH_QUANTIZE_INPUTS(GRAPH_NATIVE_PORT) };

node_desc NODE_Quantize_Angle = {
    .name = NATIVE_MATH_Quantize_Angle_NAME,
    .input_count = 2,
    .output_count = 1,
    .inputs = quantize_angle_inputs,
    .outputs = result,
    .pure_kind = NODE_PURE_CALL,
    .fn = native_math_Quantize_Angle,
    .execution_call = &NATIVE_MATH_Quantize_Angle_CALL,
    .access = &NATIVE_MATH_ACCESS,
    .category = "Math/Angles",
    .keywords = "angle quantize snap step radians rotation",
    .description = "Rounds Angle to the nearest multiple of Step, in radians; "
                   "Step defaults to a quarter turn.",
};

static pin_spec smooth_step_inputs[] = { NATIVE_MATH_UNARY_INPUTS(GRAPH_NATIVE_PORT) };

node_desc NODE_Smooth_Step = {
    .name = NATIVE_MATH_Smooth_Step_NAME,
    .input_count = 1,
    .output_count = 1,
    .inputs = smooth_step_inputs,
    .outputs = result,
    .pure_kind = NODE_PURE_CALL,
    .fn = native_math_Smooth_Step,
    .execution_call = &NATIVE_MATH_Smooth_Step_CALL,
    .access = &NATIVE_MATH_ACCESS,
    .category = "Math/Easing",
    .keywords = "smooth step ease interpolate normalized clamp",
    .description = "Clamps Value to 0 to 1, then eases it along an S-curve "
                   "that is flat at both ends.",
};
