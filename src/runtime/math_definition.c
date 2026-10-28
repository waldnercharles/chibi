#include "runtime/math_native.h"
static const native_parameter_definition UNARY_INPUTS[] = {
    NATIVE_MATH_UNARY_INPUTS(NATIVE_PORT)
};
static const native_parameter_definition OUTPUTS[] = {
    NATIVE_MATH_RESULT_OUTPUTS(NATIVE_PORT)
};
static const native_parameter_definition FLOAT_INPUTS[] = {
    NATIVE_MATH_F32_UNARY_INPUTS(NATIVE_PORT)
};
static const native_parameter_definition FLOAT_OUTPUTS[] = {
    NATIVE_MATH_F32_OUTPUTS(NATIVE_PORT)
};
static const native_parameter_definition RANDOM_REAL_INPUTS[] = {};
#define BINARY(KEY, NAME, SOURCE, FN, INPUTS, KEYWORDS, DESCRIPTION)                   \
    static const native_parameter_definition KEY##_inputs[] = { INPUTS(NATIVE_PORT) }; \
    static const native_parameter_definition F32_##KEY##_inputs[] = {                  \
        NATIVE_MATH_F32_##KEY##_INPUTS(NATIVE_PORT)                                    \
    };
#define VALUE(KEY, NAME, SOURCE, INPUTS, RANDOM)                               \
    static const native_parameter_definition KEY##_inputs[] = { INPUTS(NATIVE_PORT) };
NATIVE_MATH_BINARIES(BINARY)
NATIVE_MATH_VALUES(VALUE)
#define FLOAT_VALUE(KEY, NAME, SOURCE, INPUTS, RANDOM)                         \
    static const native_parameter_definition F32_##KEY##_inputs[] = { INPUTS(NATIVE_PORT) };
NATIVE_MATH_F32_VALUES(FLOAT_VALUE)
#undef FLOAT_VALUE
#undef BINARY
#undef VALUE
#define DEFINITION(KEY, NAME, SOURCE, INPUTS, OUT, RANDOM)                     \
    { .canonical_name = NAME,                                                  \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                      \
      .pure = true,                                                            \
      .effects = RANDOM ? SCRIPT_EFFECT_RANDOM : 0,                            \
      .completion_output = -1,                                                 \
      .inputs = INPUTS,                                                        \
      .input_count = NATIVE_COUNT(INPUTS),                                     \
      .outputs = OUT,                                                          \
      .output_count = NATIVE_COUNT(OUT) },
#define UNARY(KEY, NAME, SOURCE, FN, DESCRIPTION)                              \
    DEFINITION(KEY, NAME, SOURCE, UNARY_INPUTS, OUTPUTS, false)                \
    DEFINITION(F32_##KEY, NAME " Float", SOURCE, FLOAT_INPUTS, FLOAT_OUTPUTS, false)
#define BINARY(KEY, NAME, SOURCE, FN, INPUTS, KEYWORDS, DESCRIPTION)           \
    DEFINITION(KEY, NAME, SOURCE, KEY##_inputs, OUTPUTS, false)                \
    DEFINITION(F32_##KEY, NAME " Float", SOURCE, F32_##KEY##_inputs, FLOAT_OUTPUTS, false)
#define VALUE(KEY, NAME, SOURCE, INPUTS, RANDOM)                               \
    DEFINITION(KEY, NAME, SOURCE, KEY##_inputs, OUTPUTS, RANDOM)
static const native_capability_definition DEFINITIONS[] = {
    NATIVE_MATH_UNARIES(UNARY) NATIVE_MATH_BINARIES(BINARY) NATIVE_MATH_VALUES(VALUE)
    /* rand has no f64 form: no argument could choose it. */
    DEFINITION(RandomReal, NATIVE_MATH_RandomReal_NAME, NATIVE_MATH_RandomReal_SOURCE, RANDOM_REAL_INPUTS, FLOAT_OUTPUTS, true)
#define FLOAT_VALUE(KEY, NAME, SOURCE, INPUTS, RANDOM)                         \
    DEFINITION(F32_##KEY, NAME " Float", SOURCE, F32_##KEY##_inputs, FLOAT_OUTPUTS, RANDOM)
        NATIVE_MATH_F32_VALUES(FLOAT_VALUE)
#undef FLOAT_VALUE
};
#undef UNARY
#undef BINARY
#undef VALUE
#undef DEFINITION
#define BINDING(KEY, NAME, RANDOM)                                             \
    { .canonical_name = NAME,                                                  \
      .implementation = { .call = &NATIVE_MATH_##KEY##_CALL,                   \
                          .access = RANDOM ? &NATIVE_MATH_RANDOM_ACCESS        \
                                           : &NATIVE_MATH_ACCESS } },
#define UNARY(KEY, NAME, SOURCE, FN, DESCRIPTION)                              \
    BINDING(KEY, NAME, false) BINDING(F32_##KEY, NAME " Float", false)
#define BINARY(KEY, NAME, SOURCE, FN, INPUTS, KEYWORDS, DESCRIPTION)           \
    BINDING(KEY, NAME, false) BINDING(F32_##KEY, NAME " Float", false)
#define VALUE(KEY, NAME, SOURCE, INPUTS, RANDOM) BINDING(KEY, NAME, RANDOM)
static const native_definition_binding BINDINGS[] = {
    NATIVE_MATH_UNARIES(UNARY) NATIVE_MATH_BINARIES(BINARY) NATIVE_MATH_VALUES(VALUE)
        BINDING(RandomReal, NATIVE_MATH_RandomReal_NAME, true)
#define FLOAT_VALUE(KEY, NAME, SOURCE, INPUTS, RANDOM)                         \
    BINDING(F32_##KEY, NAME " Float", RANDOM)
            NATIVE_MATH_F32_VALUES(FLOAT_VALUE)
#undef FLOAT_VALUE
};
#undef UNARY
#undef BINARY
#undef VALUE
#undef BINDING
const native_definition_pack NATIVE_MATH_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS)
};
