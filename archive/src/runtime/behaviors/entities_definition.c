#include "runtime/behaviors/entities_definition.h"

#include "runtime/behaviors/entities_native.h"
#include "runtime/behaviors/entity_query.h"

static const native_parameter_definition GLOBAL_NAME[] = {
    ENTITY_GLOBAL_NAME_PORTS(NATIVE_CSTRING_PORT)
};
static const native_parameter_definition GLOBAL_VALUE[] = {
    ENTITY_GLOBAL_VALUE_PORTS(NATIVE_PORT)
};
static const native_parameter_definition SET_GLOBAL_IN[] = {
    ENTITY_SET_GLOBAL_INPUTS(NATIVE_CSTRING_PORT)
};
static const native_parameter_definition SET_GLOBAL_OUT[] = {
    ENTITY_SET_GLOBAL_OUTPUTS(NATIVE_PORT)
};
static const native_parameter_definition COMPONENT[] = {
    ENTITY_COMPONENT_PORTS(NATIVE_CSTRING_PORT)
};
static const native_parameter_definition FOUND[] = { ENTITY_FOUND_PORTS(NATIVE_PORT) };
static const native_parameter_definition FIND_OUT[] = { ENTITY_FIND_OUTPUTS(NATIVE_PORT) };
static const native_parameter_definition COUNT_IN[] = { ENTITY_COUNT_INPUTS(NATIVE_PORT) };
static const native_parameter_definition COUNT_OUT[] = { ENTITY_COUNT_OUTPUTS(NATIVE_PORT) };
static const native_parameter_definition NEAREST_IN[] = { ENTITY_NEAREST_INPUTS(NATIVE_PORT) };
static const native_parameter_definition NEAREST_OUT[] = {
    ENTITY_NEAREST_OUTPUTS(NATIVE_PORT)
};
static const native_parameter_definition SPAWN_IN[] = {
    ENTITY_SPAWN_INPUTS(NATIVE_CSTRING_PORT)
};
static const native_parameter_definition SPAWN_OUT[] = { ENTITY_SPAWN_OUTPUTS(NATIVE_PORT) };
static const native_parameter_definition TARGET[] = { ENTITY_TARGET_INPUTS(NATIVE_PORT) };
static const native_parameter_definition THEN[] = { ENTITY_THEN_PORTS(NATIVE_PORT) };
static const native_parameter_definition PAIR_IN[] = {
    { .name = "Source", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_HANDLE, .type_name = "ecs.entity" },
    { .name = "Relationship",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_U64,
      .reference_kind = SCRIPT_REFERENCE_RELATIONSHIP },
    { .name = "Target", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_HANDLE, .type_name = "ecs.entity" },
};
static const native_parameter_definition PAIR_COMMAND_IN[] = {
    ENTITY_EXEC_PORTS(NATIVE_PORT){ .name = "Source",
                                    .kind = NATIVE_PARAMETER_DATA,
                                    .storage = PIN_HANDLE,
                                    .type_name = "ecs.entity" },
    { .name = "Relationship",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_U64,
      .reference_kind = SCRIPT_REFERENCE_RELATIONSHIP },
    { .name = "Target", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_HANDLE, .type_name = "ecs.entity" },
};

