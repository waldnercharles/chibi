#include "runtime/behaviors/entities_definition.h"

#include "runtime/behaviors/entities_native.h"
#include "runtime/behaviors/entity_query.h"

#define PORT(NAME, KIND, STORAGE, TYPE_NAME, CONTAINER, TYPE_FLAGS, GROUP, REFERENCE, OWNER, VALUE) \
    { .name = NAME,                                                                                 \
      .kind = KIND,                                                                                 \
      .storage = STORAGE,                                                                           \
      .type_name = TYPE_NAME,                                                                       \
      .container = CONTAINER,                                                                       \
      .type_flags = TYPE_FLAGS,                                                                     \
      .generic_group = GROUP,                                                                       \
      .reference_kind = REFERENCE,                                                                  \
      .default_owner = OWNER,                                                                       \
      .has_default = true,                                                                          \
      .default_value = VALUE },

static const native_parameter_definition GLOBAL_NAME[] = {
    GAME_ENTITY_GLOBAL_NAME_PORTS(PORT)
};
static const native_parameter_definition GLOBAL_VALUE[] = {
    GAME_ENTITY_GLOBAL_VALUE_PORTS(PORT)
};
static const native_parameter_definition SET_GLOBAL_IN[] = {
    GAME_ENTITY_SET_GLOBAL_INPUTS(PORT)
};
static const native_parameter_definition SET_GLOBAL_OUT[] = {
    GAME_ENTITY_SET_GLOBAL_OUTPUTS(PORT)
};
static const native_parameter_definition COMPONENT[] = { GAME_ENTITY_COMPONENT_PORTS(PORT) };
static const native_parameter_definition FOUND[] = { GAME_ENTITY_FOUND_PORTS(PORT) };
static const native_parameter_definition FIND_OUT[] = { GAME_ENTITY_FIND_OUTPUTS(PORT) };
static const native_parameter_definition COUNT_IN[] = { GAME_ENTITY_COUNT_INPUTS(PORT) };
static const native_parameter_definition COUNT_OUT[] = { GAME_ENTITY_COUNT_OUTPUTS(PORT) };
static const native_parameter_definition NEAREST_IN[] = { GAME_ENTITY_NEAREST_INPUTS(PORT) };
static const native_parameter_definition NEAREST_OUT[] = {
    GAME_ENTITY_NEAREST_OUTPUTS(PORT)
};
static const native_parameter_definition SPAWN_IN[] = { GAME_ENTITY_SPAWN_INPUTS(PORT) };
static const native_parameter_definition SPAWN_OUT[] = { GAME_ENTITY_SPAWN_OUTPUTS(PORT) };
static const native_parameter_definition TARGET[] = { GAME_ENTITY_TARGET_INPUTS(PORT) };
static const native_parameter_definition THEN[] = { GAME_ENTITY_THEN_PORTS(PORT) };
#undef PORT

