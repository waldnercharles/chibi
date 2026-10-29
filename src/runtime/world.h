#pragma once
#include "ecs/component_catalog.h"
#include "execution/log.h"
#include "runtime/behaviors/scene_flow.h"
#include "runtime/engine.h"
#include "runtime/url.h"

typedef struct world world;
enum
{
    WORLD_PHYSICS = 1 << 0,
    WORLD_MATERIALS = 1 << 1,
    WORLD_PARTICLES = 1 << 2,
    WORLD_VISIBILITY = 1 << 3,
    WORLD_AUDIO = 1 << 4,
    WORLD_UI = 1 << 5,
    WORLD_INPUT = 1 << 6,
    WORLD_PHYSICS3D = 1 << 8,
    WORLD_MODULES_ALL = WORLD_PHYSICS | WORLD_MATERIALS | WORLD_PARTICLES |
                        WORLD_VISIBILITY | WORLD_AUDIO | WORLD_UI |
                        WORLD_INPUT | WORLD_PHYSICS3D,
};
typedef struct
{
    unsigned modules;
    ecs_world *ecs; /* Optional borrowed world; NULL creates an owned world. */
    ecs_component_catalog components; /* Host descriptors, borrowed; identical installed schemas reuse. */
    material_fields *fields; /* Prepared catalog, consumed only on success. */
    event_catalog *events;   /* Borrowed during creation, registry retains. */
    const input_action *actions; /* The input map, copied during creation. */
    int action_count;
    execution_log_service log; /* Copied callback; its user outlives the world. */
} world_options;
/* Owns its source registry, actual optional simulation services and
 * engine. Final application supplies its immutable type_catalog composition
 * (including TYPE_PACK) before construction. All supplied descriptors
 * and a borrowed ECS world outlive this host. Construction installs component
 * schemas at a quiescent boundary; an existing ECS world retains those schemas
 * even if later schema installation rejects. Teardown cancels host-owned work
 * and spawned entities, but leaves other borrowed-world entities alive.
 * On failure no field catalog ownership transfers. */
world *world_new(const world_options *, char *error, size_t size);
/* Whether world_new accepts this input map: at most INPUT_ACTION_MAX
 * uniquely named actions, each with at least one known control. */
bool world_actions_valid(const input_action *actions, int count);
void world_free(world *);
ecs_world *world_ecs(const world *);
source_registry *world_sources(const world *);
entities *world_entities(const world *);
engine *world_engine(const world *);
materials *world_materials(const world *);
/* Scene requests from scripts, which the host applies between frames. */
scene_flow *world_scenes(const world *);
/* External URL requests remain disabled until the host opts in. */
url_requests *world_urls(const world *);

/* Borrowed installed services; callbacks/context and service ownership remain
 * with the world. Valid until world_free. */
const engine_modules *world_modules(const world *);
