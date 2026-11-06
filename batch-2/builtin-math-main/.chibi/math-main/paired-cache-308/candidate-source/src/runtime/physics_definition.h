#ifndef CHIBI_PHYSICS_DEFINITION_H
#define CHIBI_PHYSICS_DEFINITION_H
#include "runtime/native_definitions.h"

#define PHYSICS_RAY_NAME "Raycast"
#define PHYSICS_RAY_SOURCE "raycast"
#define PHYSICS_CONTACT_NAME "Get Contact Entities"
#define PHYSICS_CONTACT_SOURCE "get_contact_entities"
#define PHYSICS_IMPULSE_NAME "Apply Impulse"
#define PHYSICS_IMPULSE_SOURCE "apply_impulse"
#define PHYSICS_FORCE_NAME "Apply Force"
#define PHYSICS_FORCE_SOURCE "apply_force"
#define PHYSICS_PORT(X, NAME, TYPE, NOMINAL, CONTAINER)                        \
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
#define PHYSICS_RAY_INPUTS(X)                                                    \
    PHYSICS_PORT(X, "Start", PIN_STRUCT, "vec2", PIN_CONTAINER_SINGLE)           \
    PHYSICS_PORT(X, "End", PIN_STRUCT, "vec2", PIN_CONTAINER_SINGLE)             \
    PHYSICS_PORT(X, "Categories", PIN_U32, "unsigned int", PIN_CONTAINER_SINGLE) \
    PHYSICS_PORT(X, "Exclude", PIN_HANDLE, "ecs.entity", PIN_CONTAINER_SINGLE)   \
    PHYSICS_PORT(X, "Include Sensors", PIN_BOOL, NULL, PIN_CONTAINER_SINGLE)
#define PHYSICS_RAY_OUTPUTS(X)                                                 \
    PHYSICS_PORT(X, "Found", PIN_BOOL, NULL, PIN_CONTAINER_SINGLE)             \
    PHYSICS_PORT(X, "Entity", PIN_HANDLE, "ecs.entity", PIN_CONTAINER_SINGLE)  \
    PHYSICS_PORT(X, "Point", PIN_STRUCT, "vec2", PIN_CONTAINER_SINGLE)         \
    PHYSICS_PORT(X, "Normal", PIN_STRUCT, "vec2", PIN_CONTAINER_SINGLE)        \
    PHYSICS_PORT(X, "Distance", PIN_F32, NULL, PIN_CONTAINER_SINGLE)
#define PHYSICS_CONTACT_INPUTS(X)                                              \
    PHYSICS_PORT(X, "Entity", PIN_HANDLE, "ecs.entity", PIN_CONTAINER_SINGLE)  \
    PHYSICS_PORT(X, "Categories", PIN_U32, "unsigned int", PIN_CONTAINER_SINGLE)
#define PHYSICS_CONTACT_OUTPUTS(X)                                             \
    PHYSICS_PORT(X, "Entities", PIN_HANDLE, "ecs.entity", PIN_CONTAINER_ARRAY)
#define PHYSICS_EXEC(X, NAME)                                                  \
    X(NAME,                                                                    \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }))
#define PHYSICS_PUSH_INPUTS(X, AMOUNT)                                         \
    PHYSICS_EXEC(X, "Exec")                                                    \
    PHYSICS_PORT(X, "Entity", PIN_HANDLE, "ecs.entity", PIN_CONTAINER_SINGLE)  \
    PHYSICS_PORT(X, AMOUNT, PIN_STRUCT, "vec2", PIN_CONTAINER_SINGLE)
#define PHYSICS_PUSH_OUTPUTS(X) PHYSICS_EXEC(X, "Then")
extern const native_definition_pack PHYSICS_DEFINITION_PACK;

#define PHYSICS_RAY_PRESENTATION                                               \
    .category = "Physics",                                                     \
    .keywords = "ray segment visibility line sight wall occlusion ricochet "   \
                "normal",                                                      \
    .description = "Casts from Start to End and reports the first collider "   \
                   "hit "                                                      \
                   "in Categories; sensors count only with Include Sensors."
#define PHYSICS_CONTACT_PRESENTATION                                           \
    .category = "Physics",                                                     \
    .keywords = "contact overlap touching collider entities query categories", \
    .description = "Lists colliding entities in Categories touching Entity "   \
                   "right now; empty if Entity is dead or being destroyed."
#define PHYSICS_IMPULSE_PRESENTATION                                           \
    .category = "Physics",                                                     \
    .keywords = "push kick knockback hit jump launch velocity body",           \
    .description = "Changes Entity's dynamic body's Velocity by Impulse over " \
                   "its mass at its next physics step."
#define PHYSICS_FORCE_PRESENTATION                                             \
    .category = "Physics", .keywords = "push thrust wind accelerate body",     \
    .description = "Accelerates Entity's dynamic body by Force over its mass " \
                   "through its next physics step."

#endif
