#ifndef CHIBI_RENDER_ARENA_GEOMETRY_H
#define CHIBI_RENDER_ARENA_GEOMETRY_H

#include "platform/cute.h"

enum
{
    ARENA_MESH_TRIANGLE_LIMIT = 144
};

/* Unit extents centered at the origin, with independent flat face normals. */
CF_Mesh render_arena_prism_mesh();
CF_Mesh render_arena_octahedron_mesh();
CF_Mesh render_arena_tetrahedron_mesh();
/* Canonical polygon geometry shared by drawing and exact picking. */
int render_arena_mesh_triangles(int primitive, CF_V3 triangles[ARENA_MESH_TRIANGLE_LIMIT][3]);
CF_Mesh render_arena_polygon_mesh(int primitive);

#endif
