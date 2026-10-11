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
#define ARRAYS(KEY, NAME, OUTPUTS)                                             \
    static const native_parameter_definition KEY##_outputs[] = { OUTPUTS(PORT) };
GAME_INPUT_OPERATIONS(ARRAYS)
#undef ARRAYS
static const native_parameter_definition CONTROLLER_INPUTS[] = {
    GAME_INPUT_CONTROLLER_CONNECTED_INPUTS(PORT)
};
static const native_parameter_definition CONTROLLER_OUTPUTS[] = {
    GAME_INPUT_CONTROLLER_CONNECTED_OUTPUTS(PORT)
};
#undef PORT
#define COUNT(A) (sizeof(A) / sizeof *(A))
#define DEFINITION(KEY, NAME, OUTPUTS)                                         \
    { .canonical_name = NAME,                                                  \
      GAME_INPUT_##KEY##_PRESENTATION,                                         \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                      \
      .pure = true,                                                            \
      .effects = SCRIPT_EFFECT_READ,                                           \
      .completion_output = -1,                                                 \
      .inputs = INPUTS,                                                        \
      .input_count = COUNT(INPUTS),                                            \
      .outputs = KEY##_outputs,                                                \
      .output_count = COUNT(KEY##_outputs) },
static const native_capability_definition DEFINITIONS[] = {
    GAME_INPUT_OPERATIONS(DEFINITION){ .canonical_name = GAME_INPUT_CONTROLLER_CONNECTED_NAME,
                                       .source_name = "controller_connected",
                                       GAME_INPUT_CONTROLLER_CONNECTED_PRESENTATION,
                                       .kind = SCRIPT_CAPABILITY_FUNCTION,
                                       .pure = true,
                                       .effects = SCRIPT_EFFECT_READ,
                                       .completion_output = -1,
                                       .inputs = CONTROLLER_INPUTS,
                                       .input_count = COUNT(CONTROLLER_INPUTS),
                                       .outputs = CONTROLLER_OUTPUTS,
                                       .output_count = COUNT(CONTROLLER_OUTPUTS) },
};
#undef DEFINITION
#define BINDING(KEY, NAME, OUTPUTS)                                            \
    { .canonical_name = NAME,                                                  \
      .implementation = { .call = &GAME_INPUT_##KEY##_CALL,                    \
                          .access = &GAME_INPUT_READ_ACCESS } },
static const native_definition_binding BINDINGS[] = {
    GAME_INPUT_OPERATIONS(BINDING){
        .canonical_name = GAME_INPUT_CONTROLLER_CONNECTED_NAME,
        .implementation = { .call = &GAME_INPUT_CONTROLLER_CONNECTED_CALL,
                            .access = &GAME_INPUT_READ_ACCESS } },
};
#undef BINDING
const native_definition_pack GAME_INPUT_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS),
};
#undef COUNT
