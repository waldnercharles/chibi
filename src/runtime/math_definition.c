#include "runtime/math_native.h"
#define PORT(NAME, KIND, TYPE, NOMINAL, CONTAINER, FLAGS, GROUP, REFERENCE, OWNER, VALUE) \
    { .name = NAME,                                                                       \
      .kind = KIND,                                                                       \
      .storage = TYPE,                                                                    \
      .type_name = NOMINAL,                                                               \
      .container = CONTAINER,                                                             \
      .type_flags = FLAGS,                                                                \
      .generic_group = GROUP,                                                             \
      .reference_kind = REFERENCE,                                                        \
      .has_default = true,                                                                \
      .default_owner = OWNER,                                                             \
      .default_value = VALUE },
static const native_parameter_definition UNARY_INPUTS[] = { NATIVE_MATH_UNARY_INPUTS(PORT) };
static const native_parameter_definition OUTPUTS[] = { NATIVE_MATH_RESULT_OUTPUTS(PORT) };
static const native_parameter_definition SPRING_INPUTS[] = {
    NATIVE_MATH_SPRING_INPUTS(PORT)
};
static const native_parameter_definition SPRING_OUTPUTS[] = {
    NATIVE_MATH_SPRING_OUTPUTS(PORT)
};
#define BINARY(KEY, NAME, SOURCE, FN, INPUTS, KEYWORDS, DESCRIPTION)           \
    static const native_parameter_definition KEY##_inputs[] = { INPUTS(PORT) };
#define VALUE(KEY, NAME, SOURCE, INPUTS, RANDOM)                               \
    static const native_parameter_definition KEY##_inputs[] = { INPUTS(PORT) };
NATIVE_MATH_BINARIES(BINARY)
NATIVE_MATH_VALUES(VALUE)
#undef BINARY
#undef VALUE
#undef PORT
#define COUNT(A) (sizeof(A) / sizeof *(A))
#define DEFINITION(KEY, NAME, SOURCE, INPUTS, OUT, RANDOM)                     \
    { .canonical_name = NAME,                                                  \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                      \
      .pure = true,                                                            \
      .effects = RANDOM ? SCRIPT_EFFECT_RANDOM : 0,                            \
      .completion_output = -1,                                                 \
      .inputs = INPUTS,                                                        \
      .input_count = COUNT(INPUTS),                                            \
      .outputs = OUT,                                                          \
      .output_count = COUNT(OUT) },
#define UNARY(KEY, NAME, SOURCE, FN, DESCRIPTION)                              \
    DEFINITION(KEY, NAME, SOURCE, UNARY_INPUTS, OUTPUTS, false)
#define BINARY(KEY, NAME, SOURCE, FN, INPUTS, KEYWORDS, DESCRIPTION)           \
    DEFINITION(KEY, NAME, SOURCE, KEY##_inputs, OUTPUTS, false)
#define VALUE(KEY, NAME, SOURCE, INPUTS, RANDOM)                               \
    DEFINITION(KEY, NAME, SOURCE, KEY##_inputs, OUTPUTS, RANDOM)
static const native_capability_definition DEFINITIONS[] = {
    NATIVE_MATH_UNARIES(UNARY) NATIVE_MATH_BINARIES(BINARY)
        NATIVE_MATH_VALUES(VALUE) DEFINITION(
            Advance_Damped_Spring,
            NATIVE_MATH_Advance_Damped_Spring_NAME,
            NATIVE_MATH_Advance_Damped_Spring_SOURCE,
            SPRING_INPUTS,
            SPRING_OUTPUTS,
            false
        )
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
#define UNARY(KEY, NAME, SOURCE, FN, DESCRIPTION) BINDING(KEY, NAME, false)
#define BINARY(KEY, NAME, SOURCE, FN, INPUTS, KEYWORDS, DESCRIPTION)           \
    BINDING(KEY, NAME, false)
#define VALUE(KEY, NAME, SOURCE, INPUTS, RANDOM) BINDING(KEY, NAME, RANDOM)
static const native_definition_binding BINDINGS[] = {
    NATIVE_MATH_UNARIES(UNARY) NATIVE_MATH_BINARIES(BINARY) NATIVE_MATH_VALUES(VALUE)
        BINDING(Advance_Damped_Spring, NATIVE_MATH_Advance_Damped_Spring_NAME, false)
};
#undef UNARY
#undef BINARY
#undef VALUE
#undef BINDING
const native_definition_pack NATIVE_MATH_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS)
};
#undef COUNT
