#include "runtime/arrays.h"

#define ARRAYS(KERNEL, NAME, SOURCE, INPUTS, OUTPUTS, VARIADIC)                           \
    static const native_parameter_definition KERNEL##_inputs[] = { INPUTS(NATIVE_PORT) }; \
    static const native_parameter_definition KERNEL##_outputs[] = { OUTPUTS(NATIVE_PORT) };
NATIVE_ARRAY_OPERATIONS(ARRAYS)
#undef ARRAYS
#define DEFINITION(KERNEL, NAME, SOURCE, INPUTS, OUTPUTS, VARIADIC)            \
    { .canonical_name = NAME,                                                  \
      NATIVE_ARRAY_##KERNEL##_PRESENTATION,                                    \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                      \
      .inputs = KERNEL##_inputs,                                               \
      .input_count = NATIVE_COUNT(KERNEL##_inputs),                            \
      .outputs = KERNEL##_outputs,                                             \
      .output_count = NATIVE_COUNT(KERNEL##_outputs),                          \
      .completion_output = -1,                                                 \
      .pure = true,                                                            \
      .variadic = VARIADIC },
static const native_capability_definition DEFINITIONS[] = {
    NATIVE_ARRAY_OPERATIONS(DEFINITION)
};
#undef DEFINITION
#define BINDING(KERNEL, NAME, SOURCE, INPUTS, OUTPUTS, VARIADIC)               \
    { .canonical_name = NAME,                                                  \
      .implementation = { .call = &NATIVE_ARRAY_##KERNEL##_CALL,               \
                          .access = &NATIVE_ARRAY_ACCESS } },
static const native_definition_binding BINDINGS[] = { NATIVE_ARRAY_OPERATIONS(BINDING) };
#undef BINDING
const native_definition_pack NATIVE_ARRAY_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
