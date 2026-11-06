#ifndef CHIBI_ENGINE_H
#define CHIBI_ENGINE_H

#include "ecs/ecs.h"
#include "runtime/audio.h"
#include "runtime/behaviors/entities.h"
#include "runtime/behaviors/source.h"
#include "runtime/components.h"
#include "runtime/input_action.h"
#include "runtime/input_timeline.h"
#include "runtime/materials.h"
#include "runtime/particles.h"
#include "runtime/save.h"
#include "runtime/scenes/physics.h"
#include "runtime/scenes/physics3d.h"
#include "runtime/systems/scheduler.h"
#include "runtime/terrain_storage.h"
#include "runtime/ui.h"
#include "runtime/visibility.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct engine engine;

typedef enum
{
    ENGINE_FOCUS_NONE,
    ENGINE_FOCUS_VIEWPORT,
    ENGINE_FOCUS_TEXT,
    ENGINE_FOCUS_CANVAS,
} engine_focus;

#define ENGINE_FIXED_SECONDS (1.0 / 60.0)
#define ENGINE_STEPS_MAX 8
#define ENGINE_EVENT_BATCHES_MAX 8
#define ENGINE_FRAME_SECONDS_MAX 0.25

typedef struct
{
    uint64_t frame;
    uint64_t simulation_steps;
    double simulation_time;
    double dropped_seconds; /* Cumulative wall time omitted by overload/debug holds. */
    int event_backlog; /* Events deferred after the bounded reaction pump. */
    int steps; /* Fixed clock ticks completed during this display frame. */
    float alpha; /* Presentation interpolation; paused and explicit Step show solved state. */
    double behavior_ms; /* Includes event dispatch and spawn initialization. */
    double physics_ms;
    double events_ms; /* Contact reactions, included in behavior_ms. */
    double systems_ms;
    double rendering_ms;
    uint32_t command_apply_count;
} engine_frame_timing;

typedef void (*engine_render_fn)(void *user);

/* Services are borrowed and must outlive the engine. Sources, entities,
 * physics (2D and 3D), materials and visibility must use world; particles/audio/UI/save
 * belong to that same simulation. NULL services omit their optional stages.
 * Boundary updates injected service clocks before any queued work runs.
 * Inspection callbacks may observe but must not mutate during engine work. */
typedef struct
{
    source_registry *sources;
    entities *entities;
    physics *physics;
    physics3d *physics3d;
    materials *materials;
    particles *particles;
    visibility *visibility;
    audio *audio;
    save *save;
    terrain_storage *terrain_storage;
    ui *ui;
    bool input;
    void *context;
    void (*boundary)(void *, double now, double delta);
    bool (*paused)(void *);
} engine_modules;
typedef struct
{
    int code;
    const char *message;
} engine_input_failure;
engine *engine_new(ecs_world *world, const engine_modules *);
void engine_free(engine *engine);
/* Reset time and input before recreating authored entities and their Begin runs. */
void engine_reset(engine *engine);
/* Reset per-scene state for a scene change; time, input, recordings and
 * audio go on. */
void engine_reset_scene(engine *engine);
int engine_register_system(engine *engine, const schedule_desc *desc);
const schedule *engine_system_schedule(const engine *engine);
void engine_set_render_callback(engine *engine, engine_render_fn fn, void *user);
int engine_set_focus(engine *engine, engine_focus focus);
engine_focus engine_get_focus(const engine *engine);
int engine_set_playing(engine *engine, bool playing);
bool engine_is_playing(const engine *engine);
int engine_step(engine *engine);
/* Simulation seconds per wall second while playing (Unity's Time.timeScale,
 * Unreal's slomo): fixed steps keep their length and run less or more often,
 * so behaviors, physics, particles and material time slow alike while frames
 * still draw at the display's rate. Step advances one tick at any scale. */
void engine_set_time_scale(engine *engine, double scale);
double engine_time_scale(const engine *engine);
int engine_dispatch_input(engine *engine, const char *action, double value, int pressed, engine_input_failure *error);
/* Host input ingress: hold the latest movement/aim sample across fixed steps. */
int engine_sample_movement(engine *engine, float x, float y);
int engine_sample_aim(engine *engine, CF_V2 aim, bool firing, bool point);
/* Host-projected pointer ray, held across fixed steps and recorded for replay.
 * Active rays have finite coordinates and a nonzero direction. */
int engine_sample_pointer_ray(engine *engine, input_pointer_ray pointer);
/* Copies the project's input map. Without input the engine ignores it. */
void engine_set_input_actions(engine *engine, const input_action *actions, int count);
/* Held physical controls interpreted through the input map. Ignored while
 * replaying, and while stopped unless a step is requested. */
int engine_sample_controls(engine *engine, uint64_t controls, bool *firing);
/* Atomically samples keyboard and all controller slots. Disconnected slots
 * hold no buttons. Slots are zero-based and retain the host's device assignment. */
int engine_sample_devices(
    engine *engine,
    uint64_t controls,
    const input_controller_state controllers[INPUT_CONTROLLER_MAX],
    bool *firing
);
int engine_sample_controller(engine *engine, int controller, input_controller_state state, bool *firing);
/* Gameplay input recording and replay count fixed steps from the last reset,
 * so both follow a scene from its start. Recording keeps a key wherever held
 * input changes, plus one per control change between two steps, in order and
 * at the same step. Replay samples each key at its step and holds the last one
 * from then on in place of live samples. Both seed the shared random stream,
 * so a replay draws what its recording did. */
void engine_record_input(engine *engine, unsigned seed);
void engine_stop_recording(engine *engine);
/* Borrowed until the next recording or reset. */
const input_keyframe *engine_recorded_input(const engine *engine, int *count);
/* Copies the keys, which start at step zero in nondecreasing order. */
void engine_replay_input(engine *engine, const input_keyframe *keys, int count, unsigned seed);
void engine_stop_replay(engine *engine);
int engine_frame(engine *engine, double monotonic_time);
/* Borrowed diagnostic from the last frame failure, or NULL after success. */
const char *engine_error(const engine *engine);
const visibility *engine_visibility(const engine *engine);
audio *engine_audio(const engine *engine);
ui *engine_ui(const engine *engine);
const materials *engine_materials(const engine *engine);
/* Whether an entity shows: it isn't hidden and, when its render state culls
 * it, the fog sees its 2D position. */
bool engine_entity_visible(const engine *engine, ecs_world *world, ecs_entity entity);
/* The same for an entity drawn at 3D `position`, which the fog judges by the
 * ground under it: x and z there are the plane's x and y. */
bool engine_entity_visible_3d(const engine *engine, ecs_world *world, ecs_entity entity, CF_V3 position);
const engine_frame_timing *engine_last_timing(const engine *engine);
/* Detailed per-behavior timers are opt-in; aggregate frame timing stays active. */
void engine_set_behavior_profiling(engine *engine, bool enabled);
/* Where presentation draws the entity: its global transform, moved by the
 * physics interpolation of the nearest body at or above it. engine may be NULL,
 * which draws the exact world transform, propagated or not. */
GlobalTransform2D engine_render_transform(const engine *, const ecs_world *, ecs_entity);
/* The same for a GlobalTransform3D, moved with the nearest 3D body at or above
 * the entity. */
GlobalTransform3D engine_render_transform_3d(const engine *, const ecs_world *, ecs_entity);

#endif
