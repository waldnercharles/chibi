#ifndef CHIBI_RENDER_MESH_SURFACE_H
#define CHIBI_RENDER_MESH_SURFACE_H
#include "render/scene3d_visual.h"
#include "runtime/geometry/mesh_asset.h"
#include "runtime/models/model_program.h"

typedef struct
{
    scene3d_visual visual;
    CF_V3 sdf_center; /* Model-local bound center; raw shapes remain centered. */
    /* Primitive-local vertices; model includes dimensions. Owned unless
     * `borrowed`. With `indices`, the triangles' corners index it; otherwise
     * it is a triangle list. */
    render_mesh_surface_vertex *vertices;
    const int *indices; /* Borrowed catalog indices, or NULL. */
    /* Explicit slices for catalog material groups: the triangle corner count,
     * and the slice's first index in the asset. */
    int corner_count, first_index;
    bool part_coordinates, uv_bounds;
    CF_V3 uv_min, uv_max;
    const char *material_scope; /* Borrowed catalog slot name, empty for entity fallback. */
    uint64_t asset_identity; /* Zero for generated primitive geometry. */
    bool borrowed; /* Catalog or immutable shared storage when true. */
} render_mesh_surface_part;

typedef struct render_mesh_surface
{
    render_mesh_surface_part *parts;
    CF_V3 min, max; /* World X, elevation Y, gameplay Y in Z. */
} render_mesh_surface;

/* Output arrays use support/array.h. Unsupported primitives report an error.
 * The caller starts with zero-initialized storage and owns it until free. */
bool render_mesh_surface_vertices(
    int primitive,
    render_mesh_surface_vertex **vertices,
    char *error,
    size_t size
);
/* With a `transform`, a global 3D transform, the surface stands there as
 * render_scene3d_visual_frame places it; otherwise at `position`. */
bool render_mesh_surface_build_with_bounds(
    const ecs_world *world,
    ecs_entity entity,
    const GameTransform *position,
    const GameGlobalTransform3D *transform,
    const model_bounds *bounds,
    render_mesh_surface *surface,
    char *error,
    size_t size
);
bool render_mesh_surface_build(
    const ecs_world *world,
    ecs_entity entity,
    const GameTransform *position,
    render_mesh_surface *surface,
    char *error,
    size_t size
);
const char *render_mesh_surface_lod_shape(const GameSdfSurface3D *sdf, float pixels, bool disabled);
/* How many triangle corners the part draws: three per triangle. */
int render_mesh_surface_corner_count(const render_mesh_surface_part *part);
/* The vertex at a part's triangle corner. */
const render_mesh_surface_vertex *render_mesh_surface_corner(
    const render_mesh_surface_part *part,
    int corner
);
void render_mesh_surface_free(render_mesh_surface *surface);
bool render_mesh_surface_bounds(
    const ecs_world *world,
    ecs_entity entity,
    const GameTransform *position,
    CF_V2 *min,
    CF_V2 *max,
    char *error,
    size_t size
);
/* Exact projected triangle hit; returns the frontmost (+Y) elevation. */
bool render_mesh_surface_pick(const render_mesh_surface *surface, CF_V2 position, float *height);
CF_V3 render_mesh_surface_normal(CF_M4x4 model, CF_V3 normal);

#endif
