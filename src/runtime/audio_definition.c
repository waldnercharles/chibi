#include "runtime/audio_definition.h"

#include "runtime/audio.h"

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
static const native_parameter_definition INPUTS[] = { GAME_AUDIO_INPUTS(PORT) };
static const native_parameter_definition OUTPUTS[] = { GAME_AUDIO_OUTPUTS(PORT) };
#undef PORT
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = GAME_AUDIO_PLAY_NAME,
      GAME_AUDIO_PLAY_PRESENTATION,
      .source_name = "play_sound",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = INPUTS,
      .input_count = sizeof INPUTS / sizeof *INPUTS,
      .outputs = OUTPUTS,
      .output_count = sizeof OUTPUTS / sizeof *OUTPUTS,
      .completion_output = 0 }
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = GAME_AUDIO_PLAY_NAME,
      .implementation = { .call = &GAME_AUDIO_PLAY_CALL, .access = &GAME_AUDIO_ACCESS } }
};
const native_definition_pack GAME_AUDIO_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = sizeof DEFINITIONS / sizeof *DEFINITIONS,
    .bindings = BINDINGS,
    .binding_count = sizeof BINDINGS / sizeof *BINDINGS,
};
