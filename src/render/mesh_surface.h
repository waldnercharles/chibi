#ifndef CHIBI_RENDER_MESH_SURFACE_H
#define CHIBI_RENDER_MESH_SURFACE_H

#include "render/arena_visual.h"
#include "render/mesh_asset.h"

typedef struct
{
    arena_visual visual;
    /* Primitive-local triangle list. Owned array; model includes dimensions. */
    render_mesh_surface_vertex *vertices;
    uint64_t asset_identity; /* Zero for generated primitive geometry. */
    bool borrowed;           /* Catalog-owned vertices when true. */
} render_mesh_surface_part;

typedef struct
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
bool render_mesh_surface_build(
    const ecs_world *world,
    ecs_entity entity,
    const GameTransform *position,
    render_mesh_surface *surface,
    char *error,
    size_t size
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
