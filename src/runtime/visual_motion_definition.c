#include "runtime/visual_motion_definition.h"

#include "runtime/visual_motion_native.h"

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
#define COUNT(A) (sizeof(A) / sizeof *(A))
static const native_parameter_definition TRIGGER_IN[] = {
    GAME_VISUAL_REACTION_TRIGGER_INPUTS(PORT)
};
static const native_parameter_definition TRIGGER_OUT[] = {
    GAME_VISUAL_REACTION_TRIGGER_OUTPUTS(PORT)
};
#undef PORT
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = GAME_VISUAL_REACTION_TRIGGER_NAME,
      GAME_VISUAL_REACTION_TRIGGER_PRESENTATION,
      .source_name = GAME_VISUAL_REACTION_TRIGGER_SOURCE,
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = TRIGGER_IN,
      .input_count = COUNT(TRIGGER_IN),
      .outputs = TRIGGER_OUT,
      .output_count = COUNT(TRIGGER_OUT),
      .completion_output = 0,
      .pure = false },
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = GAME_VISUAL_REACTION_TRIGGER_NAME,
      .implementation = { .call = &GAME_VISUAL_REACTION_CALL,
                          .access = &GAME_VISUAL_REACTION_ACCESS } },
};
const native_definition_pack GAME_VISUAL_REACTION_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS)
};
