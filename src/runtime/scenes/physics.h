#ifndef CHIBI_GAME_PHYSICS_H
#define CHIBI_GAME_PHYSICS_H

#include "ecs/ecs.h"
#include "graph/model/node.h"
#include "runtime/components.h"
#include "runtime/tasks.h"

struct game_entities;
typedef struct game_physics game_physics;

typedef struct
{
    double synchronize_ms, solve_ms, sweeps_ms, pairs_ms, writeback_ms, dispatch_ms;
    int bodies, pairs;
} game_physics_profile;

/* Authored lengths use 32 units per meter; velocities use those units/second.
 * One persistent, zero-gravity Box2D world belongs to each live ECS world. */
game_physics *game_physics_new(ecs_world *world, struct game_entities *entities);
void game_physics_free(game_physics *physics);
void game_physics_reset(game_physics *physics);
/* Configure before bodies are synchronized, or immediately after reset.
 * The borrowed executor must outlive this physics world. NULL selects serial. */
void game_physics_set_tasks(game_physics *physics, const game_task_system *tasks);
/* Synchronize authored state, integrate, then queue deduplicated Contact Begin.
 * A zero delta synchronizes and checks overlaps without advancing simulation. */
int game_physics_tick(game_physics *physics, float delta_seconds);
const char *game_physics_error(const game_physics *physics);
/* Optional phase timers for the last physics step; no clock reads when off. */
void game_physics_set_profiling(game_physics *physics, bool enabled);
const game_physics_profile *game_physics_last_profile(const game_physics *physics);
/* Borrow solved poses for presentation without modifying authoritative ECS. */
bool game_physics_interpolate(const game_physics *physics, ecs_entity entity, float alpha, GameTransform *out);

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

#endif
