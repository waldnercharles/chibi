#ifndef CHIBI_GAME_PHYSICS_H
#define CHIBI_GAME_PHYSICS_H

#include "ecs/ecs.h"
#include "graph/model/node.h"
#include "runtime/components.h"
#include "runtime/material_fluid.h"
#include "runtime/scenes/physics_settings.h"
#include "runtime/tasks.h"

#include <stdint.h>

struct game_entities;
typedef struct game_physics game_physics;

typedef struct
{
    double step_ms, pairs_ms, collide_ms, solve_ms;
    double merge_islands_ms, prepare_stages_ms, solve_constraints_ms;
    double prepare_constraints_ms, integrate_velocities_ms, warm_start_ms;
    double solve_impulses_ms, integrate_positions_ms, relax_impulses_ms;
    double apply_restitution_ms, store_impulses_ms, split_islands_ms;
    double transforms_ms, hit_events_ms, refit_ms, bullets_ms;
    double sleep_islands_ms, sensors_ms;
} game_physics_box2d_profile;

typedef struct
{
    double synchronize_ms, solve_ms, sweeps_ms, pairs_ms, writeback_ms, dispatch_ms;
    double contact_collection_ms, contact_enqueue_ms, contact_join_ms, contact_merge_ms;
    double position_clamp_ms, fluid_ms, fluid_prepare_ms, material_tick_ms,
        fluid_impulse_ms;
    game_physics_box2d_profile box2d;
    material_fluid_profile material_fluid;
    int bodies, pairs;
    /* Per-step work while profiling is enabled. Mask-only edits retain bodies;
     * mass_updates includes implicit shape creation and explicit batch updates. */
    uint64_t body_creations, shape_creations, shape_updates, shape_removals,
        mass_updates;
} game_physics_profile;

typedef struct
{
    uint64_t body_state, contact_pairs, contact_events;
} game_physics_hashes;

/* Authored lengths use 32 units per meter; velocities use those units/second.
 * One persistent, zero-gravity Box2D world belongs to each live ECS world.
 * Native worlds use a shared lazy pool for sufficiently large workloads. */
game_physics *game_physics_new(ecs_world *world, struct game_entities *entities);
void game_physics_free(game_physics *physics);
void game_physics_reset(game_physics *physics);
/* Worker changes require an empty/reset world using the owned runtime pool.
 * Invalid changes fail without modifying settings. Resets retain settings. */
int game_physics_configure(game_physics *physics, game_physics_settings settings);
game_physics_settings game_physics_configuration(const game_physics *physics);
/* Configure before bodies are synchronized, or immediately after reset.
 * The borrowed executor must outlive this physics world. NULL selects serial. */
void game_physics_set_tasks(game_physics *physics, const game_task_system *tasks);
/* Effective worker budget for the last step; 1 before the first step. */
int game_physics_workers(const game_physics *physics);
/* Synchronize authored state, integrate, then queue deduplicated Contact Begin.
 * Payload: Other, Point (CF_V2). Sweeps report the cast contact in world
 * units; initial overlaps and ordinary contacts use a representative center.
 * Swept events are delivered in travel order, clipped to the first blocker.
 * A zero delta synchronizes and checks overlaps without advancing simulation. */
int game_physics_tick(game_physics *physics, float delta_seconds);
const char *game_physics_error(const game_physics *physics);
/* Optional phase timers for the last physics step; no clock reads when off. */
void game_physics_set_profiling(game_physics *physics, bool enabled);
const game_physics_profile *game_physics_last_profile(const game_physics *physics);
game_physics_hashes game_physics_hash(const game_physics *physics);
/* Borrow solved poses for presentation without modifying authoritative ECS. */
bool game_physics_interpolate(const game_physics *physics, ecs_entity entity, float alpha, GameTransform *out);

typedef struct
{
    float mass, inertia;
    CF_V2 local_center;
    bool awake;
    int shape_count;
} game_physics_body_state;
/* Current solved body state; local center uses game units. Fixed rotation has
 * zero rotational inertia. Returns false before synchronization or after removal. */
bool game_physics_inspect_body(const game_physics *physics, ecs_entity entity, game_physics_body_state *out);

/* Snapshot current collider contacts into a caller-owned core array. Honors
 * bilateral filters and pending destruction, including before the first step.
 * Solid contacts include Box2D's 0.02 meter speculative separation; sensors
 * require actual overlap. Reads authored poses, so teleports are immediate. */
int game_physics_contact_entities(
    ecs_world *world,
    struct game_entities *entities,
    ecs_entity entity,
    unsigned categories,
    ecs_entity **out
);
extern node_desc GAME_NODE_GetContactEntities;

typedef struct
{
    bool found;
    ecs_entity entity;
    CF_V2 point, normal;
    float distance;
} game_ray_hit;

/* Segment query against authored circle/rotated-box colliders, including
 * teleports before the next physics tick. Nearest hit wins; ties use handle
 * order. An origin inside a collider hits at zero with normal against the ray.
 * Categories filter targets independently of their contact masks. */
int game_physics_raycast(
    ecs_world *world,
    struct game_entities *entities,
    CF_V2 start,
    CF_V2 end,
    unsigned categories,
    ecs_entity exclude,
    bool sensors,
    game_ray_hit *out
);
/* Reuse one authored collider snapshot for a batch of visibility/sensor rays.
 * Refresh after movement; queries allocate nothing and do not iterate ECS. */
typedef struct game_ray_scene game_ray_scene;
game_ray_scene *game_ray_scene_new();
void game_ray_scene_free(game_ray_scene *scene);
int game_ray_scene_refresh(game_ray_scene *scene, ecs_world *world, struct game_entities *entities);
int game_ray_scene_cast(
    const game_ray_scene *scene,
    CF_V2 start,
    CF_V2 end,
    unsigned categories,
    ecs_entity exclude,
    bool sensors,
    game_ray_hit *out
);
extern node_desc GAME_NODE_Raycast;

#endif
