#ifndef CHIBI_RENDER_MESH_PICK_H
#define CHIBI_RENDER_MESH_PICK_H

#include "runtime/geometry/mesh_asset.h"

/* Where a ray meets a triangle from either side, or FLT_MAX. The ray counts
 * as parallel relative to the edges' lengths, so a tiny triangle that a
 * Geometry3D scales up still takes picks. */
float render_triangle_hit(CF_Ray3 ray, const CF_V3 triangle[3]);

typedef struct
{
    float t; /* Along the ray to the nearest triangle it meets, or FLT_MAX. */
    int tested; /* Triangles the pick tested on the way. */
} render_mesh_hit;
/* Where a ray in a resident mesh's coordinates first meets one of its
 * triangles, from either side. A tree of boxes over the mesh's triangles,
 * built on its first pick and kept until the mesh catalog changes, limits
 * the test to triangles in boxes the ray crosses. */
render_mesh_hit render_mesh_pick(const render_mesh_asset *mesh, CF_Ray3 ray);

#endif