#define COUNT(ARRAY) ((int)(sizeof ARRAY / sizeof *ARRAY))
#define SIGNATURE(IN, OUT)                                                     \
    .inputs = IN, .input_count = COUNT(IN), .outputs = OUT,                    \
    .output_count = COUNT(OUT)
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = GAME_ENTITY_GET_GLOBAL_NAME,
      GAME_ENTITY_GET_GLOBAL_PRESENTATION,
      .source_name = "get_global_variable",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .completion_output = -1,
      SIGNATURE(GLOBAL_NAME, GLOBAL_VALUE) },
    { .canonical_name = GAME_ENTITY_SET_GLOBAL_NAME,
      GAME_ENTITY_SET_GLOBAL_PRESENTATION,
      .source_name = "set_global_variable",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .completion_output = 0,
      SIGNATURE(SET_GLOBAL_IN, SET_GLOBAL_OUT) },
    { .canonical_name = GAME_ENTITY_HAS_NAME,
      GAME_ENTITY_HAS_PRESENTATION,
      .source_name = "has_entities_with_component",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .completion_output = -1,
      SIGNATURE(COMPONENT, FOUND) },
    { .canonical_name = GAME_ENTITY_FIND_NAME,
      GAME_ENTITY_FIND_PRESENTATION,
      .source_name = "find_entities_with_component",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .completion_output = -1,
      SIGNATURE(COMPONENT, FIND_OUT) },
    { .canonical_name = GAME_ENTITY_COUNT_NAME,
      GAME_ENTITY_COUNT_PRESENTATION,
      .source_name = "count_matching_entities",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .completion_output = -1,
      SIGNATURE(COUNT_IN, COUNT_OUT) },
    { .canonical_name = GAME_ENTITY_NEAREST_NAME,
      GAME_ENTITY_NEAREST_PRESENTATION,
      .source_name = "find_nearest_entity",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .completion_output = -1,
      SIGNATURE(NEAREST_IN, NEAREST_OUT) },
    { .canonical_name = GAME_ENTITY_SPAWN_NAME,
      GAME_ENTITY_SPAWN_PRESENTATION,
      .source_name = GAME_ENTITY_SPAWN_SOURCE,
      .effects = SCRIPT_EFFECT_SUSPEND,
      .kind = SCRIPT_CAPABILITY_ACTION,
      .completion_output = 0,
      SIGNATURE(SPAWN_IN, SPAWN_OUT) },
    { .canonical_name = GAME_ENTITY_DESTROY_NAME,
      GAME_ENTITY_DESTROY_PRESENTATION,
      .source_name = "destroy_entity",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .completion_output = 0,
      SIGNATURE(TARGET, THEN) },
    { .canonical_name = GAME_ENTITY_SEND_NAME,
      GAME_ENTITY_SEND_PRESENTATION,
      .source_name = "send_event",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .completion_output = 0,
      .event_sender = true,
      SIGNATURE(TARGET, THEN) },
};
#undef SIGNATURE

static const native_definition_binding BINDINGS[] = {
    { .canonical_name = GAME_ENTITY_GET_GLOBAL_NAME,
      .implementation = { .call = &GAME_ENTITY_GET_GLOBAL_CALL,
                          .access = &GAME_ENTITY_GLOBAL_READ_ACCESS } },
    { .canonical_name = GAME_ENTITY_SET_GLOBAL_NAME,
      .implementation = { .call = &GAME_ENTITY_SET_GLOBAL_CALL,
                          .access = &GAME_ENTITY_GLOBAL_WRITE_ACCESS } },
    { .canonical_name = GAME_ENTITY_HAS_NAME,
      .implementation = { .call = &GAME_ENTITY_HAS_COMPONENT_CALL,
                          .access = &GAME_ENTITY_QUERY_ACCESS } },
    { .canonical_name = GAME_ENTITY_FIND_NAME,
      .implementation = { .query = game_entities_find_snapshot,
                          .query_service = "game.entities",
                          .access = &GAME_ENTITY_QUERY_ACCESS } },
    { .canonical_name = GAME_ENTITY_COUNT_NAME,
      .implementation = { .call = &GAME_ENTITY_COUNT_MATCHING_CALL,
                          .access = &GAME_ENTITY_COUNT_MATCHING_ACCESS } },
    { .canonical_name = GAME_ENTITY_NEAREST_NAME,
      .implementation = { .call = &GAME_ENTITY_NEAREST_CALL,
                          .access = &GAME_ENTITY_NEAREST_ACCESS } },
    { .canonical_name = GAME_ENTITY_SPAWN_NAME,
      .implementation = { .command = &GAME_ENTITY_SPAWN_COMMAND,
                          .access = &GAME_ENTITY_STRUCTURAL_ACCESS } },
    { .canonical_name = GAME_ENTITY_DESTROY_NAME,
      .implementation = { .command = &GAME_ENTITY_DESTROY_COMMAND,
                          .access = &GAME_ENTITY_STRUCTURAL_ACCESS } },
    { .canonical_name = GAME_ENTITY_SEND_NAME,
      .implementation = { .command = &GAME_ENTITY_SEND_COMMAND,
                          .access = &GAME_ENTITY_STRUCTURAL_ACCESS } },
};
const native_definition_pack GAME_ENTITY_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS),
};
#undef COUNT
