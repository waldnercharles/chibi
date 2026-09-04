#ifndef CBLUEPRINT_GAME_GAMEPLAY_H
#define CBLUEPRINT_GAME_GAMEPLAY_H

#include "generated.h"

/* Deterministic, ECS-backed first gameplay slice. Structural changes are
 * recorded and applied by the caller's normal sync point. */
int game_gameplay_fire(ecs_world *world, ecs_entity owner);
int game_gameplay_tick(ecs_world *world, double delta_seconds);

#endif
