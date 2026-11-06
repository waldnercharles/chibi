#ifndef CHIBI_AUDIO_DEFINITION_H
#define CHIBI_AUDIO_DEFINITION_H

#include "runtime/native_definitions.h"

#define AUDIO_TONE_NAME "Play Tone"
#define AUDIO_SOUND_NAME "Play Sound"
#define AUDIO_MUSIC_NAME "Play Music"
#define AUDIO_STOP_MUSIC_NAME "Stop Music"
#define AUDIO_MASTER_VOLUME_NAME "Set Master Volume"
#define AUDIO_MUSIC_VOLUME_NAME "Set Music Volume"
#define AUDIO_SOUND_VOLUME_NAME "Set Sound Volume"
#define AUDIO_SOURCE_NAME "Set Audio Source"
#define AUDIO_ANALYSIS_NAME "Get Audio Analysis"
#define AUDIO_PORT(X, NAME, KIND, STORAGE, TYPE_NAME, VALUE)                   \
    X(NAME, KIND, STORAGE, TYPE_NAME, PIN_CONTAINER_SINGLE, 0, 0, SCRIPT_REFERENCE_NONE, false, VALUE)
#define AUDIO_EXEC(X, NAME)                                                    \
    AUDIO_PORT(X, NAME, NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, ((pin_value){ 0 }))
#define AUDIO_FLOAT(X, NAME, VALUE)                                            \
    AUDIO_PORT(                                                                \
        X,                                                                     \
        NAME,                                                                  \
        NATIVE_PARAMETER_DATA,                                                 \
        PIN_F32,                                                               \
        "number",                                                              \
        ((pin_value){ .type = PIN_F32, .f = VALUE })                           \
    )
#define AUDIO_NAME(X, NAME)                                                    \
    AUDIO_PORT(X, NAME, NATIVE_PARAMETER_DATA, PIN_STR, NULL, ((pin_value){ .type = PIN_STR }))
#define AUDIO_TONE_INPUTS(X)                                                   \
    AUDIO_EXEC(X, "Exec")                                                      \
    AUDIO_FLOAT(X, "Frequency", 440)                                           \
    AUDIO_FLOAT(X, "End Frequency", 220)                                       \
    AUDIO_FLOAT(X, "Duration", 0.15f)                                          \
    AUDIO_FLOAT(X, "Volume", 0.5f)                                             \
    AUDIO_FLOAT(X, "Pan", 0.5f)                                                \
    AUDIO_FLOAT(X, "Noise", 0)
#define AUDIO_SOUND_INPUTS(X)                                                  \
    AUDIO_EXEC(X, "Exec")                                                      \
    AUDIO_NAME(X, "Sound")                                                     \
    AUDIO_FLOAT(X, "Volume", 1)                                                \
    AUDIO_FLOAT(X, "Pan", 0.5f)
#define AUDIO_MUSIC_INPUTS(X)                                                  \
    AUDIO_EXEC(X, "Exec")                                                      \
    AUDIO_NAME(X, "Music")                                                     \
    AUDIO_FLOAT(X, "Fade", 0)
#define AUDIO_FADE_INPUTS(X)                                                   \
    AUDIO_EXEC(X, "Exec")                                                      \
    AUDIO_FLOAT(X, "Fade", 0)
#define AUDIO_VOLUME_INPUTS(X)                                                 \
    AUDIO_EXEC(X, "Exec")                                                      \
    AUDIO_FLOAT(X, "Volume", 1)
#define AUDIO_OUTPUTS(X) AUDIO_EXEC(X, "Then")
#define AUDIO_INT(X, NAME)                                                     \
    AUDIO_PORT(X, NAME, NATIVE_PARAMETER_DATA, PIN_I32, "int", ((pin_value){ .type = PIN_I32 }))
#define AUDIO_SOURCE_INPUTS(X) AUDIO_EXEC(X, "Exec") AUDIO_INT(X, "Source")
#define AUDIO_ANALYSIS_OUTPUTS(X)                                                                       \
    AUDIO_FLOAT(X, "Level", 0)                                                                          \
    AUDIO_FLOAT(X, "Bass", 0)                                                                           \
    AUDIO_FLOAT(X, "Mid", 0)                                                                            \
    AUDIO_FLOAT(X, "Treble", 0)                                                                         \
    AUDIO_FLOAT(X, "Onset", 0)                                                                          \
    AUDIO_PORT(X, "Active", NATIVE_PARAMETER_DATA, PIN_BOOL, "bool", ((pin_value){ .type = PIN_BOOL })) \
    AUDIO_INT(X, "Source")                                                                              \
    AUDIO_INT(X, "Status")

extern const native_definition_pack AUDIO_DEFINITION_PACK;

#define AUDIO_TONE_PRESENTATION                                                 \
    .category = "Audio", .keywords = "synth beep sound effect cue noise sweep", \
    .description = "Queues a synthesized tone sweeping from Frequency to End "  \
                   "Frequency Hz over Duration seconds, mixed with Noise."
#define AUDIO_SOUND_PRESENTATION                                               \
    .category = "Audio", .keywords = "sample effect sfx wav ogg cue",          \
    .description = "Plays the sound file named Sound from content/sounds "     \
                   "once, on the sound bus."
#define AUDIO_MUSIC_PRESENTATION                                               \
    .category = "Audio", .keywords = "song track soundtrack loop bgm",         \
    .description = "Loops the sound file named Music from content/sounds on "  \
                   "the music bus, fading in over Fade seconds. The track "    \
                   "that already plays keeps playing."
#define AUDIO_STOP_MUSIC_PRESENTATION                                          \
    .category = "Audio", .keywords = "song track silence fade out",            \
    .description = "Fades the music out over Fade seconds."
#define AUDIO_MASTER_VOLUME_PRESENTATION                                       \
    .category = "Audio", .keywords = "bus mix loudness mute",                  \
    .description = "Scales every sound and the music, from 0 to 1."
#define AUDIO_MUSIC_VOLUME_PRESENTATION                                        \
    .category = "Audio", .keywords = "bus mix loudness mute song",             \
    .description = "Scales the music, from 0 to 1."
#define AUDIO_SOUND_VOLUME_PRESENTATION                                        \
    .category = "Audio", .keywords = "bus mix loudness mute effects sfx",      \
    .description = "Scales sounds and tones, from 0 to 1."
#define AUDIO_SOURCE_PRESENTATION                                              \
    .category = "Audio", .keywords = "capture loopback music microphone",      \
    .description = "Selects analysis of game output (0) or external audio "    \
                   "(1). External audio requires a user-selected browser "     \
                   "source or a native loopback input."
#define AUDIO_ANALYSIS_PRESENTATION                                            \
    .category = "Audio", .keywords = "spectrum envelope bass onset music",     \
    .description = "Reads normalized measured audio levels. Status is idle "   \
                   "(0), waiting (1), ready (2), unavailable (3), denied "     \
                   "or cancelled (4), or ended (5). Active means PCM is "      \
                   "arriving while playback is running; silence is zero."

#endif
