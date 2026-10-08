#include "runtime/state_definition.h"
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
#define ARRAYS(KEY, NAME, SOURCE, STATE, INPUTS, OUTPUTS)                       \
    static const native_parameter_definition KEY##_inputs[] = { INPUTS(PORT) }; \
    static const native_parameter_definition KEY##_outputs[] = { OUTPUTS(PORT) };
NATIVE_STATE_OPERATIONS(ARRAYS)
#undef ARRAYS
#undef PORT
#define COUNT(A) (sizeof(A) / sizeof *(A))
#define DEFINITION(KEY, NAME, SOURCE, STATE, INPUTS, OUTPUTS)                  \
    { .canonical_name = NAME,                                                  \
      NATIVE_STATE_##KEY##_PRESENTATION,                                       \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .state_kind = STATE,                                                     \
      .inputs = KEY##_inputs,                                                  \
      .input_count = COUNT(KEY##_inputs),                                      \
      .outputs = KEY##_outputs,                                                \
      .output_count = COUNT(KEY##_outputs),                                    \
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
    .definition_count = COUNT(DEFINITIONS)
};
