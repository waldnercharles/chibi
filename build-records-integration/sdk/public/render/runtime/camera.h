#ifndef CHIBI_CAMERA_H
#define CHIBI_CAMERA_H

#include "render/view.h"
#include "runtime/engine.h"

/* Returns 1 for an authored camera, 0 if absent, -1 for invalid or competing
 * active cameras. Editor and standalone Play use the same framing contract.
 * It propagates transforms first, so the camera frames current placements. */
int camera_view(ecs_world *world, const engine *runtime, engine_viewport_view *out);

#endif
