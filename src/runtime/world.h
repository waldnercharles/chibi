#pragma once
#include "ecs/component_catalog.h"
#include "execution/log.h"
#include "runtime/behaviors/scene_flow.h"
#include "runtime/engine.h"

typedef struct game_world game_world;
enum
{
    GAME_WORLD_PHYSICS = 1 << 0,
    GAME_WORLD_MATERIALS = 1 << 1,
    GAME_WORLD_PARTICLES = 1 << 2,
    GAME_WORLD_VISIBILITY = 1 << 3,
    GAME_WORLD_AUDIO = 1 << 4,
    GAME_WORLD_UI = 1 << 5,
    GAME_WORLD_INPUT = 1 << 6,
    GAME_WORLD_VISUAL_MOTION = 1 << 7,
    GAME_WORLD_MODULES_ALL = (1 << 8) - 1,
};
typedef struct
{
    unsigned modules;
    ecs_world *ecs; /* Optional borrowed world; NULL creates an owned world. */
    ecs_component_catalog components; /* Host descriptors, borrowed; identical installed schemas reuse. */
    material_fields *fields; /* Prepared catalog, consumed only on success. */
    game_event_catalog *events; /* Borrowed during creation, registry retains. */
    const input_action *actions; /* The input map, copied during creation. */
    int action_count;
    execution_log_service log; /* Copied callback; its user outlives the world. */
} game_world_options;
/* Owns its source registry, actual optional simulation services and
 * engine. Final application supplies its immutable type_catalog composition
 * (including GAME_TYPE_PACK) before construction. All supplied descriptors
 * and a borrowed ECS world outlive this host. Construction installs component
 * schemas at a quiescent boundary; an existing ECS world retains those schemas
 * even if later schema installation rejects. Teardown cancels host-owned work
 * and spawned entities, but leaves other borrowed-world entities alive.
 * On failure no field catalog ownership transfers. */
game_world *game_world_new(const game_world_options *, char *error, size_t size);
/* Whether game_world_new accepts this input map: at most INPUT_ACTION_MAX
 * uniquely named actions, each with at least one known control. */
bool game_world_actions_valid(const input_action *actions, int count);
void game_world_free(game_world *);
ecs_world *game_world_ecs(const game_world *);
game_source_registry *game_world_sources(const game_world *);
game_entities *game_world_entities(const game_world *);
engine *game_world_engine(const game_world *);
game_materials *game_world_materials(const game_world *);
material_fields *game_world_fields(const game_world *);
/* Scene requests from scripts, which the host applies between frames. */
game_scene_flow *game_world_scenes(const game_world *);

/* Borrowed installed services; callbacks/context and service ownership remain
 * with the world. Valid until world_free. */
const engine_modules *game_world_modules(const game_world *);
