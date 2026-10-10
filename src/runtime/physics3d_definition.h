#ifndef CHIBI_PHYSICS3D_DEFINITION_H
#define CHIBI_PHYSICS3D_DEFINITION_H
#include "runtime/native_definitions.h"

#define PHYSICS3D_RAY_NAME "Raycast 3D"
#define PHYSICS3D_RAY_SOURCE "raycast_3d"
#define PHYSICS3D_CAST_NAME "Shape Cast 3D"
#define PHYSICS3D_CAST_SOURCE "shape_cast_3d"
#define PHYSICS3D_CONTACT_NAME "Contact Entities 3D"
#define PHYSICS3D_CONTACT_SOURCE "contact_entities_3d"
#define PHYSICS3D_PORT(X, NAME, TYPE, NOMINAL, CONTAINER)                      \
    X(NAME,                                                                    \
      NATIVE_PARAMETER_DATA,                                                   \
      TYPE,                                                                    \
      NOMINAL,                                                                 \
      CONTAINER,                                                               \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define PHYSICS3D_RAY_INPUTS(X)                                                    \
    PHYSICS3D_PORT(X, "Start", PIN_STRUCT, "CF_V3", PIN_CONTAINER_SINGLE)          \
    PHYSICS3D_PORT(X, "End", PIN_STRUCT, "CF_V3", PIN_CONTAINER_SINGLE)            \
    PHYSICS3D_PORT(X, "Categories", PIN_U32, "unsigned int", PIN_CONTAINER_SINGLE) \
    PHYSICS3D_PORT(X, "Exclude", PIN_HANDLE, "ecs.entity", PIN_CONTAINER_SINGLE)   \
    PHYSICS3D_PORT(X, "Include Sensors", PIN_BOOL, NULL, PIN_CONTAINER_SINGLE)
#define PHYSICS3D_CAST_INPUTS(X)                                                   \
    PHYSICS3D_PORT(X, "Entity", PIN_HANDLE, "ecs.entity", PIN_CONTAINER_SINGLE)    \
    PHYSICS3D_PORT(X, "Translation", PIN_STRUCT, "CF_V3", PIN_CONTAINER_SINGLE)    \
    PHYSICS3D_PORT(X, "Categories", PIN_U32, "unsigned int", PIN_CONTAINER_SINGLE) \
    PHYSICS3D_PORT(X, "Include Sensors", PIN_BOOL, NULL, PIN_CONTAINER_SINGLE)
#define PHYSICS3D_HIT_OUTPUTS(X)                                                \
    PHYSICS3D_PORT(X, "Found", PIN_BOOL, NULL, PIN_CONTAINER_SINGLE)            \
    PHYSICS3D_PORT(X, "Entity", PIN_HANDLE, "ecs.entity", PIN_CONTAINER_SINGLE) \
    PHYSICS3D_PORT(X, "Point", PIN_STRUCT, "CF_V3", PIN_CONTAINER_SINGLE)       \
    PHYSICS3D_PORT(X, "Normal", PIN_STRUCT, "CF_V3", PIN_CONTAINER_SINGLE)      \
    PHYSICS3D_PORT(X, "Distance", PIN_F32, NULL, PIN_CONTAINER_SINGLE)
#define PHYSICS3D_CONTACT_INPUTS(X)                                             \
    PHYSICS3D_PORT(X, "Entity", PIN_HANDLE, "ecs.entity", PIN_CONTAINER_SINGLE) \
    PHYSICS3D_PORT(X, "Categories", PIN_U32, "unsigned int", PIN_CONTAINER_SINGLE)
#define PHYSICS3D_CONTACT_OUTPUTS(X)                                           \
    PHYSICS3D_PORT(X, "Entities", PIN_HANDLE, "ecs.entity", PIN_CONTAINER_ARRAY)
extern const native_definition_pack GAME_PHYSICS3D_DEFINITION_PACK;

#define PHYSICS3D_RAY_PRESENTATION                                                  \
    .category = "Physics", .keywords = "ray segment line sight wall hit normal 3d", \
    .description = "Casts from Start to End and reports the first 3D "              \
                   "collider hit in Categories, skipping Exclude's body; "          \
                   "sensors count only with Include Sensors."
#define PHYSICS3D_CAST_PRESENTATION                                            \
    .category = "Physics",                                                     \
    .keywords = "sweep shape cast move ahead blocked collider 3d",             \
    .description = "Moves Entity's 3D collider by Translation and reports "    \
                   "the first collider it meets in Categories, skipping its "  \
                   "own body; sensors count only with Include Sensors."
#define PHYSICS3D_CONTACT_PRESENTATION                                         \
    .category = "Physics",                                                     \
    .keywords = "contact overlap touching collider entities query 3d",         \
    .description = "Lists 3D colliders in Categories touching Entity right "   \
                   "now; empty if Entity is dead or being destroyed."

#endif
