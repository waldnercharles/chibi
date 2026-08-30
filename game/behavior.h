#ifndef CBLUEPRINT_GAME_BEHAVIOR_H
#define CBLUEPRINT_GAME_BEHAVIOR_H

#include "../core/instance.h"
#include "../ecs/ecs.h"

#include <stdint.h>

typedef struct game_behavior_world game_behavior_world;

#define GAME_BEHAVIOR_LOG_MESSAGE_MAX 256

typedef struct
{
    ecs_entity entity;
    const char *asset_name;
    const node *origin;
    char message[GAME_BEHAVIOR_LOG_MESSAGE_MAX];
} game_behavior_log_info;

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
int game_behavior_detach_asset(game_behavior_world *world, ecs_entity entity, const char *asset_name);
int game_behavior_set_breakpoints_enabled_for_asset(
    game_behavior_world *world,
    ecs_entity entity,
    const char *asset_name,
    int enabled
);
int game_behavior_breakpoints_enabled_for_asset(
    const game_behavior_world *world,
    ecs_entity entity,
    const char *asset_name
);
int game_behavior_set_debug_hold(
    game_behavior_world *world,
    ecs_entity entity,
    const char *asset_name,
    int enabled
);
int game_behavior_detach_entity(game_behavior_world *world, ecs_entity entity);
int game_behavior_destroy_entity(game_behavior_world *world, ecs_entity entity);
int game_behavior_dispatch(
    game_behavior_world *world,
    ecs_entity entity,
    const char *event_name,
    flow_inputs inputs,
    flow_error *error
);
/* Dispatches to every live attachment that declares the event. */
int game_behavior_dispatch_all(
    game_behavior_world *world,
    const char *event_name,
    flow_inputs inputs,
    flow_error *error
);
int game_behavior_pump(game_behavior_world *world, double monotonic_time);
void game_behavior_set_delta_seconds(game_behavior_world *world, double delta_seconds);
void game_behavior_set_parallel_for(
    game_behavior_world *world,
    flow_parallel_for_fn parallel_for,
    void *parallel_user
);
void game_behavior_set_breakpoints_enabled(game_behavior_world *world, int enabled);
void game_behavior_set_sink(game_behavior_world *world, flow_sink_fn sink, void *user);
int game_behavior_log_count(const game_behavior_world *world);
int game_behavior_log_info_at(const game_behavior_world *world, int index, game_behavior_log_info *out);
void game_behavior_clear_logs(game_behavior_world *world);
int game_behavior_attachment_count(const game_behavior_world *world);

/* Attachment records borrow the asset name, definition, and instance from the
 * behavior world. They remain valid until the next attach, detach, replace, or
 * world destruction operation. */
typedef struct
{
    ecs_entity entity;
    const char *asset_name;
    const blueprint *definition;
    blueprint_instance *instance;
    uint64_t sequence;
    int debug_hold;
} game_behavior_attachment_info;

typedef struct
{
    ecs_entity entity;
    int active;
    int suspended;
    double wake_time;
} game_behavior_entity_state;

int game_behavior_attachment_count_for_entity(const game_behavior_world *world, ecs_entity entity);
int game_behavior_attachment_info_at(
    game_behavior_world *world,
    ecs_entity entity,
    int index,
    game_behavior_attachment_info *out
);
int game_behavior_entity_state_at(
    const game_behavior_world *world,
    ecs_entity entity,
    const char *asset_name,
    game_behavior_entity_state *out
);
int game_behavior_asset_registered(const game_behavior_world *world, const char *asset_name);
int game_behavior_asset_count(const game_behavior_world *world);
const char *game_behavior_asset_name_at(const game_behavior_world *world, int index);
const blueprint *game_behavior_asset_definition_at(const game_behavior_world *world, int index);
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
