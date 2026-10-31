#ifndef CHIBI_RENDER_MATERIAL_RASTER_H
#define CHIBI_RENDER_MATERIAL_RASTER_H

#include "platform/cute.h"
#include "render/view_2d.h"
#include "runtime/component_catalog.h"

#include <stdbool.h>
#include <stddef.h>
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
    RENDER_SHAPE_POLY_FILL,
    /* A box showing an image: points 0-1 half extents, 8-11 the frame's uv
     * rect (u0, v0, u1, v1; flipped frames swap u), 12 the image variant, 13
     * and 14 its normal and height map variants or zero, and 15 its relief. */
    RENDER_SHAPE_SPRITE,
    /* Ordered mesh actor; points[0] indexes the draw call's mesh actors. */
    RENDER_SHAPE_MESH
};

/* The fields before material_parameters match shaders/entity.shd's std430
 * layout. Colors are straight linear RGBA, including HDR emission. Points and
 * inverse rotation are in Y-up world units. extent must conservatively include
 * AA and metaball expansion. time[0] is simulation time; the remaining values
 * are reserved by the raster. */
typedef struct
{
    float inv_rot[4], pos[2], extent[2];
    uint32_t shape_type;
    /* relief: world units the shape stands above the plane, which a
     * sprite's height map scales. */
    float radius, thickness, relief;
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
    float time[3];
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
    /* Draw order, never uploaded: lower layers draw first, and equal layers
     * keep the caller's order. */
    float layer;
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
     * coverage is cover's G channel, preserved under fluid. */
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
     * G: geometry coverage for relief height. B: mesh geometry coverage;
     * imported normals need no procedural silhouette bevel. A: reserved. */
    CF_Texture cover;
} render_material_textures;

typedef render_view_2d render_material_view;

typedef struct render_material_raster render_material_raster;

struct render_mesh_surface;
typedef struct
{
    ecs_entity entity;
    Transform position;
    const struct render_mesh_surface *surface; /* Borrowed for this draw. */
    const char *sdf_shape; /* Optional authored detail graph; parameters remain entity-bound. */
} render_mesh_material_actor;

enum
{
    RENDER_SDF_BATCH_LIMIT = 256
};
typedef struct
{
    int draw_calls, sdf_draw_calls, sdf_instances, sdf_uploads;
    int mesh_uploads, dynamic_meshes;
    int surface_builds, surface_reuses, sdf_program_prepares;
    int sdf_lod_instances, surface_pixels, surface_capacity_pixels;
    size_t sdf_upload_bytes;
    size_t sdf_pair_probes, sdf_parameter_probes, sdf_parameter_upload_bytes;
    int sdf_parameter_rows, sdf_parameter_uploads;
    int scratch_groups, depth_copies;
    size_t composition_pixels, depth_copy_pixels;
    /* Current retained GPU cache size and cumulative successful GPU builds. */
    int sdf_program_count;
    unsigned sdf_program_compilations;
} render_mesh_material_work;

typedef struct
{
    int entity_records, parameter_blocks;
    size_t entity_bytes, parameter_bytes, parameter_buffer_bytes;
} render_material_upload_work;
/* Generic 2D uploads from the last draw, excluding mesh/SDF markers. */
render_material_upload_work render_material_raster_upload_work(const render_material_raster *raster);

typedef struct
{
    ecs_entity entity;
    int part; /* -1 selects the complete model. */
    bool section;
    float normal[3], offset; /* Object-space keep dot(p,normal)<=offset. */
} render_model_inspection;
void render_material_raster_set_model_inspection(
    render_material_raster *raster,
    const render_model_inspection *inspection
);

/* Requires Cute app and shader directory set to render/shaders. NULL/false
 * reports unavailable GPU resources. The renderer owns all returned textures.
 * No commands may still reference its old textures when resizing/freeing. */
render_material_raster *render_material_raster_new();
void render_material_raster_free(render_material_raster *raster);
/* Releases a retired mutable surface's retained GPU vertices. */
void render_material_raster_forget_mesh(render_material_raster *raster, uint64_t identity);
const char *render_material_raster_error(const render_material_raster *raster);
bool render_material_raster_resize(render_material_raster *raster, int width, int height);
/* Zero uses the default limit; one permits matched unbatched comparisons. */
void render_material_raster_set_sdf_batch_limit(render_material_raster *raster, int limit);
void render_material_raster_set_quality(render_material_raster *raster, SurfaceQuality quality);
void render_material_raster_set_sdf_trace_debug(render_material_raster *raster, bool enabled);
/* Both optimizations default on; disable for matched rendering comparisons. */
void render_material_raster_set_spatial_scratch(render_material_raster *raster, bool enabled);
void render_material_raster_set_hidden_rejection(render_material_raster *raster, bool enabled);

/* Surface work sums ordered scratch groups; surface_capacity_pixels is the
 * peak retained scratch allocation, reused by those groups. */
render_mesh_material_work render_material_raster_mesh_work(const render_material_raster *raster);
bool render_material_raster_draw(
    render_material_raster *raster,
    const render_material_entity *entities,
    int count,
    render_material_view view
);
bool render_material_raster_draw_mixed(
    render_material_raster *raster,
    const render_material_entity *entities,
    int count,
    render_material_view view,
    ecs_world *world,
    const render_mesh_material_actor *meshes,
    int mesh_count,
    float time
);
render_material_textures render_material_raster_textures(const render_material_raster *raster);
/* The canvas holding those targets, for passes that add to them. */
CF_Canvas render_material_raster_canvas(const render_material_raster *raster);
/* A canvas of the material targets, for passes that rewrite them. */
CF_Canvas render_material_canvas(int width, int height);
render_material_textures render_material_canvas_textures(CF_Canvas canvas);

#endif
