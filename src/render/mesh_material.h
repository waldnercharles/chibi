#ifndef CHIBI_RENDER_MESH_MATERIAL_H
#define CHIBI_RENDER_MESH_MATERIAL_H
#include "render/material_raster.h"
#include "runtime/generated.h"
typedef struct render_mesh_material render_mesh_material;
typedef struct
{
    CF_Texture scene, emission, absorption, albedo, normal;
} render_mesh_material_textures;
render_mesh_material *render_mesh_material_new();
void render_mesh_material_free(render_mesh_material *pass);
bool render_mesh_material_resize(render_mesh_material *pass, int width, int height);
bool render_mesh_material_draw(
    render_mesh_material *pass,
    ecs_world *world,
    const render_mesh_material_actor *actors,
    int count,
    render_material_view view,
    float time
);
const char *render_mesh_material_error(const render_mesh_material *pass);
render_mesh_material_textures render_mesh_material_get_textures(const render_mesh_material *pass);
/* Pixel-aligned destination rectangle. Scratch has twice these dimensions.
 * All five targets store raw premultiplied material layers, not lit color.
 * Albedo alpha is world elevation plus graph height. Normal is (X,-Z,gain,gloss).
 * All actors share depth; scratch clears once for the group's first draw.
 * Scratch capacity can extend outside the group and destination; clear is zero. */
void render_mesh_material_rect(const render_mesh_material *pass, float rect[4]);
#endif
