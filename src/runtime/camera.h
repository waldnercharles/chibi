#ifndef CHIBI_CAMERA_H
#define CHIBI_CAMERA_H

#include "render/view.h"
#include "runtime/engine.h"

/* Frames one camera, including a disabled camera the editor previews. Returns
 * false for an invalid camera; propagates current placements before reading. */
bool camera_entity_view(ecs_world *world, const engine *runtime, ecs_entity entity, engine_viewport_view *out);

/* Returns 1 for an authored camera, 0 if absent, -1 for invalid or competing
 * active cameras. Editor and standalone Play use the same framing contract.
 * It propagates transforms first, so the camera frames current placements. */
int camera_view(ecs_world *world, const engine *runtime, engine_viewport_view *out);

#endif
