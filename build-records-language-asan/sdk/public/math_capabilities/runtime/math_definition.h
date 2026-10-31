#ifndef CHIBI_NATIVE_MATH_DEFINITION_H
#define CHIBI_NATIVE_MATH_DEFINITION_H
#include "runtime/native_definitions.h"

#define NATIVE_MATH_PORT(X, NAME, TYPE, NOMINAL, VALUE)                        \
    X(NAME, NATIVE_PARAMETER_DATA, TYPE, NOMINAL, PIN_CONTAINER_SINGLE, 0, 0, SCRIPT_REFERENCE_NONE, false, VALUE)
#define NATIVE_MATH_DOUBLE(X, NAME)                                            \
    NATIVE_MATH_PORT(X, NAME, PIN_F64, "f64", ((pin_value){ 0 }))
#define NATIVE_MATH_EMPTY_INPUTS(X)
#define NATIVE_MATH_UNARY_INPUTS(X) NATIVE_MATH_DOUBLE(X, "Value")
#define NATIVE_MATH_RESULT_OUTPUTS(X) NATIVE_MATH_DOUBLE(X, "Result")
#define NATIVE_MATH_POWER_INPUTS(X)                                            \
    NATIVE_MATH_DOUBLE(X, "Base") NATIVE_MATH_DOUBLE(X, "Exponent")
#define NATIVE_MATH_ATAN2_INPUTS(X)                                            \
    NATIVE_MATH_DOUBLE(X, "Y") NATIVE_MATH_DOUBLE(X, "X")
#define NATIVE_MATH_STEP_INPUTS(X)                                             \
    NATIVE_MATH_DOUBLE(X, "Value") NATIVE_MATH_DOUBLE(X, "Edge")
#define NATIVE_MATH_MIX_INPUTS(X)                                              \
    NATIVE_MATH_DOUBLE(X, "A")                                                 \
    NATIVE_MATH_DOUBLE(X, "B") NATIVE_MATH_DOUBLE(X, "Alpha")
#define NATIVE_MATH_RANGE_INPUTS(X)                                                      \
    NATIVE_MATH_PORT(X, "Min", PIN_F64, "f64", ((pin_value){ .type = PIN_F64, .d = 0 })) \
    NATIVE_MATH_PORT(X, "Max", PIN_F64, "f64", ((pin_value){ .type = PIN_F64, .d = 1 }))
#define NATIVE_MATH_QUANTIZE_INPUTS(X)                                                     \
    NATIVE_MATH_PORT(X, "Angle", PIN_F64, "f64", ((pin_value){ .type = PIN_F64, .d = 0 })) \
    NATIVE_MATH_PORT(X, "Step", PIN_F64, "f64", ((pin_value){ .type = PIN_F64, .d = 1.5707963267948966 }))
#define NATIVE_MATH_FLOAT(X, NAME, VALUE)                                      \
    NATIVE_MATH_PORT(X, NAME, PIN_F32, "number", ((pin_value){ .type = PIN_F32, .f = VALUE }))
#define NATIVE_MATH_F32_UNARY_INPUTS(X) NATIVE_MATH_FLOAT(X, "Value", 0)
#define NATIVE_MATH_F32_OUTPUTS(X) NATIVE_MATH_FLOAT(X, "Result", 0)
#define NATIVE_MATH_F32_Power_INPUTS(X)                                        \
    NATIVE_MATH_FLOAT(X, "Base", 0) NATIVE_MATH_FLOAT(X, "Exponent", 0)
#define NATIVE_MATH_F32_Atan2_INPUTS(X)                                        \
    NATIVE_MATH_FLOAT(X, "Y", 0) NATIVE_MATH_FLOAT(X, "X", 0)
#define NATIVE_MATH_F32_Step_INPUTS(X)                                         \
    NATIVE_MATH_FLOAT(X, "Value", 0) NATIVE_MATH_FLOAT(X, "Edge", 0)
#define NATIVE_MATH_F32_Mix_INPUTS(X)                                          \
    NATIVE_MATH_FLOAT(X, "A", 0)                                               \
    NATIVE_MATH_FLOAT(X, "B", 0) NATIVE_MATH_FLOAT(X, "Alpha", 0)
