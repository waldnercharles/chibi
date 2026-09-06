#ifndef CBLUEPRINT_RENDER_MATERIAL_RASTER_H
#define CBLUEPRINT_RENDER_MATERIAL_RASTER_H

#include <cute_graphics.h>
#include <stdbool.h>
#include <stdint.h>

/* Source Neon-Swarm shape identifiers; polygons cover both chevron variants. */
enum
{
    RENDER_SHAPE_CIRCLE,
    RENDER_SHAPE_BOX,
    RENDER_SHAPE_CAPSULE,
    RENDER_SHAPE_LINE,
    RENDER_SHAPE_TRI,
    RENDER_SHAPE_POLY,
    RENDER_SHAPE_DIAMOND,
    RENDER_SHAPE_POLY_FILL
};

/* Matches shaders/entity.shd std430 layout. Colors are straight linear RGBA,
 * including HDR emission. Points and inverse rotation are in Y-up world units.
 * extent must conservatively include AA, deformation and metaball expansion.
 * wobble = amplitude, angular frequency, temporal frequency, sort key.
 * pluck = amplitude, origin, age, speed. time[0] is simulation time;
 * remaining padding is reserved. Equal sort keys retain caller order. */
typedef struct
{
    float inv_rot[4], pos[2], extent[2];
    uint32_t shape_type;
    float radius, thickness, chubbiness;
    float aa;
    uint32_t point_count, metaball_group;
    float metaball_blend;
    float border_thickness, reserved[3];
    float points[16];
    float scene[4], emission[4], transmittance[4], albedo[4];
    float border_scene[4], border_emission[4], border_transmittance[4],
        border_albedo[4];
    float wobble[4], pluck[4], time[4];
} render_material_entity;

typedef struct
{
    CF_Texture scene, emission, transmittance, albedo;
} render_material_textures;

typedef struct
{
    float min_x, max_y, width, height;
} render_material_view;

typedef struct render_material_raster render_material_raster;

/* Requires Cute app and shader directory set to render/shaders. NULL/false
 * reports unavailable GPU resources. The renderer owns all returned textures.
 * No commands may still reference its old textures when resizing/freeing. */
render_material_raster *render_material_raster_new();
void render_material_raster_free(render_material_raster *raster);
bool render_material_raster_resize(render_material_raster *raster, int width, int height);
bool render_material_raster_draw(
    render_material_raster *raster,
    const render_material_entity *entities,
    int count,
    render_material_view view
);
render_material_textures render_material_raster_textures(const render_material_raster *raster);

#endif
