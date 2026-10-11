#ifndef CHIBI_PROTOTYPE_PREVIEW_H
#define CHIBI_PROTOTYPE_PREVIEW_H

#include "assets/prototype.h"
#include "authoring/preview.h"
#include "runtime/engine.h"
#include "runtime/particles.h"
#include "runtime/scenes/physics_settings.h"

#include <stdbool.h>

typedef struct prototype_preview prototype_preview;

typedef struct
{
    const char *name; /* unique among its siblings, so paths name it */
    const prototype *prototype;
    int parent; /* an earlier entity's index, or -1 for a root */
    av_value *components;
    const char *const *behaviors;
    int behavior_count;
    bool own_behaviors; /* behaviors replace the prototype's; otherwise it keeps them */
} prototype_preview_scene_entity;

prototype_preview *prototype_preview_new(const prototype_registry *registry);
void prototype_preview_free(prototype_preview *preview);
int prototype_preview_register_behavior(
    prototype_preview *preview,
    const char *asset_name,
    const behavior_asset *source
);
/* Returns zero or an authoring preparation error, preserving the last valid
 * nonexecutable graph generation. Executable sources use staged publication. */
int prototype_preview_prepare_library(
    prototype_preview *preview,
    const char *name,
    const behavior_asset *source,
    char *message,
    size_t size
);
int prototype_preview_update_behavior(
    prototype_preview *preview,
    const char *asset_name,
    const behavior_asset *source,
    bool restart,
    char *message,
    size_t message_size
);
int prototype_preview_launch(
    prototype_preview *preview,
    const prototype *prototype,
    char *error,
    size_t error_size
);
/* Launch a scene fixture in authored order. An entity's own behavior list
 * replaces its prototype's, matching scene loading. */
int prototype_preview_launch_scene(
    prototype_preview *preview,
    const prototype_preview_scene_entity *entities,
    int entity_count,
    char *error,
    size_t error_size
);
/* Scene changes during play: end destroys the scene's entities and per-scene
 * state while time, input and recordings go on; start spawns the next scene
 * and runs its Begin. */
void prototype_preview_end_scene(prototype_preview *preview);
int prototype_preview_start_scene(
    prototype_preview *preview,
    const prototype_preview_scene_entity *entities,
    int entity_count,
    char *error,
    size_t error_size
);
int prototype_preview_register_prototype(prototype_preview *preview, const prototype *prototype);
void prototype_preview_stop(prototype_preview *preview);
/* Apply world settings while stopped or between scenes; launch/reset
 * retains them. */
int prototype_preview_configure_physics(prototype_preview *preview, physics_settings settings);
int prototype_preview_tick(prototype_preview *preview, double monotonic_time);
/* The preview owns a normal engine; transport, timing and input use its shared pipeline. */
engine *prototype_preview_runtime(prototype_preview *preview);
/* Hold movement across fixed simulation steps. */
int prototype_preview_sample_movement(prototype_preview *preview, float x, float y);
/* Queue a local action for Input owners in the preview world. */
int prototype_preview_input(
    prototype_preview *preview,
    const char *action,
    double value,
    int pressed,
    engine_input_failure *error
);
ecs_world *prototype_preview_world(prototype_preview *preview);
particles *prototype_preview_particles(prototype_preview *preview);
source_registry *prototype_preview_sources(prototype_preview *preview);
preview_world *prototype_preview_live(prototype_preview *preview);
entities *prototype_preview_entities(prototype_preview *preview);
ecs_entity prototype_preview_entity(const prototype_preview *preview);
int prototype_preview_entity_count(const prototype_preview *preview);
ecs_entity prototype_preview_entity_at(const prototype_preview *preview, int index);
execution_instance *prototype_preview_instance(prototype_preview *preview, const char *asset_name);

#endif
