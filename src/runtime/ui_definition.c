#include "runtime/ui_definition.h"

#include "runtime/ui_native.h"

#define ARRAYS(KEY, NAME, SOURCE, INPUTS, OUTPUTS, ACTION)                                     \
    static const native_parameter_definition KEY##_inputs[] = { INPUTS(NATIVE_CSTRING_PORT) }; \
    static const native_parameter_definition KEY##_outputs[] = { OUTPUTS(NATIVE_PORT) };
UI_OPERATIONS(ARRAYS)
#undef ARRAYS
#define DEFINITION(KEY, NAME, SOURCE, INPUTS, OUTPUTS, ACTION)                 \
    { .canonical_name = NAME,                                                  \
      UI_##KEY##_PRESENTATION,                                                 \
      .source_name = SOURCE,                                                   \
      .kind = ACTION ? SCRIPT_CAPABILITY_ACTION : SCRIPT_CAPABILITY_FUNCTION,  \
      .inputs = KEY##_inputs,                                                  \
      .input_count = NATIVE_COUNT(KEY##_inputs),                               \
      .outputs = KEY##_outputs,                                                \
      .output_count = NATIVE_COUNT(KEY##_outputs),                             \
      .completion_output = ACTION ? 0 : -1,                                    \
      .pure = !ACTION },
static const native_capability_definition DEFINITIONS[] = { UI_OPERATIONS(DEFINITION) };
#undef DEFINITION
#define BINDING(KEY, NAME, SOURCE, INPUTS, OUTPUTS, ACTION)                    \
    { .canonical_name = NAME,                                                  \
      .implementation = { .call = &UI_##KEY##_CALL,                            \
                          .access = ACTION ? &UI_WRITE_ACCESS : &UI_READ_ACCESS } },
static const native_definition_binding BINDINGS[] = { UI_OPERATIONS(BINDING) };
#undef BINDING
const native_definition_pack UI_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
