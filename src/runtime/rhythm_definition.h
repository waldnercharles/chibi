#ifndef CHIBI_GAME_RHYTHM_DEFINITION_H
#define CHIBI_GAME_RHYTHM_DEFINITION_H
#include "runtime/native_definitions.h"
#define GAME_RHYTHM_NAME "Scene Rhythm"
#define GAME_RHYTHM_VALUE(X, NAME)                                             \
    X(NAME,                                                                    \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_F64,                                                                 \
      "double",                                                                \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define GAME_RHYTHM_OUTPUTS(X)                                                 \
    GAME_RHYTHM_VALUE(X, "Phase")                                              \
    GAME_RHYTHM_VALUE(X, "Pulse")                                              \
    GAME_RHYTHM_VALUE(X, "Period")                                             \
    GAME_RHYTHM_VALUE(X, "Duration")
extern const native_definition_pack GAME_RHYTHM_DEFINITION_PACK;

/* Shared authoring presentation for these native declarations. */
#define GAME_RHYTHM_PRESENTATION                                               \
    .category = "Time", .keywords = "beat tempo phase pulse rhythm music",     \
    .description = "Reads the scene rhythm clock, 120 BPM without one; Phase " \
                   "runs 0 to 1 per beat and Pulse fades from 1."

#endif
