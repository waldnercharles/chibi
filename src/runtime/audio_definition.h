#ifndef CHIBI_GAME_AUDIO_DEFINITION_H
#define CHIBI_GAME_AUDIO_DEFINITION_H

#include "runtime/native_definitions.h"

#define GAME_AUDIO_PLAY_NAME "Play Sound"
#define GAME_AUDIO_FLOAT(X, NAME, VALUE)                                       \
    X(NAME,                                                                    \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_F32,                                                                 \
      "float",                                                                 \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_F32, .f = VALUE }))
#define GAME_AUDIO_INPUTS(X)                                                   \
    X("Exec",                                                                  \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))                                                      \
    GAME_AUDIO_FLOAT(X, "Frequency", 440)                                      \
    GAME_AUDIO_FLOAT(X, "End Frequency", 220)                                  \
    GAME_AUDIO_FLOAT(X, "Duration", 0.15f)                                     \
    GAME_AUDIO_FLOAT(X, "Volume", 0.5f)                                        \
    GAME_AUDIO_FLOAT(X, "Pan", 0.5f)                                           \
    GAME_AUDIO_FLOAT(X, "Noise", 0)
#define GAME_AUDIO_OUTPUTS(X)                                                  \
    X("Then",                                                                  \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))

extern const native_definition_pack GAME_AUDIO_DEFINITION_PACK;

/* Shared authoring presentation for these native declarations. */
#define GAME_AUDIO_PLAY_PRESENTATION                                           \
    .category = "Audio", .keywords = "tone sound effect cue noise sweep",      \
    .description = "Queues a synthesized tone sweeping from Frequency to End " \
                   "Frequency Hz over Duration seconds, mixed with Noise."

#endif