#define SIGNATURE(IN, OUT)                                                     \
    .inputs = IN, .input_count = NATIVE_COUNT(IN), .outputs = OUT,             \
    .output_count = NATIVE_COUNT(OUT)
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = "add_pair",
      .source_name = "add_pair",
      .category = "Relationships",
      .description = "Queues a typed relationship link for the next ECS "
                     "command boundary.",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .completion_output = 0,
      SIGNATURE(PAIR_COMMAND_IN, THEN) },
    { .canonical_name = "remove_pair",
      .source_name = "remove_pair",
      .category = "Relationships",
      .description = "Queues removal of a typed relationship link.",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .completion_output = 0,
      SIGNATURE(PAIR_COMMAND_IN, THEN) },
    { .canonical_name = "has_pair",
      .source_name = "has_pair",
      .category = "Relationships",
      .description = "Tests the currently committed relationship links.",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .completion_output = -1,
      SIGNATURE(PAIR_IN, FOUND) },
    { .canonical_name = ENTITY_GET_GLOBAL_NAME,
      ENTITY_GET_GLOBAL_PRESENTATION,
      .source_name = "get_global_variable",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .completion_output = -1,
      SIGNATURE(GLOBAL_NAME, GLOBAL_VALUE) },
    { .canonical_name = ENTITY_SET_GLOBAL_NAME,
      ENTITY_SET_GLOBAL_PRESENTATION,
      .source_name = "set_global_variable",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .completion_output = 0,
      SIGNATURE(SET_GLOBAL_IN, SET_GLOBAL_OUT) },
    { .canonical_name = ENTITY_HAS_NAME,
      ENTITY_HAS_PRESENTATION,
      .source_name = "has_entities_with_component",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .completion_output = -1,
      SIGNATURE(COMPONENT, FOUND) },
    { .canonical_name = ENTITY_FIND_NAME,
      ENTITY_FIND_PRESENTATION,
      .source_name = "find_entities_with_component",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .completion_output = -1,
      SIGNATURE(COMPONENT, FIND_OUT) },
    { .canonical_name = ENTITY_COUNT_NAME,
      ENTITY_COUNT_PRESENTATION,
      .source_name = "count_matching_entities",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .completion_output = -1,
      SIGNATURE(COUNT_IN, COUNT_OUT) },
    { .canonical_name = ENTITY_NEAREST_NAME,
      ENTITY_NEAREST_PRESENTATION,
      .source_name = "find_nearest_entity",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .completion_output = -1,
      SIGNATURE(NEAREST_IN, NEAREST_OUT) },
    { .canonical_name = ENTITY_SPAWN_NAME,
      ENTITY_SPAWN_PRESENTATION,
      .source_name = ENTITY_SPAWN_SOURCE,
      .effects = SCRIPT_EFFECT_SUSPEND,
      .kind = SCRIPT_CAPABILITY_ACTION,
      .completion_output = 0,
      SIGNATURE(SPAWN_IN, SPAWN_OUT) },
    { .canonical_name = ENTITY_DESTROY_NAME,
      ENTITY_DESTROY_PRESENTATION,
      .source_name = "destroy_entity",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .completion_output = 0,
      SIGNATURE(TARGET, THEN) },
    { .canonical_name = ENTITY_SEND_NAME,
      ENTITY_SEND_PRESENTATION,
      .source_name = "send_event",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .completion_output = 0,
      .event_sender = true,
      SIGNATURE(TARGET, THEN) },
};
#undef SIGNATURE

static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "add_pair",
      .implementation = { .command = &ENTITY_ADD_PAIR_COMMAND,
                          .access = &ENTITY_STRUCTURAL_ACCESS } },
    { .canonical_name = "remove_pair",
      .implementation = { .command = &ENTITY_REMOVE_PAIR_COMMAND,
                          .access = &ENTITY_STRUCTURAL_ACCESS } },
    { .canonical_name = "has_pair",
      .implementation = { .call = &ENTITY_HAS_PAIR_CALL, .access = &ENTITY_QUERY_ACCESS } },
    { .canonical_name = ENTITY_GET_GLOBAL_NAME,
      .implementation = { .call = &ENTITY_GET_GLOBAL_CALL,
                          .access = &ENTITY_GLOBAL_READ_ACCESS } },
    { .canonical_name = ENTITY_SET_GLOBAL_NAME,
      .implementation = { .call = &ENTITY_SET_GLOBAL_CALL,
                          .access = &ENTITY_GLOBAL_WRITE_ACCESS } },
    { .canonical_name = ENTITY_HAS_NAME,
      .implementation = { .call = &ENTITY_HAS_COMPONENT_CALL,
                          .access = &ENTITY_QUERY_ACCESS } },
    { .canonical_name = ENTITY_FIND_NAME,
      .implementation = { .query = entities_find_snapshot,
                          .query_service = "game.entities",
                          .access = &ENTITY_QUERY_ACCESS } },
    { .canonical_name = ENTITY_COUNT_NAME,
      .implementation = { .call = &ENTITY_COUNT_MATCHING_CALL,
                          .access = &ENTITY_COUNT_MATCHING_ACCESS } },
    { .canonical_name = ENTITY_NEAREST_NAME,
      .implementation = { .call = &ENTITY_NEAREST_CALL, .access = &ENTITY_NEAREST_ACCESS } },
    { .canonical_name = ENTITY_SPAWN_NAME,
      .implementation = { .command = &ENTITY_SPAWN_COMMAND,
                          .access = &ENTITY_STRUCTURAL_ACCESS } },
    { .canonical_name = ENTITY_DESTROY_NAME,
      .implementation = { .command = &ENTITY_DESTROY_COMMAND,
                          .access = &ENTITY_STRUCTURAL_ACCESS } },
    { .canonical_name = ENTITY_SEND_NAME,
      .implementation = { .command = &ENTITY_SEND_COMMAND,
                          .access = &ENTITY_STRUCTURAL_ACCESS } },
};
const native_definition_pack ENTITY_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
