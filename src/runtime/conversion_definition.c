#include "runtime/conversions.h"
#define PORT(NAME, KIND, STORAGE, TYPE_NAME, CONTAINER, FLAGS, GROUP, REFERENCE, OWNER, VALUE) \
    { .name = NAME,                                                                            \
      .kind = KIND,                                                                            \
      .storage = STORAGE,                                                                      \
      .type_name = TYPE_NAME,                                                                  \
      .container = CONTAINER,                                                                  \
      .type_flags = FLAGS,                                                                     \
      .generic_group = GROUP,                                                                  \
      .reference_kind = REFERENCE,                                                             \
      .default_owner = OWNER,                                                                  \
      .has_default = true,                                                                     \
      .default_value = VALUE },
#define SCALAR(KEY, NAME, SOURCE, INPUT, INPUT_NAME, RESULT, OUTPUT, OUTPUT_NAME) \
    static const native_parameter_definition KEY##_inputs[] = {                   \
        NATIVE_CONVERSION_PORT(PORT, "Value", INPUT, INPUT_NAME)                  \
    };                                                                            \
    static const native_parameter_definition KEY##_outputs[] = {                  \
        NATIVE_CONVERSION_PORT(PORT, RESULT, OUTPUT, OUTPUT_NAME)                 \
    };
#define VECTOR(TYPE, WIDTH, NAME, SOURCE, DESCRIPTION)                         \
    SCALAR(Splat_##TYPE, NAME, SOURCE, PIN_F64, "double", "Vector", PIN_STRUCT, #TYPE)
NATIVE_CONVERSION_SCALARS(SCALAR)
NATIVE_CONVERSION_VECTORS(VECTOR)
#undef SCALAR
#undef VECTOR
#define COUNT(A) (sizeof(A) / sizeof *(A))
#define SCALAR(KEY, NAME, SOURCE, INPUT, INPUT_NAME, RESULT, OUTPUT, OUTPUT_NAME) \
    { .canonical_name = NAME,                                                     \
      .source_name = SOURCE,                                                      \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                         \
      .inputs = KEY##_inputs,                                                     \
      .input_count = COUNT(KEY##_inputs),                                         \
      .outputs = KEY##_outputs,                                                   \
      .output_count = COUNT(KEY##_outputs),                                       \
      .completion_output = -1,                                                    \
      .pure = true },
#define VECTOR(TYPE, WIDTH, NAME, SOURCE, DESCRIPTION)                         \
    SCALAR(Splat_##TYPE, NAME, SOURCE, PIN_F64, "double", "Vector", PIN_STRUCT, #TYPE)
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
    SCALAR(Splat_##TYPE, NAME, SOURCE, PIN_F64, "double", "Vector", PIN_STRUCT, #TYPE)
static const native_definition_binding BINDINGS[] = {
    NATIVE_CONVERSION_SCALARS(SCALAR) NATIVE_CONVERSION_VECTORS(VECTOR)
};
const native_definition_pack NATIVE_CONVERSION_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS)
};
