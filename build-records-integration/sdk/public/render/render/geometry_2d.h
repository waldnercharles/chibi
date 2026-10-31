#ifndef CHIBI_RENDER_GEOMETRY_2D_H
#define CHIBI_RENDER_GEOMETRY_2D_H

#include "ecs/ecs.h"
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

#endif
