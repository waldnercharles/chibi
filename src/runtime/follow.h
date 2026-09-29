#ifndef CHIBI_GAME_FOLLOW_H
#define CHIBI_GAME_FOLLOW_H

#include "runtime/scenes/physics.h"

/* Applies every GameFollow after physics, each target before its followers,
 * over `seconds` of simulated time. NULL on success, else why. */
const char *game_follow_update(ecs_world *world, game_physics *physics, float seconds);

#endif
