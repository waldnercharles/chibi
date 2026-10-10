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
/* Drops the Box3D world, its bodies and contacts without events; keeps the
 * settings. */
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

#endif
