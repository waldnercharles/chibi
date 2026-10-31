#ifndef CHIBI_RENDER_SCENE3D_GEOMETRY_H
#define CHIBI_RENDER_SCENE3D_GEOMETRY_H

#include "platform/cute.h"

enum
{
    SCENE3D_MESH_TRIANGLE_LIMIT = 144
};

/* Unit extents centered at the origin, with independent flat face normals. */
CF_Mesh render_scene3d_prism_mesh();
/* A unit-diameter sphere with smooth normals, fine enough that a sphere
 * filling much of the screen keeps a round outline. */
CF_Mesh render_scene3d_sphere_mesh();
CF_Mesh render_scene3d_octahedron_mesh();
CF_Mesh render_scene3d_tetrahedron_mesh();
/* Canonical polygon geometry shared by drawing and exact picking. */
int render_scene3d_mesh_triangles(int primitive, CF_V3 triangles[SCENE3D_MESH_TRIANGLE_LIMIT][3]);
CF_Mesh render_scene3d_polygon_mesh(int primitive);

#endif
