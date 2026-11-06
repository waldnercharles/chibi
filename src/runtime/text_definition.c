#include "runtime/text_format.h"

#define ARRAYS(KEY, NAME, SOURCE, INPUTS, OUTPUTS, VARIADIC)                           \
    static const native_parameter_definition KEY##_inputs[] = { INPUTS(NATIVE_PORT) }; \
    static const native_parameter_definition KEY##_outputs[] = { OUTPUTS(NATIVE_PORT) };
NATIVE_TEXT_OPERATIONS(ARRAYS)
#undef ARRAYS
#define DEFINITION(KEY, NAME, SOURCE, INPUTS, OUTPUTS, VARIADIC)               \
    { .canonical_name = NAME,                                                  \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                      \
      .inputs = KEY##_inputs,                                                  \
      .input_count = NATIVE_COUNT(KEY##_inputs),                               \
      .outputs = KEY##_outputs,                                                \
      .output_count = NATIVE_COUNT(KEY##_outputs),                             \
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
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};

#define COUNT(KEY, NAME, SOURCE, INPUTS, OUTPUTS, VARIADIC) +1
enum
{
    COMPILER_TEXT_COUNT = 0 NATIVE_COMPILER_TEXT_OPERATIONS(COUNT)
};
#undef COUNT
const native_definition_pack NATIVE_COMPILER_TEXT_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = COMPILER_TEXT_COUNT,
    .bindings = BINDINGS,
    .binding_count = COMPILER_TEXT_COUNT,
};
