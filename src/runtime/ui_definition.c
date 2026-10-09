#include "runtime/ui_definition.h"

#include "runtime/ui_native.h"

#define ARRAYS(KEY, NAME, SOURCE, INPUTS, OUTPUTS, ACTION)                             \
    static const native_parameter_definition KEY##_inputs[] = { INPUTS(NATIVE_PORT) }; \
    static const native_parameter_definition KEY##_outputs[] = { OUTPUTS(NATIVE_PORT) };
GAME_UI_OPERATIONS(ARRAYS)
#undef ARRAYS
#define DEFINITION(KEY, NAME, SOURCE, INPUTS, OUTPUTS, ACTION)                 \
    { .canonical_name = NAME,                                                  \
      GAME_UI_##KEY##_PRESENTATION,                                            \
      .source_name = SOURCE,                                                   \
      .kind = ACTION ? SCRIPT_CAPABILITY_ACTION : SCRIPT_CAPABILITY_FUNCTION,  \
      .inputs = KEY##_inputs,                                                  \
      .input_count = NATIVE_COUNT(KEY##_inputs),                               \
      .outputs = KEY##_outputs,                                                \
      .output_count = NATIVE_COUNT(KEY##_outputs),                             \
      .completion_output = ACTION ? 0 : -1,                                    \
      .pure = !ACTION },
static const native_capability_definition DEFINITIONS[] = { GAME_UI_OPERATIONS(DEFINITION) };
#undef DEFINITION
#define BINDING(KEY, NAME, SOURCE, INPUTS, OUTPUTS, ACTION)                    \
    { .canonical_name = NAME,                                                  \
      .implementation = { .call = &GAME_UI_##KEY##_CALL,                       \
                          .access = ACTION ? &GAME_UI_WRITE_ACCESS             \
                                           : &GAME_UI_READ_ACCESS } },
static const native_definition_binding BINDINGS[] = { GAME_UI_OPERATIONS(BINDING) };
#undef BINDING
const native_definition_pack GAME_UI_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