#define NATIVE_MATH_F32_Quantize_Angle_INPUTS(X)                               \
    NATIVE_MATH_FLOAT(X, "Angle", 0)                                           \
    NATIVE_MATH_FLOAT(X, "Step", 1.5707963267948966f)
#define NATIVE_MATH_F32_Smooth_Step_INPUTS(X) NATIVE_MATH_F32_UNARY_INPUTS(X)
#define NATIVE_MATH_F32_RANGE_INPUTS(X)                                        \
    NATIVE_MATH_FLOAT(X, "Min", 0) NATIVE_MATH_FLOAT(X, "Max", 1)
#define NATIVE_MATH_SPRING_INPUTS(X)                                           \
    NATIVE_MATH_FLOAT(X, "Position", 0)                                        \
    NATIVE_MATH_FLOAT(X, "Velocity", 0)                                        \
    NATIVE_MATH_FLOAT(X, "Target", 0)                                          \
    NATIVE_MATH_FLOAT(X, "Frequency", 1)                                       \
    NATIVE_MATH_FLOAT(X, "Damping", 1) NATIVE_MATH_FLOAT(X, "Delta", 0)
#define NATIVE_MATH_SPRING_OUTPUTS(X)                                          \
    NATIVE_MATH_PORT(X, "Position", PIN_F32, "number", ((pin_value){ 0 }))     \
    NATIVE_MATH_PORT(X, "Velocity", PIN_F32, "number", ((pin_value){ 0 }))

#define NATIVE_MATH_Sine_NAME "Sine"
#define NATIVE_MATH_Sine_SOURCE "sin"
#define NATIVE_MATH_Cosine_NAME "Cosine"
#define NATIVE_MATH_Cosine_SOURCE "cos"
#define NATIVE_MATH_Tangent_NAME "Tangent"
#define NATIVE_MATH_Tangent_SOURCE "tan"
#define NATIVE_MATH_ArcSine_NAME "Arc Sine"
#define NATIVE_MATH_ArcSine_SOURCE "asin"
#define NATIVE_MATH_ArcCosine_NAME "Arc Cosine"
#define NATIVE_MATH_ArcCosine_SOURCE "acos"
#define NATIVE_MATH_ArcTangent_NAME "Arc Tangent"
#define NATIVE_MATH_ArcTangent_SOURCE "atan"
#define NATIVE_MATH_SquareRoot_NAME "Square Root"
#define NATIVE_MATH_SquareRoot_SOURCE "sqrt"
#define NATIVE_MATH_Exponential_NAME "Exponential"
#define NATIVE_MATH_Exponential_SOURCE "exp"
#define NATIVE_MATH_Logarithm_NAME "Logarithm"
#define NATIVE_MATH_Logarithm_SOURCE "log"
#define NATIVE_MATH_Logarithm10_NAME "Logarithm 10"
#define NATIVE_MATH_Logarithm10_SOURCE "log10"
#define NATIVE_MATH_Floor_NAME "Floor"
#define NATIVE_MATH_Floor_SOURCE "floor"
#define NATIVE_MATH_Ceiling_NAME "Ceiling"
#define NATIVE_MATH_Ceiling_SOURCE "ceil"
#define NATIVE_MATH_Round_NAME "Round"
#define NATIVE_MATH_Round_SOURCE "round"
#define NATIVE_MATH_Fract_NAME "Fract"
#define NATIVE_MATH_Fract_SOURCE "fract"
#define NATIVE_MATH_Sign_NAME "Sign"
#define NATIVE_MATH_Sign_SOURCE "sign"
#define NATIVE_MATH_Power_NAME "Power"
#define NATIVE_MATH_Power_SOURCE "pow"
#define NATIVE_MATH_Atan2_NAME "Atan2"
#define NATIVE_MATH_Atan2_SOURCE "atan2"
#define NATIVE_MATH_Step_NAME "Step"
#define NATIVE_MATH_Step_SOURCE "step"
#define NATIVE_MATH_Mix_NAME "Mix"
#define NATIVE_MATH_Mix_SOURCE "mix"
#define NATIVE_MATH_RandomReal_NAME "Random Real"
#define NATIVE_MATH_RandomReal_SOURCE "rand"
#define NATIVE_MATH_RandomRangeReal_NAME "Random Range"
#define NATIVE_MATH_RandomRangeReal_SOURCE "random_range"
#define NATIVE_MATH_Quantize_Angle_NAME "Quantize Angle"
#define NATIVE_MATH_Quantize_Angle_SOURCE "quantize_angle"
#define NATIVE_MATH_Smooth_Step_NAME "Smooth Step"
#define NATIVE_MATH_Smooth_Step_SOURCE "smooth_step"
#define NATIVE_MATH_Advance_Damped_Spring_NAME "Advance Damped Spring"
#define NATIVE_MATH_Advance_Damped_Spring_SOURCE "advance_damped_spring"

