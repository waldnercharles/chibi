#ifndef CHIBI_RENDER_MESH_MATERIAL_H
#define CHIBI_RENDER_MESH_MATERIAL_H
#include "render/material_raster.h"
#include "runtime/component_catalog.h"
typedef struct render_mesh_material render_mesh_material;
typedef struct
{
    CF_Texture scene, emission, absorption, albedo, normal;
} render_mesh_material_textures;
render_mesh_material *render_mesh_material_new();
void render_mesh_material_free(render_mesh_material *pass);
void render_mesh_material_forget(render_mesh_material *pass, uint64_t identity);
bool render_mesh_material_resize(render_mesh_material *pass, int width, int height);
bool render_mesh_material_draw(
    render_mesh_material *pass,
    ecs_world *world,
    const render_mesh_material_actor *actors,
    int count,
    render_material_view view,
    float time
);
/* 0 selects the default 256; 1 draws each SDF separately with the same shader. */
void render_mesh_material_set_model_inspection(
    render_mesh_material *pass,
    const render_model_inspection *inspection
);
void render_mesh_material_set_sdf_batch_limit(render_mesh_material *pass, int limit);
void render_mesh_material_set_quality(render_mesh_material *pass, SurfaceQuality quality);
/* 0: shaded output; 1: surviving trace/normal calls; 2: include hidden rejects. */
void render_mesh_material_set_sdf_trace_debug(render_mesh_material *pass, int mode);
void render_mesh_material_set_hidden_rejection(render_mesh_material *pass, bool enabled);
render_mesh_material_work render_mesh_material_last_work(const render_mesh_material *pass);
const char *render_mesh_material_error(const render_mesh_material *pass);
render_mesh_material_textures render_mesh_material_get_textures(const render_mesh_material *pass);
/* Pixel-aligned destination rectangle. Scratch uses the selected sample scale.
 * All five targets store raw premultiplied material layers, not lit color.
 * Albedo alpha is camera-plane elevation plus graph height. Normal stores
 * camera-plane (right,-down,gain,gloss); graph contexts remain world-space.
 * All actors share depth; scratch clears once for the group's first draw.
 * Active pixels start at target origin. Allocation capacity may be larger. */
void render_mesh_material_rect(const render_mesh_material *pass, float rect[4]);
/* Raw target pixels; either output may be NULL. Sample only the active extent. */
void render_mesh_material_extent(const render_mesh_material *pass, int active[2], int capacity[2]);
#endif
