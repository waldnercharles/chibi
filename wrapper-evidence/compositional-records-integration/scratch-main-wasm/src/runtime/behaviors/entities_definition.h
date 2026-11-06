#ifndef CHIBI_ENTITIES_DEFINITION_H
#define CHIBI_ENTITIES_DEFINITION_H

#include "runtime/native_definitions.h"

#define ENTITY_GET_GLOBAL_NAME "Get Global Variable"
#define ENTITY_SET_GLOBAL_NAME "Set Global Variable"
#define ENTITY_HAS_NAME "Has Entities With Component"
#define ENTITY_FIND_NAME "Find Entities With Component"
#define ENTITY_COUNT_NAME "Count Matching Entities"
#define ENTITY_NEAREST_NAME "Find Nearest Entity"
#define ENTITY_SPAWN_NAME "Spawn Prototype"
#define ENTITY_DESTROY_NAME "Destroy Entity"
#define ENTITY_SEND_NAME "Send Event"
#define ENTITY_SPAWN_SOURCE "spawn_prototype"

/* X(name, kind, storage, type_name, container, type_flags, generic_group,
 *   reference_kind, default_owner, default_value). */
#define ENTITY_GLOBAL_NAME_PORTS(X)                                            \
    X("Name",                                                                  \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_NAME,                                                                \
      "name",                                                                  \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define ENTITY_GLOBAL_VALUE_PORTS(X)                                           \
    X("Value",                                                                 \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      SCRIPT_TYPE_UNTYPED,                                                     \
      1,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define ENTITY_EXEC_PORTS(X)                                                   \
    X("Exec",                                                                  \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define ENTITY_THEN_PORTS(X)                                                   \
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
#define ENTITY_SET_GLOBAL_INPUTS(X)                                            \
    ENTITY_EXEC_PORTS(X)                                                       \
    ENTITY_GLOBAL_NAME_PORTS(X) ENTITY_GLOBAL_VALUE_PORTS(X)
#define ENTITY_SET_GLOBAL_OUTPUTS(X)                                           \
    ENTITY_THEN_PORTS(X) ENTITY_GLOBAL_VALUE_PORTS(X)
#define ENTITY_COMPONENT_PORTS(X)                                              \
    X("Component",                                                             \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_NAME,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_COMPONENT,                                              \
      false,                                                                   \
      ((pin_value){ 0 }))
#define ENTITY_FOUND_PORTS(X)                                                  \
    X("Found",                                                                 \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_BOOL,                                                                \
      "bool",                                                                  \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define ENTITY_FIND_OUTPUTS(X)                                                 \
    X("Entities",                                                              \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_HANDLE,                                                              \
      "ecs.entity",                                                            \
      PIN_CONTAINER_ARRAY,                                                     \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define ENTITY_COUNT_INPUTS(X)                                                 \
    ENTITY_FIND_OUTPUTS(X)                                                     \
    X("Conditions",                                                            \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_STRUCT,                                                              \
      "EntityCondition",                                                       \
      PIN_CONTAINER_ARRAY,                                                     \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_STRUCT,                                        \
                    .type_name = "EntityCondition",                            \
                    .container = PIN_CONTAINER_ARRAY }))
#define ENTITY_COUNT_OUTPUTS(X)                                                \
    X("Count",                                                                 \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_I32,                                                                 \
      "int",                                                                   \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_I32 }))
#define ENTITY_COUNT_PRESENTATION                                              \
    .category = "Entities",                                                    \
    .keywords = "query filter count reduce component conditions", .description = "Counts live Entities that pass all Conditions, each a component presence test or scalar field comparison."
#define ENTITY_NEAREST_INPUTS(X)                                               \
    X("Position",                                                              \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_STRUCT,                                                              \
      "vec2",                                                                  \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))                                                      \
    X("Radius",                                                                \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_F32,                                                                 \
      "number",                                                                \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))                                                      \
    X("Categories",                                                            \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_U32,                                                                 \
      "unsigned int",                                                          \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))                                                      \
    X("Exclude",                                                               \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_HANDLE,                                                              \
      "ecs.entity",                                                            \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define ENTITY_ENTITY_PORTS(X)                                                 \
    X("Entity",                                                                \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_HANDLE,                                                              \
      "ecs.entity",                                                            \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define ENTITY_NEAREST_OUTPUTS(X)                                              \
    ENTITY_FOUND_PORTS(X)                                                      \
    ENTITY_ENTITY_PORTS(X)                                                     \
    X("Position",                                                              \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_STRUCT,                                                              \
      "vec2",                                                                  \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))                                                      \
    X("Distance",                                                              \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_F32,                                                                 \
      "number",                                                                \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define ENTITY_SPAWN_INPUTS(X)                                                 \
    ENTITY_EXEC_PORTS(X)                                                       \
    X("Prototype",                                                             \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_NAME,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_PROTOTYPE,                                              \
      false,                                                                   \
      ((pin_value){ 0 }))
#define ENTITY_SPAWN_OUTPUTS(X) ENTITY_THEN_PORTS(X) ENTITY_ENTITY_PORTS(X)
#define ENTITY_TARGET_INPUTS(X) ENTITY_EXEC_PORTS(X) ENTITY_ENTITY_PORTS(X)

extern const native_definition_pack ENTITY_DEFINITION_PACK;

#define ENTITY_GET_GLOBAL_PRESENTATION                                         \
    .category = "Global",                                                      \
    .keywords = "global shared variable binding reference entity value",       \
    .description = "Reads the value set under Name, or the entity bound to "   \
                   "it; "                                                      \
                   "dead or dying entities read as none."
#define ENTITY_SET_GLOBAL_PRESENTATION                                                                 \
    .category = "Global", .keywords = "global shared variable binding reference entity value publish", \
    .description = "Stores Value under Name for all behaviors; later writes "                          \
                   "must keep the type of the first."
#define ENTITY_HAS_PRESENTATION                                                \
    .category = "Entities",                                                    \
    .keywords = "query has any exists find component present alive",           \
    .description = "Whether any entity has Component, ignoring entities "      \
                   "queued "                                                   \
                   "for destruction."
#define ENTITY_FIND_PRESENTATION                                               \
    .category = "Entities",                                                    \
    .keywords = "query find component entities array snapshot",                \
    .description = "Lists every entity that has Component, skipping entities " \
                   "being destroyed."
#define ENTITY_NEAREST_PRESENTATION                                            \
    .category = "Entities",                                                    \
    .keywords = "nearest closest find search entity enemy position radius "    \
                "category target",                                             \
    .description = "Finds the closest entity with a collider in Categories "   \
                   "within Radius of Position, skipping Exclude."
#define ENTITY_SPAWN_PRESENTATION                                              \
    .keywords = "instantiate create entity prefab",                            \
    .description = "Spawns Prototype at the end of the step and resumes at "   \
                   "Then with Entity; its behaviors start when this flow "     \
                   "ends.",                                                    \
    .category = "Entities"
#define ENTITY_DESTROY_PRESENTATION                                            \
    .keywords = "kill despawn remove delete",                                  \
    .description = "Destroys Entity at the end of the step; queries skip it "  \
                   "meanwhile and dead entities are ignored.",                 \
    .category = "Entities"
#define ENTITY_SEND_PRESENTATION                                               \
    .category = "Events", .keywords = "send entity custom event message",      \
    .description = "Queues the chosen event and its payload for Entity after " \
                   "the current behaviors run; dropped if Entity is gone."

#endif
