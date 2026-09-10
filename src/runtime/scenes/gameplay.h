#ifndef CHIBI_GAME_GAMEPLAY_H
#define CHIBI_GAME_GAMEPLAY_H

#include "ecs/ecs.h"

#include <cute_math.h>

struct game_behavior_world;

/* Deliver a sampled direction only to current GameInput members. Held movement
 * fires once per sample; zero fires once when a member stops moving. */
int game_movement_dispatch(ecs_world *world, struct game_behavior_world *behaviors, CF_V2 direction);

#endif
