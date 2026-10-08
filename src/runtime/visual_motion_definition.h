#ifndef CHIBI_GAME_VISUAL_REACTION_DEFINITION_H
#define CHIBI_GAME_VISUAL_REACTION_DEFINITION_H
#include "runtime/native_definitions.h"

#define GAME_VISUAL_REACTION_TRIGGER_NAME "Trigger Visual Reaction"
#define GAME_VISUAL_REACTION_TRIGGER_SOURCE "trigger_visual_reaction"
#define GAME_VISUAL_REACTION_TRIGGER_INPUTS(X)                                 \
    X("Exec",                                                                  \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))                                       \
    X("Rotation",                                                              \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_STRUCT,                                                              \
      "CF_V3",                                                                 \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_STRUCT }))                                     \
    X("Lift",                                                                  \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_F32,                                                                 \
      "float",                                                                 \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_F32 }))                                        \
    X("Target",                                                                \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_HANDLE,                                                              \
      "ecs.entity",                                                            \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      true,                                                                    \
      ((pin_value){ .type = PIN_HANDLE }))
#define GAME_VISUAL_REACTION_TRIGGER_OUTPUTS(X)                                \
    X("Then",                                                                  \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))

#define GAME_VISUAL_REACTION_NAME GAME_VISUAL_REACTION_TRIGGER_NAME
#define GAME_VISUAL_REACTION_SOURCE GAME_VISUAL_REACTION_TRIGGER_SOURCE
#define GAME_VISUAL_REACTION_INPUTS(X) GAME_VISUAL_REACTION_TRIGGER_INPUTS(X)
#define GAME_VISUAL_REACTION_OUTPUTS(X) GAME_VISUAL_REACTION_TRIGGER_OUTPUTS(X)

extern const native_definition_pack GAME_VISUAL_REACTION_DEFINITION_PACK;

#define GAME_VISUAL_REACTION_TRIGGER_PRESENTATION                              \
    .category = "Visual/Motion",                                               \
    .keywords = "trigger visual reaction bump recoil hit lift rotation",       \
    .description = "Pulses Target's visual rotation by Rotation and height "   \
                   "by "                                                       \
                   "Lift over its reaction duration; Target defaults to Self."

#endif
