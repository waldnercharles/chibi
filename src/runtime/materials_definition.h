#ifndef CHIBI_GAME_MATERIAL_DEFINITION_H
#define CHIBI_GAME_MATERIAL_DEFINITION_H
#include "runtime/native_definitions.h"

#define GAME_MATERIAL_DamageMaterial_NAME "Damage Material"
#define GAME_MATERIAL_DamageMaterial_SOURCE "damage_material"
#define GAME_MATERIAL_DamageMaterial_INPUTS(X)                                 \
    X("In",                                                                    \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))                                       \
    X("Entity",                                                                \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_HANDLE,                                                              \
      "ecs.entity",                                                            \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_HANDLE }))                                     \
    X("Point",                                                                 \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_STRUCT,                                                              \
      "CF_V2",                                                                 \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_STRUCT }))                                     \
    X("Radius",                                                                \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_F32,                                                                 \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_F32 }))                                        \
    X("Energy",                                                                \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_F32,                                                                 \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_F32 }))                                        \
    X("Cause",                                                                 \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_I32,                                                                 \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_I32 }))                                        \
    X("Source",                                                                \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_HANDLE,                                                              \
      "ecs.entity",                                                            \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_HANDLE }))
#define GAME_MATERIAL_DamageMaterial_OUTPUTS(X)                                \
    X("Out",                                                                   \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))
#define GAME_MATERIAL_ReleaseMaterial_NAME "Release Material"
#define GAME_MATERIAL_ReleaseMaterial_SOURCE "release_material"
#define GAME_MATERIAL_ReleaseMaterial_INPUTS(X)                                \
    X("In",                                                                    \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))                                       \
    X("Entity",                                                                \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_HANDLE,                                                              \
      "ecs.entity",                                                            \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_HANDLE }))
#define GAME_MATERIAL_ReleaseMaterial_OUTPUTS(X)                               \
    X("Out",                                                                   \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))
#define GAME_MATERIAL_IsMaterialFragment_NAME "Is Material Fragment"
#define GAME_MATERIAL_IsMaterialFragment_SOURCE "is_material_fragment"
#define GAME_MATERIAL_IsMaterialFragment_INPUTS(X)                             \
    X("Entity",                                                                \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_HANDLE,                                                              \
      "ecs.entity",                                                            \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_HANDLE }))
#define GAME_MATERIAL_IsMaterialFragment_OUTPUTS(X)                            \
    X("Fragment",                                                              \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_BOOL,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_BOOL }))
#define GAME_MATERIAL_IsPierceableMaterialFragment_NAME                        \
    "Is Pierceable Material Fragment"
#define GAME_MATERIAL_IsPierceableMaterialFragment_SOURCE                      \
    "is_pierceable_material_fragment"
#define GAME_MATERIAL_IsPierceableMaterialFragment_INPUTS(X)                   \
    X("Entity",                                                                \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_HANDLE,                                                              \
      "ecs.entity",                                                            \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_HANDLE }))
#define GAME_MATERIAL_IsPierceableMaterialFragment_OUTPUTS(X)                  \
    X("Fragment",                                                              \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_BOOL,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_BOOL }))

#define GAME_MATERIAL_DAMAGE_INPUTS(X) GAME_MATERIAL_DamageMaterial_INPUTS(X)
#define GAME_MATERIAL_RELEASE_INPUTS(X) GAME_MATERIAL_ReleaseMaterial_INPUTS(X)
#define GAME_MATERIAL_FRAGMENT_INPUTS(X)                                       \
    GAME_MATERIAL_IsMaterialFragment_INPUTS(X)
#define GAME_MATERIAL_ACTION_OUTPUTS(X) GAME_MATERIAL_DamageMaterial_OUTPUTS(X)
#define GAME_MATERIAL_FRAGMENT_OUTPUTS(X)                                      \
    GAME_MATERIAL_IsMaterialFragment_OUTPUTS(X)

extern const native_definition_pack GAME_MATERIAL_DEFINITION_PACK;

/* Shared authoring presentation for these native declarations. */
#define GAME_MATERIAL_DamageMaterial_PRESENTATION                              \
    .category = "Physics",                                                     \
    .description = "Queues Energy damage that carves Entity's material "       \
                   "within "                                                   \
                   "Radius of Point; non-material targets are ignored."
#define GAME_MATERIAL_IsMaterialFragment_PRESENTATION                          \
    .category = "Physics",                                                     \
    .description = "Whether Entity is a fragment broken off destructible "     \
                   "material."
#define GAME_MATERIAL_IsPierceableMaterialFragment_PRESENTATION                \
    .category = "Physics",                                                     \
    .description = "Whether Entity is a material fragment that continuous "    \
                   "sensors may pass through."
#define GAME_MATERIAL_ReleaseMaterial_PRESENTATION                             \
    .category = "Physics",                                                     \
    .description = "Queues Entity's remaining material to break loose as one " \
                   "fragment, then destroys Entity."

#endif
