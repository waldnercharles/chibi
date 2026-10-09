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
static const native_parameter_definition TONE_IN[] = { GAME_AUDIO_TONE_INPUTS(PORT) };
static const native_parameter_definition SOUND_IN[] = { GAME_AUDIO_SOUND_INPUTS(PORT) };
static const native_parameter_definition MUSIC_IN[] = { GAME_AUDIO_MUSIC_INPUTS(PORT) };
static const native_parameter_definition FADE_IN[] = { GAME_AUDIO_FADE_INPUTS(PORT) };
static const native_parameter_definition VOLUME_IN[] = { GAME_AUDIO_VOLUME_INPUTS(PORT) };
static const native_parameter_definition OUTPUTS[] = { GAME_AUDIO_OUTPUTS(PORT) };
#undef PORT
#define COUNT(ARRAY) ((int)(sizeof ARRAY / sizeof *ARRAY))
#define ACTION(KEY, SOURCE, INPUTS)                                            \
    { .canonical_name = KEY##_NAME,                                            \
      KEY##_PRESENTATION,                                                      \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .inputs = INPUTS,                                                        \
      .input_count = COUNT(INPUTS),                                            \
      .outputs = OUTPUTS,                                                      \
      .output_count = COUNT(OUTPUTS),                                          \
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
};
#define BIND(KEY)                                                              \
    {                                                                          \
        .canonical_name = KEY##_NAME, .implementation = {                      \
            .call = &KEY##_CALL,                                               \
            .access = &GAME_AUDIO_ACCESS                                       \
        }                                                                      \
    }
static const native_definition_binding BINDINGS[] = {
    BIND(GAME_AUDIO_TONE),          BIND(GAME_AUDIO_SOUND),
    BIND(GAME_AUDIO_MUSIC),         BIND(GAME_AUDIO_STOP_MUSIC),
    BIND(GAME_AUDIO_MASTER_VOLUME), BIND(GAME_AUDIO_MUSIC_VOLUME),
    BIND(GAME_AUDIO_SOUND_VOLUME),
};
const native_definition_pack GAME_AUDIO_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS),
};
