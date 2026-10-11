#include "runtime/audio_definition.h"

#include "runtime/audio.h"

static const native_parameter_definition TONE_IN[] = { AUDIO_TONE_INPUTS(NATIVE_PORT) };
static const native_parameter_definition SOUND_IN[] = { AUDIO_SOUND_INPUTS(NATIVE_PORT) };
static const native_parameter_definition MUSIC_IN[] = { AUDIO_MUSIC_INPUTS(NATIVE_PORT) };
static const native_parameter_definition FADE_IN[] = { AUDIO_FADE_INPUTS(NATIVE_PORT) };
static const native_parameter_definition VOLUME_IN[] = { AUDIO_VOLUME_INPUTS(NATIVE_PORT) };
static const native_parameter_definition OUTPUTS[] = { AUDIO_OUTPUTS(NATIVE_PORT) };
#define ACTION(KEY, SOURCE, INPUTS)                                            \
    { .canonical_name = KEY##_NAME,                                            \
      KEY##_PRESENTATION,                                                      \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .inputs = INPUTS,                                                        \
      .input_count = NATIVE_COUNT(INPUTS),                                     \
      .outputs = OUTPUTS,                                                      \
      .output_count = NATIVE_COUNT(OUTPUTS),                                   \
      .completion_output = 0,                                                  \
      .effects = SCRIPT_EFFECT_HOST }
static const native_capability_definition DEFINITIONS[] = {
    ACTION(AUDIO_TONE, "play_tone", TONE_IN),
    ACTION(AUDIO_SOUND, "play_sound", SOUND_IN),
    ACTION(AUDIO_MUSIC, "play_music", MUSIC_IN),
    ACTION(AUDIO_STOP_MUSIC, "stop_music", FADE_IN),
    ACTION(AUDIO_MASTER_VOLUME, "set_master_volume", VOLUME_IN),
    ACTION(AUDIO_MUSIC_VOLUME, "set_music_volume", VOLUME_IN),
    ACTION(AUDIO_SOUND_VOLUME, "set_sound_volume", VOLUME_IN),
};
#define BIND(KEY)                                                              \
    {                                                                          \
        .canonical_name = KEY##_NAME, .implementation = {                      \
            .call = &KEY##_CALL,                                               \
            .access = &AUDIO_ACCESS                                            \
        }                                                                      \
    }
static const native_definition_binding BINDINGS[] = {
    BIND(AUDIO_TONE),          BIND(AUDIO_SOUND),
    BIND(AUDIO_MUSIC),         BIND(AUDIO_STOP_MUSIC),
    BIND(AUDIO_MASTER_VOLUME), BIND(AUDIO_MUSIC_VOLUME),
    BIND(AUDIO_SOUND_VOLUME),
};
const native_definition_pack AUDIO_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
