#ifndef CHIBI_RHYTHM_DEFINITION_H
#define CHIBI_RHYTHM_DEFINITION_H
#include "runtime/native_definitions.h"
#define RHYTHM_NAME "Scene Rhythm"
#define RHYTHM_VALUE(X, NAME)                                                  \
    X(NAME,                                                                    \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_F32,                                                                 \
      "number",                                                                \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define RHYTHM_OUTPUTS(X)                                                      \
    RHYTHM_VALUE(X, "Phase")                                                   \
    RHYTHM_VALUE(X, "Pulse")                                                   \
    RHYTHM_VALUE(X, "Period")                                                  \
    RHYTHM_VALUE(X, "Duration")
extern const native_definition_pack RHYTHM_DEFINITION_PACK;

#define RHYTHM_PRESENTATION                                                    \
    .category = "Time", .keywords = "beat tempo phase pulse rhythm music",     \
    .description = "Reads the scene rhythm clock, 120 BPM without one; Phase " \
                   "runs 0 to 1 per beat and Pulse fades from 1."

#endif
