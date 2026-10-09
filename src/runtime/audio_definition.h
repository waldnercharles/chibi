#ifndef CHIBI_GAME_AUDIO_DEFINITION_H
#define CHIBI_GAME_AUDIO_DEFINITION_H

#include "runtime/native_definitions.h"

#define GAME_AUDIO_TONE_NAME "Play Tone"
#define GAME_AUDIO_SOUND_NAME "Play Sound"
#define GAME_AUDIO_MUSIC_NAME "Play Music"
#define GAME_AUDIO_STOP_MUSIC_NAME "Stop Music"
#define GAME_AUDIO_MASTER_VOLUME_NAME "Set Master Volume"
#define GAME_AUDIO_MUSIC_VOLUME_NAME "Set Music Volume"
#define GAME_AUDIO_SOUND_VOLUME_NAME "Set Sound Volume"
#define GAME_AUDIO_PORT(X, NAME, KIND, STORAGE, TYPE_NAME, VALUE)              \
    X(NAME, KIND, STORAGE, TYPE_NAME, PIN_CONTAINER_SINGLE, 0, 0, SCRIPT_REFERENCE_NONE, false, VALUE)
#define GAME_AUDIO_EXEC(X, NAME)                                               \
    GAME_AUDIO_PORT(X, NAME, NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, ((pin_value){ 0 }))
#define GAME_AUDIO_FLOAT(X, NAME, VALUE)                                       \
    GAME_AUDIO_PORT(                                                           \
        X,                                                                     \
        NAME,                                                                  \
        NATIVE_PARAMETER_DATA,                                                 \
        PIN_F32,                                                               \
        "float",                                                               \
        ((pin_value){ .type = PIN_F32, .f = VALUE })                           \
    )
#define GAME_AUDIO_NAME(X, NAME)                                               \
    GAME_AUDIO_PORT(X, NAME, NATIVE_PARAMETER_DATA, PIN_STR, NULL, ((pin_value){ .type = PIN_STR }))
#define GAME_AUDIO_TONE_INPUTS(X)                                              \
    GAME_AUDIO_EXEC(X, "Exec")                                                 \
    GAME_AUDIO_FLOAT(X, "Frequency", 440)                                      \
    GAME_AUDIO_FLOAT(X, "End Frequency", 220)                                  \
    GAME_AUDIO_FLOAT(X, "Duration", 0.15f)                                     \
    GAME_AUDIO_FLOAT(X, "Volume", 0.5f)                                        \
    GAME_AUDIO_FLOAT(X, "Pan", 0.5f)                                           \
    GAME_AUDIO_FLOAT(X, "Noise", 0)
#define GAME_AUDIO_SOUND_INPUTS(X)                                             \
    GAME_AUDIO_EXEC(X, "Exec")                                                 \
    GAME_AUDIO_NAME(X, "Sound")                                                \
    GAME_AUDIO_FLOAT(X, "Volume", 1)                                           \
    GAME_AUDIO_FLOAT(X, "Pan", 0.5f)
#define GAME_AUDIO_MUSIC_INPUTS(X)                                             \
    GAME_AUDIO_EXEC(X, "Exec")                                                 \
    GAME_AUDIO_NAME(X, "Music")                                                \
    GAME_AUDIO_FLOAT(X, "Fade", 0)
#define GAME_AUDIO_FADE_INPUTS(X)                                              \
    GAME_AUDIO_EXEC(X, "Exec")                                                 \
    GAME_AUDIO_FLOAT(X, "Fade", 0)
#define GAME_AUDIO_VOLUME_INPUTS(X)                                            \
    GAME_AUDIO_EXEC(X, "Exec")                                                 \
    GAME_AUDIO_FLOAT(X, "Volume", 1)
#define GAME_AUDIO_OUTPUTS(X) GAME_AUDIO_EXEC(X, "Then")

extern const native_definition_pack GAME_AUDIO_DEFINITION_PACK;

#define GAME_AUDIO_TONE_PRESENTATION                                            \
    .category = "Audio", .keywords = "synth beep sound effect cue noise sweep", \
    .description = "Queues a synthesized tone sweeping from Frequency to End "  \
                   "Frequency Hz over Duration seconds, mixed with Noise."
#define GAME_AUDIO_SOUND_PRESENTATION                                          \
    .category = "Audio", .keywords = "sample effect sfx wav ogg cue",          \
    .description = "Plays the sound file named Sound from content/sounds "     \
                   "once, on the sound bus."
#define GAME_AUDIO_MUSIC_PRESENTATION                                          \
    .category = "Audio", .keywords = "song track soundtrack loop bgm",         \
    .description = "Loops the sound file named Music from content/sounds on "  \
                   "the music bus, fading in over Fade seconds. The track "    \
                   "that already plays keeps playing."
#define GAME_AUDIO_STOP_MUSIC_PRESENTATION                                     \
    .category = "Audio", .keywords = "song track silence fade out",            \
    .description = "Fades the music out over Fade seconds."
#define GAME_AUDIO_MASTER_VOLUME_PRESENTATION                                  \
    .category = "Audio", .keywords = "bus mix loudness mute",                  \
    .description = "Scales every sound and the music, from 0 to 1."
#define GAME_AUDIO_MUSIC_VOLUME_PRESENTATION                                   \
    .category = "Audio", .keywords = "bus mix loudness mute song",             \
    .description = "Scales the music, from 0 to 1."
#define GAME_AUDIO_SOUND_VOLUME_PRESENTATION                                   \
    .category = "Audio", .keywords = "bus mix loudness mute effects sfx",      \
    .description = "Scales sounds and tones, from 0 to 1."

#endif
