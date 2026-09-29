#ifndef CHIBI_RENDER_MATERIAL_RASTER_H
#define CHIBI_RENDER_MATERIAL_RASTER_H

#include "platform/cute.h"
#include "render/view_2d.h"

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
 * remaining values are reserved by the raster. Equal sort keys retain caller
 * order. */
typedef struct
{
    float inv_rot[4], pos[2], extent[2];
    uint32_t shape_type;
    float radius, thickness, chubbiness;
    float aa;
    uint32_t point_count, metaball_group;
    float metaball_blend;
    /* reserved[0/1] select catalog material/shape; zero uses authored data.
     * reserved[2] is received-light gain near the silhouette. */
    float border_thickness, reserved[3];
    /* Boxes: 0-1 half extents, and 4-7 an optional cut (normal, offset, 1)
     * that keeps the side where dot(normal, p) <= offset. */
    float points[16];
    float scene[4], emission[4], absorption[4], albedo[4];
    float border_scene[4], border_emission[4], border_absorption[4],
        border_albedo[4];
    float wobble[4], pluck[4], time[3];
    /* Nonzero for boxes that tile one shape without overlapping, such as a
     * material mask's cells. Consecutive pieces that share it add coverage.
     * Pieces
     * with a whole number plus a half, such as a mask's border, come just
     * before the pieces with that whole number and lie over them. */
    float tile_group;
    /* Y-up position and cos/sin of the rotation where the local frame sat when
     * the material was laid out. Zero cos and sin sample materials at the
     * pixel's current world position. */
    float material_frame[4];
    /* Uploaded separately: Cute storage-buffer structs cannot contain arrays. */
    float material_parameters[32][4], shape_parameters[32][4];
} render_material_entity;

/* The material raster's RGBA16F render targets. */
typedef struct
{
    /* Premultiplied color; A coverage. */
    CF_Texture scene;
    /* RGB emission density and A optical depth per pixel, the transport's
     * sources; see hrc/source_average.shd. Mipmapped. */
    CF_Texture source;
    /* RGB albedo; A material height (world units) times coverage, whose
     * coverage is scene's alpha where no fluid covers the solid. */
    CF_Texture albedo;
    /* Solids: xy bump tilt in world axes (Y up), z edge-light gain, w gloss
     * (one minus roughness). Liquids: xy surface normal, negative z refraction
     * strength, w surface confidence. */
    CF_Texture normal;
    /* Fluid's camera-only readability floor in RGB and coverage for display
     * exposure in A, excluded from all HRC feedback; zero without fluid. */
    CF_Texture visibility;
    /* R: display's scale from source's emission density to emission
     * premultiplied by coverage. Absorbers emit their radiance times their
     * optical depth (hrc/source_average.shd); clear emitters' scale is one.
     * G: output pixels solids refract along their tilt. B: heat haze weight
     * of burning and emissive fluid, at most one. A: zero. */
    CF_Texture cover;
} render_material_textures;

typedef render_view_2d render_material_view;

typedef struct render_material_raster render_material_raster;

/* Requires Cute app and shader directory set to render/shaders. NULL/false
 * reports unavailable GPU resources. The renderer owns all returned textures.
 * No commands may still reference its old textures when resizing/freeing. */
render_material_raster *render_material_raster_new();
void render_material_raster_free(render_material_raster *raster);
const char *render_material_raster_error(const render_material_raster *raster);
bool render_material_raster_resize(render_material_raster *raster, int width, int height);
bool render_material_raster_draw(
    render_material_raster *raster,
    const render_material_entity *entities,
    int count,
    render_material_view view
);
render_material_textures render_material_raster_textures(const render_material_raster *raster);
/* The canvas holding those targets, for passes that add to them. */
CF_Canvas render_material_raster_canvas(const render_material_raster *raster);
/* A canvas of the material targets, for passes that rewrite them. */
CF_Canvas render_material_canvas(int width, int height);
render_material_textures render_material_canvas_textures(CF_Canvas canvas);

#endif
