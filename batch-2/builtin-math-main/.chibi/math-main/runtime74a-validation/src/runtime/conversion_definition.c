#include "runtime/conversions.h"
#define SCALAR(KEY, NAME, SOURCE, INPUT, INPUT_NAME, RESULT, OUTPUT, OUTPUT_NAME) \
    static const native_parameter_definition KEY##_inputs[] = {                   \
        { .name = "Value",                                                        \
          .kind = NATIVE_PARAMETER_DATA,                                          \
          .storage = INPUT,                                                       \
          .type_name = INPUT_NAME,                                                \
          .container = PIN_CONTAINER_SINGLE,                                      \
          .has_default = true,                                                    \
          .default_value = { .type = INPUT },                                     \
          .c_string = OUTPUT == PIN_NAME },                                       \
    };                                                                            \
    static const native_parameter_definition KEY##_outputs[] = {                  \
        NATIVE_CONVERSION_PORT(NATIVE_PORT, RESULT, OUTPUT, OUTPUT_NAME)          \
    };
#define VECTOR(TYPE, WIDTH, NAME, SOURCE, DESCRIPTION)                         \
    SCALAR(Splat_##TYPE, NAME, SOURCE, PIN_F64, "f64", "Vector", PIN_STRUCT, #TYPE)
NATIVE_CONVERSION_SCALARS(SCALAR)
NATIVE_CONVERSION_VECTORS(VECTOR)
#undef SCALAR
#undef VECTOR
#define SCALAR(KEY, NAME, SOURCE, INPUT, INPUT_NAME, RESULT, OUTPUT, OUTPUT_NAME) \
    { .canonical_name = NAME,                                                     \
      .source_name = SOURCE,                                                      \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                         \
      .inputs = KEY##_inputs,                                                     \
      .input_count = NATIVE_COUNT(KEY##_inputs),                                  \
      .outputs = KEY##_outputs,                                                   \
      .output_count = NATIVE_COUNT(KEY##_outputs),                                \
      .completion_output = -1,                                                    \
      .pure = true },
#define VECTOR(TYPE, WIDTH, NAME, SOURCE, DESCRIPTION)                         \
    SCALAR(Splat_##TYPE, NAME, SOURCE, PIN_F64, "f64", "Vector", PIN_STRUCT, #TYPE)
static const native_capability_definition DEFINITIONS[] = {
    NATIVE_CONVERSION_SCALARS(SCALAR) NATIVE_CONVERSION_VECTORS(VECTOR)
};
#undef SCALAR
#undef VECTOR
#define SCALAR(KEY, NAME, SOURCE, INPUT, INPUT_NAME, RESULT, OUTPUT, OUTPUT_NAME) \
    { .canonical_name = NAME,                                                     \
      .implementation = { .call = &NATIVE_CONVERSION_##KEY##_CALL,                \
                          .access = &NATIVE_CONVERSION_ACCESS } },
#define VECTOR(TYPE, WIDTH, NAME, SOURCE, DESCRIPTION)                         \
    SCALAR(Splat_##TYPE, NAME, SOURCE, PIN_F64, "f64", "Vector", PIN_STRUCT, #TYPE)
static const native_definition_binding BINDINGS[] = {
    NATIVE_CONVERSION_SCALARS(SCALAR) NATIVE_CONVERSION_VECTORS(VECTOR)
};
const native_definition_pack NATIVE_CONVERSION_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS)
};
