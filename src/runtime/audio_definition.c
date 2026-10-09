#include "runtime/audio_definition.h"

#include "runtime/audio.h"

static const native_parameter_definition INPUTS[] = { GAME_AUDIO_INPUTS(NATIVE_PORT) };
static const native_parameter_definition OUTPUTS[] = { GAME_AUDIO_OUTPUTS(NATIVE_PORT) };
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = GAME_AUDIO_PLAY_NAME,
      GAME_AUDIO_PLAY_PRESENTATION,
      .source_name = "play_sound",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = INPUTS,
      .input_count = NATIVE_COUNT(INPUTS),
      .outputs = OUTPUTS,
      .output_count = NATIVE_COUNT(OUTPUTS),
      .completion_output = 0 }
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = GAME_AUDIO_PLAY_NAME,
      .implementation = { .call = &GAME_AUDIO_PLAY_CALL, .access = &GAME_AUDIO_ACCESS } }
};
const native_definition_pack GAME_AUDIO_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
