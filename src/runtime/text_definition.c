#include "runtime/text_format.h"

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
#define ARRAYS(KEY, NAME, SOURCE, INPUTS, OUTPUTS, VARIADIC)                    \
    static const native_parameter_definition KEY##_inputs[] = { INPUTS(PORT) }; \
    static const native_parameter_definition KEY##_outputs[] = { OUTPUTS(PORT) };
NATIVE_TEXT_OPERATIONS(ARRAYS)
#undef ARRAYS
#undef PORT
#define COUNT(A) (sizeof(A) / sizeof *(A))
#define DEFINITION(KEY, NAME, SOURCE, INPUTS, OUTPUTS, VARIADIC)               \
    { .canonical_name = NAME,                                                  \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                      \
      .inputs = KEY##_inputs,                                                  \
      .input_count = COUNT(KEY##_inputs),                                      \
      .outputs = KEY##_outputs,                                                \
      .output_count = COUNT(KEY##_outputs),                                    \
      .completion_output = -1,                                                 \
      .pure = true,                                                            \
      .variadic = VARIADIC,                                                    \
      .variadic_optional = VARIADIC },
static const native_capability_definition DEFINITIONS[] = {
    NATIVE_TEXT_OPERATIONS(DEFINITION)
};
#undef DEFINITION
#define BINDING(KEY, NAME, SOURCE, INPUTS, OUTPUTS, VARIADIC)                  \
    { .canonical_name = NAME,                                                  \
      .implementation = { .call = &NATIVE_##KEY##_CALL,                        \
                          .access = &NATIVE_TEXT_ACCESS } },
static const native_definition_binding BINDINGS[] = { NATIVE_TEXT_OPERATIONS(BINDING) };
#undef BINDING
const native_definition_pack NATIVE_TEXT_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS),
};
#undef COUNT