#define NATIVE_MATH_UNARIES(X)                                                                                                                                              \
    X(Sine,                                                                                                                                                                 \
      NATIVE_MATH_Sine_NAME,                                                                                                                                                \
      NATIVE_MATH_Sine_SOURCE,                                                                                                                                              \
      cos_sin_sine,                                                                                                                                                         \
      "Returns the sine of Value, an angle in radians.")                                                                                                                    \
    X(Cosine,                                                                                                                                                               \
      NATIVE_MATH_Cosine_NAME,                                                                                                                                              \
      NATIVE_MATH_Cosine_SOURCE,                                                                                                                                            \
      cos_sin_cosine,                                                                                                                                                       \
      "Returns the cosine of Value, an angle in radians.")                                                                                                                  \
    X(Tangent,                                                                                                                                                              \
      NATIVE_MATH_Tangent_NAME,                                                                                                                                             \
      NATIVE_MATH_Tangent_SOURCE,                                                                                                                                           \
      tan,                                                                                                                                                                  \
      "Returns the tangent of Value, an angle in radians.")                                                                                                                 \
    X(ArcSine, NATIVE_MATH_ArcSine_NAME, NATIVE_MATH_ArcSine_SOURCE, asin, "Returns the angle in radians, from -pi/2 to pi/2, whose sine is Value; NaN outside -1 to 1.")   \
    X(ArcCosine, NATIVE_MATH_ArcCosine_NAME, NATIVE_MATH_ArcCosine_SOURCE, acos, "Returns the angle in radians, from 0 to pi, whose cosine is Value; NaN outside -1 to 1.") \
    X(ArcTangent, NATIVE_MATH_ArcTangent_NAME, NATIVE_MATH_ArcTangent_SOURCE, atan, "Returns the angle in radians, from -pi/2 to pi/2, whose tangent is Value.")            \
    X(SquareRoot,                                                                                                                                                           \
      NATIVE_MATH_SquareRoot_NAME,                                                                                                                                          \
      NATIVE_MATH_SquareRoot_SOURCE,                                                                                                                                        \
      sqrt,                                                                                                                                                                 \
      "Returns the non-negative square root of Value; NaN for negative Value.")                                                                                             \
    X(Exponential,                                                                                                                                                          \
      NATIVE_MATH_Exponential_NAME,                                                                                                                                         \
      NATIVE_MATH_Exponential_SOURCE,                                                                                                                                       \
      exp,                                                                                                                                                                  \
      "Returns e raised to the power of Value.")                                                                                                                            \
    X(Logarithm, NATIVE_MATH_Logarithm_NAME, NATIVE_MATH_Logarithm_SOURCE, log, "Returns the natural (base e) logarithm of Value; 0 gives -infinity and negatives NaN.")    \
    X(Logarithm10, NATIVE_MATH_Logarithm10_NAME, NATIVE_MATH_Logarithm10_SOURCE, log10, "Returns the base-10 logarithm of Value; 0 gives -infinity and negatives NaN.")     \
    X(Floor, NATIVE_MATH_Floor_NAME, NATIVE_MATH_Floor_SOURCE, floor, "Rounds Value down to the nearest whole number.")                                                     \
    X(Ceiling, NATIVE_MATH_Ceiling_NAME, NATIVE_MATH_Ceiling_SOURCE, ceil, "Rounds Value up to the nearest whole number.")                                                  \
    X(Round,                                                                                                                                                                \
      NATIVE_MATH_Round_NAME,                                                                                                                                               \
      NATIVE_MATH_Round_SOURCE,                                                                                                                                             \
      round,                                                                                                                                                                \
      "Rounds Value to the nearest whole number, with halves away from zero.")                                                                                              \
    X(Fract,                                                                                                                                                                \
      NATIVE_MATH_Fract_NAME,                                                                                                                                               \
      NATIVE_MATH_Fract_SOURCE,                                                                                                                                             \
      behavior_fract,                                                                                                                                                       \
      "Returns Value minus its floor, from 0 up to 1, so -0.25 gives 0.75.")                                                                                                \
    X(Sign,                                                                                                                                                                 \
      NATIVE_MATH_Sign_NAME,                                                                                                                                                \
      NATIVE_MATH_Sign_SOURCE,                                                                                                                                              \
      behavior_sign,                                                                                                                                                        \
      "Returns -1, 0 or 1 as Value is negative, zero or positive.")

