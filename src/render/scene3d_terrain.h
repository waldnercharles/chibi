#ifndef CHIBI_RENDER_SCENE3D_TERRAIN_H
#define CHIBI_RENDER_SCENE3D_TERRAIN_H

#include "render/material_mesh.h"

#define RENDER_TERRAIN_GI_PROXY_LIMIT 4096
typedef struct render_scene3d_terrain render_scene3d_terrain;
typedef struct
{
    const render_mesh_asset *mesh;
    CF_M4x4 frame;
    /* Mask-local unit-box transforms. Their union covers only occupied cells;
     * unlike an enclosing cave box it cannot bridge a flight opening. */
    const CF_M4x4 *proxies;
    int proxy_count;
    uint64_t identity, revision;
} render_scene3d_terrain_surface;
render_scene3d_terrain *render_scene3d_terrain_new();
void render_scene3d_terrain_free(render_scene3d_terrain *);
void render_scene3d_terrain_reset(render_scene3d_terrain *);
void render_scene3d_terrain_begin(render_scene3d_terrain *);
void render_scene3d_terrain_end(render_scene3d_terrain *);
int render_scene3d_terrain_rebuilds(const render_scene3d_terrain *);
const render_scene3d_terrain_surface *render_scene3d_terrain_get(
    render_scene3d_terrain *,
    const material_mask *,
    MaterialSurface3D,
    MaterialFrame,
    GlobalTransform2D,
    ecs_world *,
    ecs_entity,
    bool indirect,
    char *,
    size_t
);

#endif
