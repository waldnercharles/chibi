#ifndef CHIBI_RENDER_MATERIAL_MESH_H
#define CHIBI_RENDER_MATERIAL_MESH_H

#include "render/material_raster.h"
#include "render/mesh_surface.h"
#include "runtime/material_mask.h"

typedef struct render_material_mesh_cache render_material_mesh_cache;

render_material_mesh_cache *render_material_mesh_cache_new();
/* Retires CPU and optional planar GPU storage. NULL raster selects a CPU-only
 * owner whose renderer retires its own GPU copies independently. */
void render_material_mesh_cache_clear(render_material_mesh_cache *, render_material_raster *);
void render_material_mesh_cache_free(render_material_mesh_cache *, render_material_raster *);
void render_material_mesh_cache_begin(render_material_mesh_cache *);
void render_material_mesh_cache_end(render_material_mesh_cache *, render_material_raster *);
/* The same owned geometry exposed for another renderer, borrowed until this
 * entry rebuilds or retires. Its revision matches the returned surface part. */
const render_mesh_asset *render_material_mesh_cache_asset(
    const render_material_mesh_cache *,
    uint64_t incarnation,
    uint64_t *revision
);
/* Borrowed stable surface until retirement. Placement changes update the frame
 * without rebuilding or uploading vertices. Mask/options/source-frame changes
 * rebuild the derived geometry and advance its GPU revision. */
const render_mesh_surface *render_material_mesh_cache_get(
    render_material_mesh_cache *,
    material_occupancy_view,
    MaterialSurface3D,
    MaterialFrame,
    GlobalTransform2D,
    ecs_world *,
    ecs_entity,
    char *error,
    size_t size
);

#endif
