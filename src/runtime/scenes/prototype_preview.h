#ifndef CHIBI_GAME_PROTOTYPE_PREVIEW_H
#define CHIBI_GAME_PROTOTYPE_PREVIEW_H

#include "assets/prototype.h"
#include "render/particles.h"
#include "runtime/behaviors/behavior.h"
#include "runtime/engine.h"
#include "runtime/scenes/physics_settings.h"

typedef struct game_prototype_preview game_prototype_preview;

typedef struct
{
    const game_prototype *prototype;
    const char *components_json;
    const char *const *behaviors;
    int behavior_count;
} game_prototype_preview_scene_entity;

game_prototype_preview *game_prototype_preview_new(const game_prototype_registry *registry);
void game_prototype_preview_free(game_prototype_preview *preview);
int game_prototype_preview_register_behavior(
    game_prototype_preview *preview,
    const char *asset_name,
    const behavior_asset *source
);
/* Returns INSTANCE_UPDATE_* or a compile error, preserving the last valid
 * version. Restart explicitly allows behavior state to be recreated. */
int game_prototype_preview_prepare_library(
    game_prototype_preview *preview,
    const char *name,
    const behavior_asset *source,
    char *message,
    size_t size
);
int game_prototype_preview_update_behavior(
    game_prototype_preview *preview,
    const char *asset_name,
    const behavior_asset *source,
    bool restart,
    char *message,
    size_t message_size
);
int game_prototype_preview_launch(
    game_prototype_preview *preview,
    const game_prototype *prototype,
    char *error,
    size_t error_size
);
/* Launch a scene fixture in authored order. The scene behavior list replaces
 * the prototype's attached behaviors for each entity, matching scene loading. */
int game_prototype_preview_launch_scene(
    game_prototype_preview *preview,
    const game_prototype_preview_scene_entity *entities,
    int entity_count,
    char *error,
    size_t error_size
);
int game_prototype_preview_register_prototype(
    game_prototype_preview *preview,
    const game_prototype *prototype
);
void game_prototype_preview_stop(game_prototype_preview *preview);
/* Apply world settings while stopped; launch/reset retains them. */
int game_prototype_preview_configure_physics(game_prototype_preview *preview, game_physics_settings settings);
int game_prototype_preview_tick(game_prototype_preview *preview, double monotonic_time);
/* The preview owns a normal engine; transport, timing and input use its shared pipeline. */
engine *game_prototype_preview_runtime(game_prototype_preview *preview);
/* Hold movement across fixed simulation steps. */
int game_prototype_preview_sample_movement(game_prototype_preview *preview, float x, float y);
/* Queue a local action for GameInput owners in the preview world. */
int game_prototype_preview_input(
    game_prototype_preview *preview,
    const char *action,
    double value,
    int pressed,
    flow_error *error
);
ecs_world *game_prototype_preview_world(game_prototype_preview *preview);
game_particles *game_prototype_preview_particles(game_prototype_preview *preview);
game_behavior_world *game_prototype_preview_behaviors(game_prototype_preview *preview);
game_entities *game_prototype_preview_entities(game_prototype_preview *preview);
ecs_entity game_prototype_preview_entity(const game_prototype_preview *preview);
int game_prototype_preview_entity_count(const game_prototype_preview *preview);
ecs_entity game_prototype_preview_entity_at(const game_prototype_preview *preview, int index);
behavior_instance *game_prototype_preview_instance(game_prototype_preview *preview, const char *asset_name);

#endif

int game_prototype_preview_register_system(
    game_prototype_preview *preview,
    const char *name,
    const char *json,
    char *error,
    size_t size
);
