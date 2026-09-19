#ifndef CHIBI_RENDER_GEOMETRY_2D_H
#define CHIBI_RENDER_GEOMETRY_2D_H

#include "runtime/components.h"

#include <stdbool.h>

/* Project a 3D authored primitive onto the gameplay plane. Physics dimensions
 * remain owned by GameCollider; this helper only creates visual SDF geometry. */
bool render_geometry_2d_project(
    const GameGeometry3D *geometry,
    const GameAppearance *fallback,
    const GameVisualTransform *visual,
    float heading,
    GameAppearance *appearance,
    float *angle
);

#endif
