#include "runtime/state_definition.h"
#define ARRAYS(KEY, NAME, SOURCE, STATE, INPUTS, OUTPUTS)                              \
    static const native_parameter_definition KEY##_inputs[] = { INPUTS(NATIVE_PORT) }; \
    static const native_parameter_definition KEY##_outputs[] = { OUTPUTS(NATIVE_PORT) };
NATIVE_STATE_OPERATIONS(ARRAYS)
#undef ARRAYS
#define DEFINITION(KEY, NAME, SOURCE, STATE, INPUTS, OUTPUTS)                  \
    { .canonical_name = NAME,                                                  \
      NATIVE_STATE_##KEY##_PRESENTATION,                                       \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .state_kind = STATE,                                                     \
      .inputs = KEY##_inputs,                                                  \
      .input_count = NATIVE_COUNT(KEY##_inputs),                               \
      .outputs = KEY##_outputs,                                                \
      .output_count = NATIVE_COUNT(KEY##_outputs),                             \
      .completion_output = STATE == NATIVE_STATE_RETRIGGER ? 0 : -1,           \
      .effects = STATE == NATIVE_STATE_RETRIGGER                               \
                   ? SCRIPT_EFFECT_SUSPEND                                     \
                   : (SCRIPT_EFFECT_WRITE |                                    \
                      (STATE == NATIVE_STATE_MULTI ? SCRIPT_EFFECT_RANDOM : 0)) },
static const native_capability_definition DEFINITIONS[] = {
    NATIVE_STATE_OPERATIONS(DEFINITION)
};
const native_definition_pack NATIVE_STATE_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS)
};