#define NATIVE_MATH_BINARIES(X)                                                \
    X(Power,                                                                   \
      NATIVE_MATH_Power_NAME,                                                  \
      NATIVE_MATH_Power_SOURCE,                                                \
      pow,                                                                     \
      NATIVE_MATH_POWER_INPUTS,                                                \
      "exponent pow",                                                          \
      "Raises Base to the power of Exponent.")                                 \
    X(Atan2,                                                                   \
      NATIVE_MATH_Atan2_NAME,                                                  \
      NATIVE_MATH_Atan2_SOURCE,                                                \
      atan2,                                                                   \
      NATIVE_MATH_ATAN2_INPUTS,                                                \
      "arc tangent angle direction radians",                                   \
      "Returns the angle in radians, from -pi to pi, of the point (X, Y) "     \
      "from the positive x axis.")

#define NATIVE_MATH_VALUES(X)                                                               \
    X(Step, NATIVE_MATH_Step_NAME, NATIVE_MATH_Step_SOURCE, NATIVE_MATH_STEP_INPUTS, false) \
    X(Mix, NATIVE_MATH_Mix_NAME, NATIVE_MATH_Mix_SOURCE, NATIVE_MATH_MIX_INPUTS, false)     \
    X(RandomRangeReal,                                                                      \
      NATIVE_MATH_RandomRangeReal_NAME,                                                     \
      NATIVE_MATH_RandomRangeReal_SOURCE,                                                   \
      NATIVE_MATH_RANGE_INPUTS,                                                             \
      true)                                                                                 \
    X(Quantize_Angle,                                                                       \
      NATIVE_MATH_Quantize_Angle_NAME,                                                      \
      NATIVE_MATH_Quantize_Angle_SOURCE,                                                    \
      NATIVE_MATH_QUANTIZE_INPUTS,                                                          \
      false)                                                                                \
    X(Smooth_Step,                                                                          \
      NATIVE_MATH_Smooth_Step_NAME,                                                         \
      NATIVE_MATH_Smooth_Step_SOURCE,                                                       \
      NATIVE_MATH_UNARY_INPUTS,                                                             \
      false)

extern const native_definition_pack NATIVE_MATH_DEFINITION_PACK;
#define NATIVE_MATH_F32_VALUES(X)                                                               \
    X(Step, NATIVE_MATH_Step_NAME, NATIVE_MATH_Step_SOURCE, NATIVE_MATH_F32_Step_INPUTS, false) \
    X(Mix, NATIVE_MATH_Mix_NAME, NATIVE_MATH_Mix_SOURCE, NATIVE_MATH_F32_Mix_INPUTS, false)     \
    X(RandomRangeReal,                                                                          \
      NATIVE_MATH_RandomRangeReal_NAME,                                                         \
      NATIVE_MATH_RandomRangeReal_SOURCE,                                                       \
      NATIVE_MATH_F32_RANGE_INPUTS,                                                             \
      true)                                                                                     \
    X(Quantize_Angle,                                                                           \
      NATIVE_MATH_Quantize_Angle_NAME,                                                          \
      NATIVE_MATH_Quantize_Angle_SOURCE,                                                        \
      NATIVE_MATH_F32_Quantize_Angle_INPUTS,                                                    \
      false)                                                                                    \
    X(Smooth_Step,                                                                              \
      NATIVE_MATH_Smooth_Step_NAME,                                                             \
      NATIVE_MATH_Smooth_Step_SOURCE,                                                           \
      NATIVE_MATH_F32_Smooth_Step_INPUTS,                                                       \
      false)
#endif
