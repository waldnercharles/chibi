#include "runtime/input_definition.h"

#include "runtime/engine_input.h"

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
static const native_parameter_definition INPUTS[] = { GAME_INPUT_ACTION_INPUTS(PORT) };
#define ARRAYS(KEY, NAME, SOURCE, OUTPUTS)                                     \
    static const native_parameter_definition KEY##_outputs[] = { OUTPUTS(PORT) };
GAME_INPUT_OPERATIONS(ARRAYS)
#undef ARRAYS
#undef PORT
#define COUNT(A) (sizeof(A) / sizeof *(A))
#define DEFINITION(KEY, NAME, SOURCE, OUTPUTS)                                 \
    { .canonical_name = NAME,                                                  \
      GAME_INPUT_##KEY##_PRESENTATION,                                         \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                      \
      .pure = true,                                                            \
      .effects = SCRIPT_EFFECT_READ,                                           \
      .completion_output = -1,                                                 \
      .inputs = INPUTS,                                                        \
      .input_count = COUNT(INPUTS),                                            \
      .outputs = KEY##_outputs,                                                \
      .output_count = COUNT(KEY##_outputs) },
static const native_capability_definition DEFINITIONS[] = {
    GAME_INPUT_OPERATIONS(DEFINITION)
};
#undef DEFINITION
#define BINDING(KEY, NAME, SOURCE, OUTPUTS)                                    \
    { .canonical_name = NAME,                                                  \
      .implementation = { .call = &GAME_INPUT_##KEY##_CALL,                    \
                          .access = &GAME_INPUT_READ_ACCESS } },
static const native_definition_binding BINDINGS[] = { GAME_INPUT_OPERATIONS(BINDING) };
#undef BINDING
const native_definition_pack GAME_INPUT_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS),
};
#undef COUNT
