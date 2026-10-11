#include "runtime/audio_definition.h"

#include "runtime/audio.h"

static const native_parameter_definition TONE_IN[] = { GAME_AUDIO_TONE_INPUTS(NATIVE_PORT) };
static const native_parameter_definition SOUND_IN[] = { GAME_AUDIO_SOUND_INPUTS(NATIVE_PORT) };
static const native_parameter_definition MUSIC_IN[] = { GAME_AUDIO_MUSIC_INPUTS(NATIVE_PORT) };
static const native_parameter_definition FADE_IN[] = { GAME_AUDIO_FADE_INPUTS(NATIVE_PORT) };
static const native_parameter_definition VOLUME_IN[] = {
    GAME_AUDIO_VOLUME_INPUTS(NATIVE_PORT)
};
static const native_parameter_definition OUTPUTS[] = { GAME_AUDIO_OUTPUTS(NATIVE_PORT) };
static const native_parameter_definition SOURCE_IN[] = {
    GAME_AUDIO_SOURCE_INPUTS(NATIVE_PORT)
};
static const native_parameter_definition ANALYSIS_OUT[] = {
    GAME_AUDIO_ANALYSIS_OUTPUTS(NATIVE_PORT)
};
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
    ACTION(GAME_AUDIO_TONE, "play_tone", TONE_IN),
    ACTION(GAME_AUDIO_SOUND, "play_sound", SOUND_IN),
    ACTION(GAME_AUDIO_MUSIC, "play_music", MUSIC_IN),
    ACTION(GAME_AUDIO_STOP_MUSIC, "stop_music", FADE_IN),
    ACTION(GAME_AUDIO_MASTER_VOLUME, "set_master_volume", VOLUME_IN),
    ACTION(GAME_AUDIO_MUSIC_VOLUME, "set_music_volume", VOLUME_IN),
    ACTION(GAME_AUDIO_SOUND_VOLUME, "set_sound_volume", VOLUME_IN),
    ACTION(GAME_AUDIO_SOURCE, "set_audio_source", SOURCE_IN),
    { .canonical_name = GAME_AUDIO_ANALYSIS_NAME,
      GAME_AUDIO_ANALYSIS_PRESENTATION,
      .source_name = "get_audio_analysis",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .outputs = ANALYSIS_OUT,
      .output_count = NATIVE_COUNT(ANALYSIS_OUT),
      .completion_output = -1,
      .effects = SCRIPT_EFFECT_READ },
};
#define BIND(KEY)                                                              \
    {                                                                          \
        .canonical_name = KEY##_NAME, .implementation = {                      \
            .call = &KEY##_CALL,                                               \
            .access = &GAME_AUDIO_ACCESS                                       \
        }                                                                      \
    }
static const native_definition_binding BINDINGS[] = {
    BIND(GAME_AUDIO_TONE),
    BIND(GAME_AUDIO_SOUND),
    BIND(GAME_AUDIO_MUSIC),
    BIND(GAME_AUDIO_STOP_MUSIC),
    BIND(GAME_AUDIO_MASTER_VOLUME),
    BIND(GAME_AUDIO_MUSIC_VOLUME),
    BIND(GAME_AUDIO_SOUND_VOLUME),
    BIND(GAME_AUDIO_SOURCE),
    { .canonical_name = GAME_AUDIO_ANALYSIS_NAME,
      .implementation = { .call = &GAME_AUDIO_ANALYSIS_CALL,
                          .access = &GAME_AUDIO_READ_ACCESS } },
};
const native_definition_pack GAME_AUDIO_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
