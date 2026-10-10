#ifndef CHIBI_MESH_PICK_H
#define CHIBI_MESH_PICK_H

#include "runtime/geometry/mesh_asset.h"

/* Where a ray meets a triangle from either side, or FLT_MAX. Like the ray's
 * reach `t`, the hit counts in multiples of its direction, which need not be
 * unit length. The ray counts as parallel relative to the lengths of the
 * direction and the edges, so a tiny triangle that a Geometry3D scales up
 * still takes picks. */
float render_triangle_hit(CF_Ray3 ray, const CF_V3 triangle[3]);

typedef struct
{
    float t; /* Along the ray to the nearest triangle it meets, or FLT_MAX. */
    int tested; /* Triangles the pick tested on the way. */
} render_mesh_hit;
/* Where a ray in a resident mesh's coordinates first meets one of its
 * triangles, from either side, in multiples of its direction as
 * render_triangle_hit counts them. A tree of boxes over the mesh's triangles,
 * built on its first pick, limits the test to triangles in boxes the ray
 * crosses. */
render_mesh_hit render_mesh_pick(const render_mesh_asset *mesh, CF_Ray3 ray);

/* Frees every mesh's pick tree. The mesh catalog calls it whenever it
 * changes, since a change may drop or replace any mesh. */
void render_mesh_picks_clear();
/* Meshes with a pick tree, for tests. */
int render_mesh_pick_trees();

#endif
