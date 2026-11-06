#ifndef CHIBI_PHYSICS_H
#define CHIBI_PHYSICS_H

#include "ecs/ecs.h"
#include "runtime/components.h"
#include "runtime/material_fluid.h"
#include "runtime/scenes/physics_settings.h"
#include "runtime/tasks.h"

#include <stdint.h>

struct entities;
typedef struct physics physics;

typedef struct
{
    double step_ms, pairs_ms, collide_ms, solve_ms;
    double merge_islands_ms, prepare_stages_ms, solve_constraints_ms;
    double prepare_constraints_ms, integrate_velocities_ms, warm_start_ms;
    double solve_impulses_ms, integrate_positions_ms, relax_impulses_ms;
    double apply_restitution_ms, store_impulses_ms, split_islands_ms;
    double transforms_ms, hit_events_ms, refit_ms, bullets_ms;
    double sleep_islands_ms, sensors_ms;
} physics_box2d_profile;

typedef struct
{
    double synchronize_ms, solve_ms, sweeps_ms, pairs_ms, writeback_ms, dispatch_ms;
    double contact_collection_ms, contact_enqueue_ms, contact_join_ms, contact_merge_ms;
    double position_clamp_ms, fluid_ms, fluid_prepare_ms, material_tick_ms,
        fluid_impulse_ms;
    physics_box2d_profile box2d;
    material_fluid_profile material_fluid;
    int bodies, pairs;
    /* Per-step work while profiling is enabled. Mask-only edits retain bodies;
     * mass_updates includes implicit shape creation and explicit batch updates. */
    uint64_t body_creations, shape_creations, shape_updates, shape_removals,
        mass_updates;
} physics_profile;

typedef struct
{
    uint64_t body_state, contact_pairs, contact_events;
} physics_hashes;

/* Authored lengths use 32 units per meter; velocities use those units/second.
 * One persistent, zero-gravity Box2D world belongs to each live ECS world.
 * Native worlds use a shared lazy pool for sufficiently large workloads. */
physics *physics_new(ecs_world *world, struct entities *entities);
void physics_free(physics *physics);
void physics_reset(physics *physics);
/* Worker changes require an empty/reset world using the owned runtime pool.
 * Invalid changes fail without modifying settings. Resets retain settings. */
int physics_configure(physics *physics, physics_settings settings);
physics_settings physics_configuration(const physics *physics);
/* Configure before bodies are synchronized, or immediately after reset.
 * The borrowed executor must outlive this physics world. NULL selects serial. */
void physics_set_tasks(physics *physics, const task_system *tasks);
/* Effective worker budget for the last step; 1 before the first step. */
int physics_workers(const physics *physics);
/* Push entity's dynamic Body through its center of mass in the next step that
 * advances. An impulse changes its Velocity by impulse over its mass at once;
 * a force accelerates it by force over its mass through that step. Bodies
 * weigh 1 kg per square meter. Pushes add up, and a push on a body destroyed or
 * no longer dynamic by then is dropped. FLOW_ERR_ARGUMENT for a non-finite
 * vector or an entity without a dynamic Body. */
int physics_apply_impulse(physics *physics, ecs_entity entity, CF_V2 impulse);
int physics_apply_force(physics *physics, ecs_entity entity, CF_V2 force);
/* Synchronize authored state, integrate, then queue Contact End (payload:
 * Other) to both entities of each pair that stopped touching, in pair order,
 * and deduplicated Contact Begin (payload: Other, Point (CF_V2)) to both of
 * each pair that started. Sweeps report the cast contact in world units;
 * initial overlaps and ordinary contacts use a representative center. Swept
 * Begin events are delivered in travel order, clipped to the first blocker.
 * A zero delta synchronizes and checks overlaps without advancing simulation. */
int physics_tick(physics *physics, float delta_seconds);
const char *physics_error(const physics *physics);
/* Optional phase timers for the last physics step; no clock reads when off. */
void physics_set_profiling(physics *physics, bool enabled);
const physics_profile *physics_last_profile(const physics *physics);
physics_hashes physics_hash(const physics *physics);
/* How far presentation moves a root body from its solved position at alpha,
 * without modifying authoritative ECS. False when the entity has no body of
 * its own to interpolate; a child body follows its parent's. */
bool physics_interpolation_offset(const physics *, ecs_entity, float alpha, CF_V2 *out);

typedef struct
{
    float mass, inertia;
    CF_V2 local_center;
    bool awake;
    int shape_count;
} physics_body_state;
/* Current solved body state; local center uses game units. Fixed rotation has
 * zero rotational inertia. A collider carried by its root's body reports that
 * body. Returns false before synchronization or after removal. */
bool physics_inspect_body(const physics *physics, ecs_entity entity, physics_body_state *out);

/* Snapshot current collider contacts into a caller-owned core array. Honors
 * bilateral filters, Joints without collide and pending destruction,
 * including before the first step.
 * Solid contacts include Box2D's 0.02 meter speculative separation; sensors
 * require actual overlap. Reads authored poses, so teleports are immediate. */
int physics_contact_entities(
    ecs_world *world,
    struct entities *entities,
    ecs_entity entity,
    unsigned categories,
    ecs_entity **out
);

typedef struct
{
    bool found;
    ecs_entity entity;
    CF_V2 point, normal;
    float distance;
} ray_hit;

/* Segment query against authored circle/rotated-box colliders, including
 * teleports before the next physics tick. Nearest hit wins; ties use handle
 * order. An origin inside a collider hits at zero with normal against the ray.
 * Categories filter targets independently of their contact masks. */
int physics_raycast(
    ecs_world *world,
    struct entities *entities,
    CF_V2 start,
    CF_V2 end,
    unsigned categories,
    ecs_entity exclude,
    bool sensors,
    ray_hit *out
);
/* Reuse one authored collider snapshot for a batch of visibility/sensor rays.
 * Refresh after movement; queries allocate nothing and do not iterate ECS. */
typedef struct ray_scene ray_scene;
ray_scene *ray_scene_new();
void ray_scene_free(ray_scene *scene);
int ray_scene_refresh(ray_scene *scene, ecs_world *world, struct entities *entities);
int ray_scene_cast(
    const ray_scene *scene,
    CF_V2 start,
    CF_V2 end,
    unsigned categories,
    ecs_entity exclude,
    bool sensors,
    ray_hit *out
);

#endif
