#ifndef CHIBI_GAME_CAMERA_H
#define CHIBI_GAME_CAMERA_H

#include "runtime/engine.h"

/* Returns 1 for an authored camera, 0 if absent, -1 for invalid or competing
 * active cameras. Editor and standalone Play use the same framing contract. */
int game_camera_view(ecs_world *world, const engine *runtime, engine_viewport_view *out);

#endif
