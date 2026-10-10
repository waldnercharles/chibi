#ifndef CHIBI_GAME_PHYSICS3D_H
#define CHIBI_GAME_PHYSICS3D_H

#include "ecs/ecs.h"
#include "runtime/components.h"
#include "runtime/scenes/physics_settings.h"

struct game_entities;
typedef struct game_physics3d game_physics3d;

/* Simulates Collider3D entities in a Box3D world, which exists from the first
 * collider until reset, so a world without colliders costs one empty query
 * per step. Lengths are project units, 32 per meter. */
game_physics3d *game_physics3d_new(ecs_world *world, struct game_entities *entities);
void game_physics3d_free(game_physics3d *physics);
/* Drops the Box3D world, its bodies, contacts and pushes without events;
 * keeps the settings. */
void game_physics3d_reset(game_physics3d *physics);
/* Applies the workers, substeps, sleep, continuous and contact settings;
 * force_drive is 2D only. Worker changes require a reset world. Invalid
 * settings fail without changing any. */
int game_physics3d_configure(game_physics3d *physics, game_physics_settings settings);
/* Synchronizes bodies from their components, steps, and writes moving root
 * bodies back. Then it queues Contact End 3D and Contact Begin 3D, in that
 * order, to both entities of each pair that stopped or started touching,
 * pairs ordered by entity handle. Solid contacts include Box3D's speculative
 * margin; sensors need overlap. A zero delta synchronizes and refreshes
 * contacts without advancing. */
int game_physics3d_tick(game_physics3d *physics, float delta_seconds);
const char *game_physics3d_error(const game_physics3d *physics);
/* Push entity's dynamic Body3D through its center of mass in the next step
 * that advances. An impulse changes its velocity by impulse over its mass at
 * once; a force accelerates it by force over its mass through that step.
 * Bodies weigh 1000 kg per cubic meter, Box3D's density of water. Pushes add
 * up, and a push on a body destroyed or no longer dynamic by then is dropped.
 * FLOW_ERR_ARGUMENT for a non-finite vector or an entity without a dynamic
 * Body3D. */
int game_physics3d_apply_impulse(game_physics3d *physics, ecs_entity entity, CF_V3 impulse);
int game_physics3d_apply_force(game_physics3d *physics, ecs_entity entity, CF_V3 force);
/* How presentation moves the entity's own body from its solved pose at alpha:
 * shown = rotation * solved + translation, in world space. The identity when
 * the entity's Transform3D changed after the step. False when the entity has
 * no moving body of its own; a follower moves with its parent's. */
bool game_physics3d_interpolation(
    const game_physics3d *physics,
    ecs_entity entity,
    float alpha,
    CF_Quat *rotation,
    CF_V3 *translation
);

/* The first collider a raycast or shape cast reaches. */
typedef struct
{
    bool found;
    ecs_entity entity;
    CF_V3 point, normal; /* In the world. */
    float distance;      /* From the start, in project units. */
} game_physics3d_hit;

/* Queries read Transform3D and Collider3D as they are now, so they see this
 * frame's spawns and moves before any step. Colliders on one body never meet
 * each other: a child collider shares the body of the top of its Transform3D
 * hierarchy when that has a collider. Each scans every collider and returns
 * FLOW_ERR_ARGUMENT for a coordinate beyond 100 km or an invalid collider.
 * They build a hull or mesh collider's collision data as the simulation
 * would and keep it until a reset. */

/* The first collider in categories on the segment from start to end. It skips
 * sensors unless asked, and every collider on exclude's body. A ray that
 * starts inside a collider hits it at start with a zero normal. */
int game_physics3d_raycast(
    game_physics3d *physics,
    CF_V3 start,
    CF_V3 end,
    unsigned categories,
    ecs_entity exclude,
    bool sensors,
    game_physics3d_hit *out
);
/* The first collider in categories that caster's collider meets when moved by
 * translation without turning; the hit stops within Box3D's linear slop, .16
 * units, of touching. It skips colliders on the caster's body and sensors
 * unless asked. A caster that starts touching or overlapping a collider, as a
 * body resting on the ground does, hits it at distance zero with a zero
 * normal; leave the ground's category out to slide along it. A caster without
 * a live collider finds nothing, and a mesh caster is an error. */
int game_physics3d_shape_cast(
    game_physics3d *physics,
    ecs_entity caster,
    CF_V3 translation,
    unsigned categories,
    bool sensors,
    game_physics3d_hit *out
);
/* Replaces out with the colliders in categories touching entity's collider,
 * in query order, as the simulation would let them touch: on another body,
 * with each one's category in the other's mask, overlapping a sensor or
 * within Box3D's speculative margin of a solid. Two meshes never touch. */
int game_physics3d_contact_entities(
    game_physics3d *physics,
    ecs_entity entity,
    unsigned categories,
    ecs_entity **out
);

#endif
