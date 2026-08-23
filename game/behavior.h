#ifndef CBLUEPRINT_GAME_BEHAVIOR_H
#define CBLUEPRINT_GAME_BEHAVIOR_H

#include "../core/instance.h"
#include "../ecs/ecs.h"

#include <stdint.h>

typedef struct game_behavior_world game_behavior_world;

game_behavior_world *game_behavior_world_new(ecs_world *world);
void game_behavior_world_free(game_behavior_world *world);
int game_behavior_register_asset(game_behavior_world *world, const char *asset_name, blueprint *definition);
int game_behavior_replace_asset(
    game_behavior_world *world,
    const char *asset_name,
    blueprint *definition,
    blueprint **old_definition
);
int game_behavior_attach(game_behavior_world *world, ecs_entity entity, const char *asset_name);
int game_behavior_detach_entity(game_behavior_world *world, ecs_entity entity);
int game_behavior_destroy_entity(game_behavior_world *world, ecs_entity entity);
int game_behavior_dispatch(
    game_behavior_world *world,
    ecs_entity entity,
    const char *event_name,
    flow_inputs inputs,
    flow_error *error
);
int game_behavior_pump(game_behavior_world *world, double monotonic_time);
int game_behavior_attachment_count(const game_behavior_world *world);
int game_behavior_pool_size(const game_behavior_world *world, const char *asset_name);
blueprint_instance *game_behavior_instance(
    game_behavior_world *world,
    ecs_entity entity,
    const char *asset_name
);

/* The editor supplies the component-specific node replacement operation. The
 * runtime owns the shared entity and instance invariants; it does not put ECS
 * types into core. */
typedef int (*game_behavior_promote_fn)(
    blueprint *definition,
    uint64_t variable_id,
    ecs_component_id component_id,
    void *user
);
int game_behavior_promote_variable(
    game_behavior_world *world,
    const char *asset_name,
    uint64_t variable_id,
    ecs_component_id component_id,
    game_behavior_promote_fn promote,
    void *user
);

#endif
