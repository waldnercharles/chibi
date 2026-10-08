#include "runtime/rhythm_definition.h"

#include "runtime/rhythm.h"
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
static const native_parameter_definition OUTPUTS[] = { GAME_RHYTHM_OUTPUTS(PORT) };
#undef PORT
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = GAME_RHYTHM_NAME,
      GAME_RHYTHM_PRESENTATION,
      .source_name = "scene_rhythm",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .outputs = OUTPUTS,
      .output_count = sizeof OUTPUTS / sizeof *OUTPUTS,
      .completion_output = -1 }
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = GAME_RHYTHM_NAME,
      .implementation = { .call = &GAME_RHYTHM_CALL, .access = &GAME_RHYTHM_ACCESS } }
};
const native_definition_pack GAME_RHYTHM_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = sizeof DEFINITIONS / sizeof *DEFINITIONS,
    .bindings = BINDINGS,
    .binding_count = sizeof BINDINGS / sizeof *BINDINGS,
};
