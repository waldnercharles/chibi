#ifndef CHIBI_RENDER_DEBUG_GEOMETRY_H
#define CHIBI_RENDER_DEBUG_GEOMETRY_H
#include "render/scene3d.h"
#include "render/viewport.h"

#include <stdint.h>
typedef struct
{
    CF_V3 a, b;
    uint32_t color;
    ecs_entity entity;
} render_debug_segment;
typedef struct
{
    int segments, unsupported;
    bool truncated, invalid;
} render_debug_geometry_result;
/* Bounded CPU geometry, independent of editor selection/tools. Gameplay2D
 * occupies worldXZ. Collider hull/mesh topology is explicitly unsupported;
 * boxes/spheres/capsules and camera frusta use shared runtime placement/lens.
 * Caller-provided capacity1..8192; at most4096 queried entities. */
render_debug_geometry_result render_debug_geometry_collect(
    ecs_world *,
    const engine *,
    bool colliders,
    bool cameras,
    render_skip,
    render_debug_segment *,
    int capacity
);
/* Optional production-viewport overlay. Near/far clipping refuses segments
 * crossing the clip planes rather than projecting behind-camera coordinates.
 * Source generation scopes and current canvas are owned by caller. */
render_debug_geometry_result render_debug_geometry_draw(
    ecs_world *,
    const engine *,
    engine_viewport *,
    bool colliders,
    bool cameras,
    render_skip
);
#endif
