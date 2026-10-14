#ifndef CHIBI_RENDER_GEOMETRY_2D_H
#define CHIBI_RENDER_GEOMETRY_2D_H

#include "ecs/ecs.h"
#include "render/skip.h"
#include "runtime/components.h"

#include <stdbool.h>

/* Project a 3D authored primitive onto the gameplay plane. Physics dimensions
 * remain owned by Collider; this helper only creates visual SDF geometry. */
bool render_geometry_2d_project(
    const Geometry3D *geometry,
    const Appearance *fallback,
    const VisualTransform *visual,
    float heading,
    Appearance *appearance,
    float *angle
);

/* Exact projected mesh bounds for enabled Mesh Surface 2D entities. Returned
 * points are in gameplay coordinates; unmarked entities return false. */
bool render_geometry_2d_mesh_bounds(
    const ecs_world *world,
    ecs_entity entity,
    const Transform *position,
    CF_V2 *min,
    CF_V2 *max,
    char *error,
    size_t size
);

/* Gameplay-space body bounds at exact hierarchical placement. Excludes glow;
 * outputs remain unchanged for missing/invalid geometry. */
bool render_geometry_2d_bounds(const ecs_world *world, ecs_entity entity, CF_V2 *min, CF_V2 *max);

/* Pick visible 2D body geometry at a gameplay (Y-down) point. Border/glow are
 * excluded; tilemaps use occupied rectangular extents, mesh/SDF surfaces use
 * their projected triangles, and custom shapes use their CPU sampler. Highest
 * authored layer wins, then stable entity slot order. skip is sorted/borrowed.
 * Call within the owning resource and declared-component scopes. */
ecs_entity render_geometry_2d_pick(ecs_world *world, CF_V2 point, float time, render_skip skip);

#endif
