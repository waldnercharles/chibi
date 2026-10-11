#ifndef CHIBI_VISUAL_REACTION_DEFINITION_H
#define CHIBI_VISUAL_REACTION_DEFINITION_H
#include "runtime/native_definitions.h"

#define VISUAL_REACTION_TRIGGER_NAME "Trigger Visual Reaction"
#define VISUAL_REACTION_TRIGGER_SOURCE "trigger_visual_reaction"
#define VISUAL_REACTION_TRIGGER_INPUTS(X)                                      \
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
#define VISUAL_REACTION_TRIGGER_OUTPUTS(X)                                     \
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

#define VISUAL_REACTION_NAME VISUAL_REACTION_TRIGGER_NAME
#define VISUAL_REACTION_SOURCE VISUAL_REACTION_TRIGGER_SOURCE
#define VISUAL_REACTION_INPUTS(X) VISUAL_REACTION_TRIGGER_INPUTS(X)
#define VISUAL_REACTION_OUTPUTS(X) VISUAL_REACTION_TRIGGER_OUTPUTS(X)

extern const native_definition_pack VISUAL_REACTION_DEFINITION_PACK;

#define VISUAL_REACTION_TRIGGER_PRESENTATION                                   \
    .category = "Visual/Motion",                                               \
    .keywords = "trigger visual reaction bump recoil hit lift rotation",       \
    .description = "Pulses Target's visual rotation by Rotation and height "   \
                   "by "                                                       \
                   "Lift over its reaction duration; Target defaults to Self."

#endif
