#ifndef CHIBI_GAME_PROTOTYPE_PREVIEW_H
#define CHIBI_GAME_PROTOTYPE_PREVIEW_H

#include "assets/prototype.h"
#include "authoring/preview.h"
#include "runtime/engine.h"
#include "runtime/particles.h"
#include "runtime/scenes/physics_settings.h"

#include <stdbool.h>

typedef struct game_prototype_preview game_prototype_preview;

typedef struct
{
    const game_prototype *prototype;
    int parent; /* an earlier entity's index, or -1 for a root */
    av_value *components;
    const char *const *behaviors;
    int behavior_count;
    bool own_behaviors; /* behaviors replace the prototype's; otherwise it keeps them */
} game_prototype_preview_scene_entity;

game_prototype_preview *game_prototype_preview_new(const game_prototype_registry *registry);
void game_prototype_preview_free(game_prototype_preview *preview);
int game_prototype_preview_register_behavior(
    game_prototype_preview *preview,
    const char *asset_name,
    const behavior_asset *source
);
/* Returns zero or an authoring preparation error, preserving the last valid
 * nonexecutable graph generation. Executable sources use staged publication. */
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
/* Launch a scene fixture in authored order. An entity's own behavior list
 * replaces its prototype's, matching scene loading. */
int game_prototype_preview_launch_scene(
    game_prototype_preview *preview,
    const game_prototype_preview_scene_entity *entities,
    int entity_count,
    char *error,
    size_t error_size
);
/* Scene changes during play: end destroys the scene's entities and per-scene
 * state while time, input and recordings go on; start spawns the next scene
 * and runs its Begin. */
void game_prototype_preview_end_scene(game_prototype_preview *preview);
int game_prototype_preview_start_scene(
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
/* Apply world settings while stopped or between scenes; launch/reset
 * retains them. */
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
    engine_input_failure *error
);
ecs_world *game_prototype_preview_world(game_prototype_preview *preview);
game_particles *game_prototype_preview_particles(game_prototype_preview *preview);
game_source_registry *game_prototype_preview_sources(game_prototype_preview *preview);
game_preview_world *game_prototype_preview_live(game_prototype_preview *preview);
game_entities *game_prototype_preview_entities(game_prototype_preview *preview);
ecs_entity game_prototype_preview_entity(const game_prototype_preview *preview);
int game_prototype_preview_entity_count(const game_prototype_preview *preview);
ecs_entity game_prototype_preview_entity_at(const game_prototype_preview *preview, int index);
execution_instance *game_prototype_preview_instance(
    game_prototype_preview *preview,
    const char *asset_name
);

#endif
