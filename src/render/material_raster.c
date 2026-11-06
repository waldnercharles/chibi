#include "render/material_raster.h"

#include "render/fullscreen.h"
#include "render/gpu_timing.h"
#include "render/material.h"
#include "render/mesh_material.h"
#include "render/mesh_projection.h"
#include "render/mesh_surface.h"
#include "render/shader_catalog.h"
#include "render/shader_program.h"
#include "render/spatial_groups.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The GPU entity is the prefix before the separately uploaded parameters:
 * the material's, then the shape's. The layer after them stays on the CPU. */
enum
{
    GPU_ENTITY_BYTES = offsetof(render_material_entity, material_parameters),
    PARAMETER_BYTES = offsetof(render_material_entity, layer) - GPU_ENTITY_BYTES
};
_Static_assert(offsetof(render_material_entity, points) == 80, "entity point offset");
_Static_assert(offsetof(render_material_entity, scene) == 144, "entity material offset");
_Static_assert(offsetof(render_material_entity, time) == 272, "entity clock offset");
_Static_assert(GPU_ENTITY_BYTES == 304, "entity std430 stride");
_Static_assert(PARAMETER_BYTES == 1024, "material and shape parameters");

/* A fragment shader binds at most 16 samplers. On GLES3, Cute binds the
 * raster's four storage buffers as textures among them. */
enum
{
    FRAGMENT_SAMPLERS = 16,
    GLES_STORAGE_TEXTURES = 4
};

typedef struct
{
    int first, last, group, program;
    bool mesh;
    render_shader_set set;
} material_batch;

typedef struct
{
    float layer;
    int index;
} draw_order;

struct render_material_raster
{
    /* The first batch's programs, and later batches', which continue from
     * the previous batch's targets. */
    render_programs *programs, *accumulate, *mesh_composite;
    render_mesh_material *mesh_pass;
    render_mesh_material_actor *group_meshes, *batch_meshes;
    render_mesh_surface *spatial_surfaces;
    render_spatial_actor *spatial_actors;
    render_pixel_rect *spatial_rects;
    int *spatial_membership, *spatial_offsets, *spatial_order, *spatial_cursor;
    bool disable_spatial_scratch, disable_hidden_rejection;
    render_model_inspection inspection;
    material_batch *batches;
    char error[256];
    CF_Material material;
    CF_Mesh mesh;
    CF_StorageBuffer entities, offsets, indices, parameters;
    unsigned char *entity_data;
    float *parameter_data;
    int *gpu_indices;
    int parameter_capacity, parameter_buffer_bytes;
    render_material_upload_work upload_work;
    uint32_t *tile_offsets, *tile_cursor, *tile_indices;
    /* Batches alternate between these; see render_material_textures. The
     * second is made when a frame first has several batches. */
    CF_Canvas canvases[2];
    int result;
    render_material_entity *sorted;
    /* Sorting scratch: layers and source indices, cheaper to move than entities. */
    draw_order *order;
    int width, height, tiles, capacity, index_capacity;
    int sdf_batch_limit;
    SurfaceQuality quality;
    bool sdf_trace_debug;
    render_mesh_material_work mesh_work;
};

void render_material_raster_set_spatial_scratch(render_material_raster *r, bool enabled)
{
    r->disable_spatial_scratch = !enabled;
}
void render_material_raster_set_hidden_rejection(render_material_raster *r, bool enabled)
{
    r->disable_hidden_rejection = !enabled;
    if (r->mesh_pass) {
        render_mesh_material_set_hidden_rejection(r->mesh_pass, enabled);
    }
}

void render_material_raster_set_model_inspection(
    render_material_raster *r,
    const render_model_inspection *inspection
)
{
    r->inspection = *inspection;
    if (r->mesh_pass) {
        render_mesh_material_set_model_inspection(r->mesh_pass, inspection);
    }
}

void render_material_raster_set_sdf_batch_limit(render_material_raster *r, int limit)
{
    assert(limit >= 0 && limit <= RENDER_SDF_BATCH_LIMIT);
    r->sdf_batch_limit = limit;
    if (r->mesh_pass) {
        render_mesh_material_set_sdf_batch_limit(r->mesh_pass, limit);
    }
}

void render_material_raster_set_quality(render_material_raster *r, SurfaceQuality quality)
{
    r->quality = quality;
    if (r->mesh_pass) {
        render_mesh_material_set_quality(r->mesh_pass, quality);
    }
}

void render_material_raster_set_sdf_trace_debug(render_material_raster *r, bool enabled)
{
    r->sdf_trace_debug = enabled;
    if (r->mesh_pass) {
        render_mesh_material_set_sdf_trace_debug(r->mesh_pass, enabled);
    }
}

void render_material_raster_forget_mesh(render_material_raster *r, uint64_t identity)
{
    if (r->mesh_pass) {
        render_mesh_material_forget(r->mesh_pass, identity);
    }
}

render_mesh_material_work render_material_raster_mesh_work(const render_material_raster *r)
{
    render_mesh_material_work work = r->mesh_work;
    if (r->mesh_pass) {
        render_mesh_material_work cache = render_mesh_material_last_work(r->mesh_pass);
        work.sdf_program_count = cache.sdf_program_count;
        work.sdf_program_compilations = cache.sdf_program_compilations;
        work.dynamic_meshes = cache.dynamic_meshes;
    }
    return work;
}

static void free_targets(render_material_raster *r)
{
    for (int i = 0; i < 2; i++) {
        if (r->canvases[i].id) {
            cf_destroy_canvas(r->canvases[i]);
        }
        r->canvases[i] = (CF_Canvas){ 0 };
    }
    if (r->offsets.id) {
        cf_destroy_storage_buffer(r->offsets);
    }
    if (r->indices.id) {
        cf_destroy_storage_buffer(r->indices);
    }
    if (r->entities.id) {
        cf_destroy_storage_buffer(r->entities);
    }
    if (r->parameters.id) {
        cf_destroy_storage_buffer(r->parameters);
    }
    r->offsets = r->indices = r->entities = r->parameters = (CF_StorageBuffer){ 0 };
    r->width = r->height = r->tiles = r->capacity = r->index_capacity = 0;
    r->parameter_capacity = r->parameter_buffer_bytes = 0;
}

render_material_raster *render_material_raster_new()
{
    render_material_raster *r = xcalloc(1, sizeof *r);
    r->programs = render_programs_new(RENDER_PROGRAM_RASTER);
    r->accumulate = render_programs_new(RENDER_PROGRAM_RASTER_ACCUMULATE);
    r->material = cf_make_material();
    r->mesh = render_make_pass_mesh();
    if (!r->material.id || !r->mesh.id) {
        render_material_raster_free(r);
        return NULL;
    }
    return r;
}

void render_material_raster_free(render_material_raster *r)
{
    if (!r) {
        return;
    }
    free_targets(r);
    render_programs_free(r->programs);
    render_programs_free(r->accumulate);
    render_programs_free(r->mesh_composite);
    render_mesh_material_free(r->mesh_pass);
    if (r->mesh.id) {
        cf_destroy_mesh(r->mesh);
    }
    afree(r->batches);
    if (r->material.id) {
        cf_destroy_material(r->material);
    }
    afree(r->sorted);
    afree(r->order);
    afree(r->group_meshes);
    afree(r->batch_meshes);
    for (int i = 0; i < acount(r->spatial_surfaces); i++) {
        render_mesh_surface_free(&r->spatial_surfaces[i]);
    }
    afree(r->spatial_surfaces);
    afree(r->spatial_actors);
    afree(r->spatial_rects);
    afree(r->spatial_membership);
    afree(r->spatial_offsets);
    afree(r->spatial_order);
    afree(r->spatial_cursor);
    afree(r->tile_offsets);
    afree(r->tile_cursor);
    afree(r->tile_indices);
    free(r->entity_data);
    free(r->parameter_data);
    afree(r->gpu_indices);
    free(r);
}

static CF_StorageBuffer buffer(int bytes)
{
    CF_StorageBufferParams params = cf_storage_buffer_defaults(bytes);
    return cf_make_storage_buffer(params);
}

bool render_material_raster_resize(render_material_raster *r, int width, int height)
{
    assert(r);
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        return false;
    }
    if (r->width == width && r->height == height) {
        return true;
    }
    /* Its material keeps last frame's bindings, which WebGL validates on
       each draw; draws bind what they sample. */
    cf_material_clear_textures(r->material);
    free_targets(r);
    r->canvases[0] = render_material_canvas(width, height);
    if (!r->canvases[0].id) {
        free_targets(r);
        return false;
    }
    r->tiles = ((width + 15) / 16) * ((height + 15) / 16);
    /* The shader reads offsets four at a time. */
    r->offsets = buffer((r->tiles + 4) / 4 * 4 * (int)sizeof(uint32_t));
    if (!r->offsets.id) {
        free_targets(r);
        return false;
    }
    r->width = width;
    r->height = height;
    return true;
}

static bool reserve_entities(render_material_raster *r, int count)
{
    if (count <= r->capacity) {
        return true;
    }
    int capacity = r->capacity ? r->capacity : 256;
    while (capacity < count) {
        if (capacity > INT_MAX / 2) {
            return false;
        }
        capacity *= 2;
    }
    count = capacity;
    if (count > INT_MAX / (int)sizeof(render_material_entity) ||
        count > INT_MAX / PARAMETER_BYTES) {
        return false;
    }
    CF_StorageBuffer entities = buffer(count * GPU_ENTITY_BYTES);
    if (!entities.id) {
        return false;
    }
    if (r->entities.id) {
        cf_destroy_storage_buffer(r->entities);
    }
    r->entity_data = xrealloc(r->entity_data, (size_t)count * GPU_ENTITY_BYTES);
    r->entities = entities;
    r->capacity = count;
    return true;
}

static bool reserve_parameters(render_material_raster *r, int blocks)
{
    if (r->parameters.id && blocks <= r->parameter_capacity) {
        return true;
    }
    int capacity = r->parameter_capacity ? r->parameter_capacity : 1;
    while (capacity < blocks) {
        if (capacity > INT_MAX / 2048) {
            return false;
        }
        capacity *= 2;
    }
    int bytes = blocks ? capacity * 1024 : 16;
    CF_StorageBuffer parameters = buffer(bytes);
    if (!parameters.id) {
        return false;
    }
    if (r->parameters.id) {
        cf_destroy_storage_buffer(r->parameters);
    }
    r->parameters = parameters;
    r->parameter_data = xrealloc(r->parameter_data, (size_t)bytes);
    r->parameter_capacity = blocks ? capacity : 0;
    r->parameter_buffer_bytes = bytes;
    return true;
}

render_material_upload_work render_material_raster_upload_work(const render_material_raster *r)
{
    render_material_upload_work work = r->upload_work;
    work.parameter_buffer_bytes = r->parameter_buffer_bytes;
    return work;
}

static bool reserve_indices(render_material_raster *r, int count)
{
    if (count < 1) {
        count = 1;
    }
    if (count <= r->index_capacity) {
        return true;
    }
    const int max_capacity = INT_MAX / (int)sizeof(uint32_t);
    if (count > max_capacity) {
        return false;
    }
    int capacity = r->index_capacity ? r->index_capacity : 256;
    while (capacity < count) {
        if (capacity > max_capacity / 2) {
            capacity = count;
            break;
        }
        capacity *= 2;
    }
    CF_StorageBuffer indices = buffer(capacity * (int)sizeof(uint32_t));
    if (!indices.id) {
        return false;
    }
    if (r->indices.id) {
        cf_destroy_storage_buffer(r->indices);
    }
    r->indices = indices;
    r->index_capacity = capacity;
    return true;
}

typedef struct
{
    int x0, y0, x1, y1;
} tile_rect;

static bool tile_axis_range(float low, float high, int tiles, int *first, int *last)
{
    float extent = (float)(tiles * 16);
    if (high < 0 || low > extent) {
        return false;
    }
    if (isnan(low) || isnan(high)) {
        *first = 0;
        *last = tiles - 1;
        return true;
    }
    *first = low <= 0 ? 0 : (int)ceilf(low / 16.0f) - 1;
    *last = high >= extent ? tiles - 1 : (int)floorf(high / 16.0f);
    *first = *first < 0 ? 0 : *first;
    *last = *last >= tiles ? tiles - 1 : *last;
    return *first <= *last;
}

static bool entity_tile_rect(
    const render_material_entity *entity,
    render_material_view view,
    float px,
    float py,
    float pad,
    int tiles_x,
    int tiles_y,
    tile_rect *rect
)
{
    if (render_mesh_camera_orbit(view)) {
        CF_V3 basis[3];
        render_mesh_camera_basis(view, basis);
        if (view.focal_distance <= 0 && fabsf(basis[1].y) < 1e-5f) {
            return false;
        }
        float ex = entity->extent[0] + pad, ey = entity->extent[1] + pad;
        CF_V2 lo, hi;
        if (!render_mesh_project_bounds(
                view,
                (CF_V3){ entity->pos[0] - ex, 0, -entity->pos[1] - ey },
                (CF_V3){ entity->pos[0] + ex, 0, -entity->pos[1] + ey },
                &lo,
                &hi
            )) {
            return false;
        }
        /* Filtering is in authored units; retain its projected pixel fringe. */
        float fringe = pad > 0 ? 3 : 0;
        return tile_axis_range(
                   (lo.x - view.min_x) * px - fringe,
                   (hi.x - view.min_x) * px + fringe,
                   tiles_x,
                   &rect->x0,
                   &rect->x1
               ) &&
               tile_axis_range(
                   (view.max_y + lo.y) * py - fringe,
                   (view.max_y + hi.y) * py + fringe,
                   tiles_y,
                   &rect->y0,
                   &rect->y1
               );
    }
    float center_x = (entity->pos[0] - view.min_x) * px;
    float center_y = (view.max_y - entity->pos[1]) * py;
    float extent_x = (entity->extent[0] + pad) * px;
    float extent_y = (entity->extent[1] + pad) * py;
    return tile_axis_range(center_x - extent_x, center_x + extent_x, tiles_x, &rect->x0, &rect->x1) &&
           tile_axis_range(center_y - extent_y, center_y + extent_y, tiles_y, &rect->y0, &rect->y1);
}

static float circle_filter_half_width(float aa, float px, float py)
{
    float max_pixel = fmaxf(px, py);
    return fmaxf(1.5f * max_pixel, fmaxf(0, aa) * 0.5f);
}

static bool circle_emission_filterable(const render_material_entity *entity)
{
    return entity->shape_type == RENDER_SHAPE_CIRCLE && entity->thickness <= 0 &&
           entity->border_thickness <= 0 && entity->reserved[0] <= 0 &&
           entity->reserved[1] <= 0 && entity->metaball_group == 0 &&
           (entity->emission[0] != 0 || entity->emission[1] != 0 ||
            entity->emission[2] != 0);
}

static bool bin_entities(
    render_material_raster *r,
    render_material_view view,
    int tiles_x,
    int tiles_y,
    float px,
    float py
)
{
    asetlen(r->tile_offsets, r->tiles + 1);
    memset(r->tile_offsets, 0, (size_t)(r->tiles + 1) * sizeof *r->tile_offsets);

    for (int i = 0; i < acount(r->sorted); i++) {
        tile_rect rect;
        const render_material_entity *entity = &r->sorted[i];
        if (entity->shape_type == RENDER_SHAPE_MESH) {
            continue;
        }
        float pad = circle_emission_filterable(entity)
                      ? circle_filter_half_width(entity->aa, view.width / r->width, view.height / r->height)
                      : 0;
        if (!entity_tile_rect(entity, view, px, py, pad, tiles_x, tiles_y, &rect)) {
            continue;
        }
        for (int y = rect.y0; y <= rect.y1; y++) {
            for (int x = rect.x0; x <= rect.x1; x++) {
                r->tile_offsets[y * tiles_x + x + 1]++;
            }
        }
    }

    uint64_t total = 0;
    for (int tile = 0; tile < r->tiles; tile++) {
        total += r->tile_offsets[tile + 1];
        if (total > (uint64_t)(INT_MAX / (int)sizeof(uint32_t))) {
            snprintf(r->error, sizeof r->error, "Material tile list exceeds storage-buffer limit");
            return false;
        }
        r->tile_offsets[tile + 1] = (uint32_t)total;
    }

    asetlen(r->tile_cursor, r->tiles);
    memcpy(r->tile_cursor, r->tile_offsets, (size_t)r->tiles * sizeof *r->tile_cursor);
    asetlen(r->tile_indices, (int)total);
    for (int i = 0; i < acount(r->sorted); i++) {
        tile_rect rect;
        const render_material_entity *entity = &r->sorted[i];
        if (entity->shape_type == RENDER_SHAPE_MESH) {
            continue;
        }
        float pad = circle_emission_filterable(entity)
                      ? circle_filter_half_width(entity->aa, view.width / r->width, view.height / r->height)
                      : 0;
        if (!entity_tile_rect(entity, view, px, py, pad, tiles_x, tiles_y, &rect)) {
            continue;
        }
        for (int y = rect.y0; y <= rect.y1; y++) {
            for (int x = rect.x0; x <= rect.x1; x++) {
                int tile = y * tiles_x + x;
                r->tile_indices[r->tile_cursor[tile]++] = (uint32_t)r->gpu_indices[i];
            }
        }
    }
    return true;
}

static float circle_filter_scale(float radius, float half_width)
{
    float r2 = radius * radius;
    if (radius <= 0) {
        return 0;
    }
    if (half_width <= 0) {
        return 1;
    }
    if (radius >= half_width) {
        return r2 / (r2 + 0.2f * half_width * half_width);
    }

    /* Integrate the symmetric smoothstep over a disk whose radius is below the
     * filter half-width; clipping the inner ramp changes the support polynomial. */
    float q = radius / half_width;
    float q2 = q * q;
    float q3 = q2 * q;
    float q5 = q3 * q2;
    float support = 2.0f * half_width * half_width *
                    (3.0f * q / 16.0f + 1.0f / 20.0f + q2 / 4.0f + q3 / 8.0f - q5 / 80.0f);
    return r2 / support;
}

static float entity_emission_scale(
    const render_material_entity *entity,
    render_material_view view,
    int width,
    int height
)
{
    if (render_mesh_camera_orbit(view) || !circle_emission_filterable(entity) ||
        !isfinite(entity->radius) || entity->radius <= 0) {
        return 1;
    }
    float half_width = circle_filter_half_width(entity->aa, view.width / width, view.height / height);
    return circle_filter_scale(entity->radius, half_width);
}

static int compare(const void *a, const void *b)
{
    const draw_order *x = a, *y = b;
    if (x->layer != y->layer) {
        return x->layer < y->layer ? -1 : 1;
    }
    return (x->index > y->index) - (x->index < y->index);
}

static void uniform(render_material_raster *r, const char *name, void *data, CF_UniformType type, int n)
{
    cf_material_set_uniform_fs(r->material, name, data, type, n);
}

CF_Canvas render_material_canvas(int width, int height)
{
    CF_CanvasParams params = cf_canvas_defaults(width, height);
    params.target_count = 6;
    for (int i = 0; i < 6; i++) {
        params.targets[i].pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
        params.targets[i].filter = CF_FILTER_LINEAR;
        params.targets[i].wrap_u = params.targets[i].wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    }
    /* Gather reads emission's mipmaps when it reconstructs below the canvas. */
    params.targets[1].allocate_mipmaps = true;
    return cf_make_canvas(params);
}

render_material_textures render_material_canvas_textures(CF_Canvas canvas)
{
    return (render_material_textures){
        cf_canvas_get_target2(canvas, 0), cf_canvas_get_target2(canvas, 1),
        cf_canvas_get_target2(canvas, 2), cf_canvas_get_target2(canvas, 3),
        cf_canvas_get_target2(canvas, 4), cf_canvas_get_target2(canvas, 5)
    };
}

const char *render_material_raster_error(const render_material_raster *r)
{
    return r->error[0] ? r->error : render_programs_error(r->programs);
}

/* A sprite's map variant, zero when it has none. */
static bool add_sprite_map(render_shader_set *set, float variant, shader_error *error)
{
    return variant <= 0 || render_shader_set_add_image(set, (int)variant, error);
}

static bool add_images(render_shader_set *set, const render_material_entity *entity, shader_error *error)
{
    return render_shader_set_add(set, (int)entity->reserved[0], error) &&
           render_shader_set_add(set, (int)entity->reserved[1], error) &&
           (entity->shape_type != RENDER_SHAPE_SPRITE ||
            (render_shader_set_add_image(set, (int)entity->points[12], error) &&
             add_sprite_map(set, entity->points[13], error) &&
             add_sprite_map(set, entity->points[14], error)));
}

/* Whether the next batch can sample `set`. Later batches also sample the
 * previous batch's targets. */
static bool fits(const render_material_raster *r, const render_shader_set *set, shader_error *error)
{
    render_program_kind kind = acount(r->batches) ? RENDER_PROGRAM_RASTER_ACCUMULATE
                                                  : RENDER_PROGRAM_RASTER;
    int samplers = render_program_samplers(kind, set) +
                   (cf_query_backend() == CF_BACKEND_TYPE_GLES3 ? GLES_STORAGE_TEXTURES
                                                                : 0);
    if (samplers <= FRAGMENT_SAMPLERS) {
        return true;
    }
    snprintf(
        error->message,
        sizeof error->message,
        "Needs %d fragment samplers, over the %d a shader may bind",
        samplers,
        FRAGMENT_SAMPLERS
    );
    return false;
}

static bool prepare_batches(render_material_raster *r)
{
    aclear(r->batches);
    r->error[0] = 0;
    material_batch ordinary = { 0 };
    shader_error error = { 0 };
    for (int i = 0; i < acount(r->sorted); i++) {
        const render_material_entity *entity = &r->sorted[i];
        if (entity->shape_type == RENDER_SHAPE_MESH) {
            if (ordinary.last > ordinary.first || (!acount(r->batches) && i > 0)) {
                apush(r->batches, ordinary);
            }
            int first = i;
            while (i + 1 < acount(r->sorted) &&
                   r->sorted[i + 1].shape_type == RENDER_SHAPE_MESH &&
                   r->sorted[i + 1].layer == entity->layer) {
                i++;
            }
            apush(r->batches, ((material_batch){ .first = first, .last = i + 1, .mesh = true }));
            ordinary = (material_batch){ .first = i + 1 };
            continue;
        }
        if (entity->metaball_group) {
            continue;
        }
        render_shader_set set = ordinary.set;
        if (!add_images(&set, entity, &error) ||
            (set.count > ordinary.set.count && !fits(r, &set, &error))) {
            if (ordinary.last > ordinary.first) {
                apush(r->batches, ordinary);
            }
            ordinary = (material_batch){ .first = i };
            set = (render_shader_set){ 0 };
            if (!add_images(&set, entity, &error) || !fits(r, &set, &error)) {
                snprintf(r->error, sizeof r->error, "Entity %d: %s", i, error.message);
                return false;
            }
        }
        ordinary.set = set;
        ordinary.last = i + 1;
    }
    if (ordinary.last > ordinary.first) {
        apush(r->batches, ordinary);
    }
    /* Each smooth-union group is evaluated in full, then composited in group order. */
    for (int group = 1; group <= 4; group++) {
        material_batch batch = { .last = acount(r->sorted), .group = group };
        bool found = false, valid = true;
        for (int i = 0; i < acount(r->sorted); i++) {
            if (r->sorted[i].metaball_group != (unsigned)group) {
                continue;
            }
            found = true;
            valid = valid && add_images(&batch.set, &r->sorted[i], &error);
        }
        if (found && !(valid && fits(r, &batch.set, &error))) {
            snprintf(
                r->error,
                sizeof r->error,
                "Metaball group %d: %s",
                group,
                error.message
            );
            return false;
        }
        if (found) {
            apush(r->batches, batch);
        }
    }
    if (!acount(r->batches)) {
        apush(r->batches, ((material_batch){ 0 }));
    }
    for (int i = 0; i < acount(r->batches); i++) {
        material_batch *batch = &r->batches[i];
        if (batch->mesh && !r->mesh_composite) {
            r->mesh_composite = render_programs_new(RENDER_PROGRAM_RASTER_MESH);
        }
        render_programs *programs = batch->mesh ? r->mesh_composite
                                  : i           ? r->accumulate
                                                : r->programs;
        batch->program = render_programs_prepare(programs, &batch->set);
        if (render_programs_error(programs)[0]) {
            snprintf(r->error, sizeof r->error, "%s", render_programs_error(programs));
        }
        if (!batch->program) {
            return false;
        }
    }
    return true;
}

static int prepare_spatial_groups(
    render_material_raster *r,
    ecs_world *world,
    const material_batch *batch,
    const render_mesh_material_actor *meshes,
    render_material_view view
)
{
    int count = batch->last - batch->first;
    aclear(r->batch_meshes);
    for (int i = batch->first; i < batch->last; i++) {
        apush(r->batch_meshes, meshes[(int)r->sorted[i].points[0]]);
    }
    asetlen(r->spatial_actors, count);
    asetlen(r->spatial_membership, count);
    for (int i = 0; i < acount(r->spatial_surfaces); i++) {
        render_mesh_surface_free(&r->spatial_surfaces[i]);
    }
    asetlen(r->spatial_surfaces, count);
    memset(r->spatial_surfaces, 0, (size_t)count * sizeof *r->spatial_surfaces);
    render_pixel_rect full = { 0, 0, r->width, r->height };
    for (int i = 0; i < count; i++) {
        render_mesh_material_actor *actor = &r->batch_meshes[i];
        render_pixel_rect rect = full;
        if (!r->disable_spatial_scratch) {
            if (!actor->surface) {
                if (!render_mesh_surface_build(
                        world,
                        actor->entity,
                        &actor->position,
                        &r->spatial_surfaces[i],
                        r->error,
                        sizeof r->error
                    )) {
                    return 0;
                }
                actor->surface = &r->spatial_surfaces[i];
                r->mesh_work.surface_builds++;
            }
            CF_V2 lo, hi;
            if (render_mesh_project_bounds(
                    view,
                    actor->surface->min,
                    actor->surface->max,
                    &lo,
                    &hi
                )) {
                rect = render_mesh_pixel_bounds(view, lo, hi, r->width, r->height);
            }
        }
        r->spatial_actors[i] = (render_spatial_actor){ rect, i, 0 };
    }
    int groups = render_spatial_partition(r->spatial_actors, count, 0);
    asetlen(r->spatial_rects, groups);
    for (int i = 0; i < groups; i++) {
        r->spatial_rects[i] = (render_pixel_rect){ r->width, r->height, 0, 0 };
    }
    for (int i = 0; i < count; i++) {
        const render_spatial_actor *actor = &r->spatial_actors[i];
        r->spatial_membership[actor->actor] = actor->group;
        render_pixel_rect *rect = &r->spatial_rects[actor->group];
        if (actor->rect.x < rect->x) {
            rect->x = actor->rect.x;
        }
        if (actor->rect.y < rect->y) {
            rect->y = actor->rect.y;
        }
        if (actor->rect.right > rect->right) {
            rect->right = actor->rect.right;
        }
        if (actor->rect.bottom > rect->bottom) {
            rect->bottom = actor->rect.bottom;
        }
    }
    asetlen(r->spatial_offsets, groups + 1);
    memset(r->spatial_offsets, 0, (size_t)(groups + 1) * sizeof *r->spatial_offsets);
    for (int i = 0; i < count; i++) {
        r->spatial_offsets[r->spatial_membership[i] + 1]++;
    }
    for (int i = 0; i < groups; i++) {
        r->spatial_offsets[i + 1] += r->spatial_offsets[i];
    }
    asetlen(r->spatial_cursor, groups);
    memcpy(r->spatial_cursor, r->spatial_offsets, (size_t)groups * sizeof *r->spatial_cursor);
    asetlen(r->spatial_order, count);
    for (int i = 0; i < count; i++) {
        r->spatial_order[r->spatial_cursor[r->spatial_membership[i]]++] = i;
    }
    return groups;
}

static void compose_batch(
    render_material_raster *r,
    const material_batch *batch,
    int index,
    CF_StorageBuffer inputs[4],
    render_pixel_rect clip,
    bool clear
)
{
    const char *previous[] = { "u_prev_scene",
                               "u_prev_source",
                               "u_prev_albedo",
                               "u_prev_normal",
                               "u_prev_cover" };
    const int targets[] = { 0, 1, 2, 3, 5 };
    for (int t = 0; (index || batch->mesh) && t < 5; t++) {
        cf_material_set_texture_fs(
            r->material,
            previous[t],
            cf_canvas_get_target2(r->canvases[(index - 1) & 1], targets[t])
        );
    }
    CF_Shader shader = render_programs_bind_raster(
        batch->mesh ? r->mesh_composite
        : index     ? r->accumulate
                    : r->programs,
        batch->program,
        r->material
    );
    cf_apply_canvas(r->canvases[index & 1], clear);
    cf_apply_mesh(r->mesh);
    cf_apply_shader(shader, r->material);
    cf_apply_fs_storage_buffers(inputs, 4);
    cf_apply_viewport(0, 0, r->width, r->height);
    cf_apply_scissor(clip.x, clip.y, clip.right - clip.x, clip.bottom - clip.y);
    cf_draw_elements();
    if (batch->mesh) {
        r->mesh_work.composition_pixels += (size_t)(clip.right - clip.x) *
                                           (clip.bottom - clip.y);
        render_gpu_timing_mark("Mesh composition");
    }
}

static bool draw_mesh_group(render_material_raster *r, ecs_world *world, render_material_view view, float time)
{
    if (!r->mesh_pass ||
        !render_mesh_material_resize(r->mesh_pass, r->width, r->height) ||
        !render_mesh_material_draw(r->mesh_pass, world, r->group_meshes, acount(r->group_meshes), view, time)) {
        snprintf(
            r->error,
            sizeof r->error,
            "Mesh material: %s",
            r->mesh_pass ? render_mesh_material_error(r->mesh_pass) : "resource allocation failed"
        );
        return false;
    }
    render_mesh_material_work work = render_mesh_material_last_work(r->mesh_pass);
    r->mesh_work.draw_calls += work.draw_calls;
    r->mesh_work.sdf_draw_calls += work.sdf_draw_calls;
    r->mesh_work.sdf_instances += work.sdf_instances;
    r->mesh_work.sdf_uploads += work.sdf_uploads;
    r->mesh_work.mesh_uploads += work.mesh_uploads;
    r->mesh_work.sdf_upload_bytes += work.sdf_upload_bytes;
    r->mesh_work.surface_builds += work.surface_builds;
    r->mesh_work.surface_reuses += work.surface_reuses;
    r->mesh_work.sdf_program_prepares += work.sdf_program_prepares;
    r->mesh_work.sdf_lod_instances += work.sdf_lod_instances;
    r->mesh_work.surface_pixels += work.surface_pixels;
    if (work.surface_capacity_pixels > r->mesh_work.surface_capacity_pixels) {
        r->mesh_work.surface_capacity_pixels = work.surface_capacity_pixels;
    }
    r->mesh_work.sdf_pair_probes += work.sdf_pair_probes;
    r->mesh_work.sdf_parameter_probes += work.sdf_parameter_probes;
    r->mesh_work.sdf_parameter_upload_bytes += work.sdf_parameter_upload_bytes;
    r->mesh_work.sdf_parameter_rows += work.sdf_parameter_rows;
    r->mesh_work.sdf_parameter_uploads += work.sdf_parameter_uploads;
    r->mesh_work.depth_copies += work.depth_copies;
    r->mesh_work.depth_copy_pixels += work.depth_copy_pixels;
    r->mesh_work.scratch_groups++;
    render_gpu_timing_mark("Mesh material");
    render_mesh_material_textures textures = render_mesh_material_get_textures(r->mesh_pass);
    cf_material_set_texture_fs(r->material, "u_mesh_scene", textures.scene);
    cf_material_set_texture_fs(r->material, "u_mesh_emission", textures.emission);
    cf_material_set_texture_fs(r->material, "u_mesh_absorption", textures.absorption);
    cf_material_set_texture_fs(r->material, "u_mesh_albedo", textures.albedo);
    cf_material_set_texture_fs(r->material, "u_mesh_normal", textures.normal);
    float rect[4];
    render_mesh_material_rect(r->mesh_pass, rect);
    uniform(r, "u_mesh_rect", rect, CF_UNIFORM_TYPE_FLOAT4, 1);
    int active[2];
    render_mesh_material_extent(r->mesh_pass, active, NULL);
    float extent[2] = { (float)active[0], (float)active[1] };
    uniform(r, "u_mesh_extent", extent, CF_UNIFORM_TYPE_FLOAT2, 1);
    return true;
}

bool render_material_raster_draw_mixed(
    render_material_raster *r,
    const render_material_entity *entities,
    int count,
    render_material_view view,
    ecs_world *world,
    const render_mesh_material_actor *meshes,
    int mesh_count,
    float time
)
{
    assert(r && count >= 0 && (entities || !count));
    r->mesh_work = (render_mesh_material_work){ 0 };
    r->upload_work = (render_material_upload_work){ 0 };
    if (!r->width || !isfinite(view.min_x) || !isfinite(view.max_y) ||
        !isfinite(view.width) || !isfinite(view.height) || view.width <= 0 ||
        view.height <= 0 || !isfinite(view.yaw) || !isfinite(view.pitch) ||
        !isfinite(view.roll) || !isfinite(view.target_elevation) ||
        !isfinite(view.focal_distance) ||
        (view.focal_distance > 0 && view.focal_distance <= RENDER_MESH_NEAR)) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (!isfinite(entities[i].layer) || entities[i].shape_type > RENDER_SHAPE_MESH ||
            entities[i].point_count > 8 || entities[i].metaball_group > 4) {
            return false;
        }
        if (entities[i].shape_type == RENDER_SHAPE_MESH &&
            (!world || !meshes || !isfinite(entities[i].points[0]) ||
             entities[i].points[0] < 0 || entities[i].points[0] >= mesh_count ||
             entities[i].points[0] != floorf(entities[i].points[0]))) {
            snprintf(r->error, sizeof r->error, "Invalid mesh material actor index");
            return false;
        }
    }
    int ordinary = 0, parameter_blocks = 0;
    for (int i = 0; i < count; i++) {
        if (entities[i].shape_type == RENDER_SHAPE_MESH) {
            continue;
        }
        ordinary++;
        parameter_blocks += entities[i].reserved[0] > 0 ||
                            entities[i].reserved[1] > 0;
    }
    if (!reserve_entities(r, ordinary ? ordinary : 1) ||
        !reserve_parameters(r, parameter_blocks)) {
        return false;
    }
    /* Stable by layer: sorting each entity's layer and index moves a few
       bytes, where sorting entities would move each one log n times. */
    asetlen(r->order, count);
    for (int i = 0; i < count; i++) {
        r->order[i] = (draw_order){ entities[i].layer, i };
    }
    if (count) {
        qsort(r->order, (size_t)count, sizeof *r->order, compare);
    }
    asetlen(r->sorted, count);
    for (int i = 0; i < count; i++) {
        r->sorted[i] = entities[r->order[i].index];
    }
    for (int i = 0; i < count; i++) {
        r->sorted[i].time[2] = entity_emission_scale(&r->sorted[i], view, r->width, r->height);
    }
    if (!prepare_batches(r)) {
        return false;
    }
    asetlen(r->gpu_indices, count + 1);
    int record = 0, block = 0;
    for (int i = 0; i < count; i++) {
        r->gpu_indices[i] = record;
        const render_material_entity *entity = &r->sorted[i];
        if (entity->shape_type == RENDER_SHAPE_MESH) {
            continue;
        }
        unsigned char *data = r->entity_data + (size_t)record++ * GPU_ENTITY_BYTES;
        memcpy(data, entity, GPU_ENTITY_BYTES);
        uint32_t parameter_base = 0;
        if (entity->reserved[0] > 0 || entity->reserved[1] > 0) {
            parameter_base = (uint32_t)block * 64;
            memcpy(r->parameter_data + (size_t)block++ * 256, entity->material_parameters, 1024);
        }
        // The uploaded time[1] bits address compact parameters; entity and
        // tile indices remain independent.
        memcpy(data + offsetof(render_material_entity, time) + sizeof(float), &parameter_base, sizeof parameter_base);
    }
    r->gpu_indices[count] = record;
    if (record) {
        cf_update_storage_buffer(r->entities, r->entity_data, record * GPU_ENTITY_BYTES);
    }
    if (block) {
        cf_update_storage_buffer(r->parameters, r->parameter_data, block * 1024);
    }
    r->upload_work = (render_material_upload_work){
        .entity_records = record,
        .parameter_blocks = block,
        .entity_bytes = (size_t)record * GPU_ENTITY_BYTES,
        .parameter_bytes = (size_t)block * 1024,
    };
    int tx = (r->width + 15) / 16, ty = (r->height + 15) / 16;
    float units[2] = { view.width / r->width, view.height / r->height };
    float pixels[2] = { r->width / view.width, r->height / view.height };
    if (!bin_entities(r, view, tx, ty, pixels[0], pixels[1]) ||
        !reserve_indices(r, acount(r->tile_indices))) {
        return false;
    }
    cf_update_storage_buffer(r->offsets, r->tile_offsets, (r->tiles + 1) * (int)sizeof(uint32_t));
    if (acount(r->tile_indices)) {
        cf_update_storage_buffer(
            r->indices,
            r->tile_indices,
            acount(r->tile_indices) * (int)sizeof(uint32_t)
        );
    }
    bool needs_previous = acount(r->batches) > 1 || r->batches[0].mesh;
    if (needs_previous && !r->canvases[1].id) {
        r->canvases[1] = render_material_canvas(r->width, r->height);
        if (!r->canvases[1].id) {
            snprintf(r->error, sizeof r->error, "Material raster could not allocate its second canvas");
            return false;
        }
    }
    uniform(r, "u_world_min_x", &view.min_x, CF_UNIFORM_TYPE_FLOAT, 1);
    uniform(r, "u_world_max_y", &view.max_y, CF_UNIFORM_TYPE_FLOAT, 1);
    uniform(r, "u_unit_per_px", units, CF_UNIFORM_TYPE_FLOAT2, 1);
    CF_V3 basis[3], target = render_mesh_camera_target(view);
    render_mesh_camera_basis(view, basis);
    float right[4] = { basis[0].x, basis[0].y, basis[0].z, view.focal_distance };
    float down[4] = { basis[2].x, basis[2].y, basis[2].z, 0 };
    float plane[4] = { target.x, target.y, target.z, render_mesh_camera_orbit(view) };
    uniform(r, "u_plane_right", right, CF_UNIFORM_TYPE_FLOAT4, 1);
    uniform(r, "u_plane_down", down, CF_UNIFORM_TYPE_FLOAT4, 1);
    uniform(r, "u_plane_target", plane, CF_UNIFORM_TYPE_FLOAT4, 1);
    uniform(r, "u_tiles_x", &tx, CF_UNIFORM_TYPE_INT, 1);
    CF_StorageBuffer inputs[] = { r->entities, r->offsets, r->indices, r->parameters };
    render_pixel_rect full = { 0, 0, r->width, r->height };
    cf_push_gpu_label("Material raster batches");
    for (int i = 0; i < acount(r->batches); i++) {
        const material_batch *batch = &r->batches[i];
        int first = r->gpu_indices[batch->first];
        int last = batch->mesh ? first : r->gpu_indices[batch->last];
        int has_previous = i > 0;
        uniform(r, "u_has_previous", &has_previous, CF_UNIFORM_TYPE_INT, 1);
        uniform(r, "u_first_entity", &first, CF_UNIFORM_TYPE_INT, 1);
        uniform(r, "u_last_entity", &last, CF_UNIFORM_TYPE_INT, 1);
        uniform(r, "u_metaball_group", (void *)&batch->group, CF_UNIFORM_TYPE_INT, 1);
        if (!batch->mesh) {
            compose_batch(r, batch, i, inputs, full, true);
            continue;
        }
        render_gpu_timing_mark("Material shapes");
        if (!r->mesh_pass) {
            r->mesh_pass = render_mesh_material_new();
            if (r->mesh_pass) {
                render_mesh_material_set_sdf_batch_limit(r->mesh_pass, r->sdf_batch_limit);
                render_mesh_material_set_quality(r->mesh_pass, r->quality);
                render_mesh_material_set_sdf_trace_debug(r->mesh_pass, r->sdf_trace_debug);
                render_mesh_material_set_hidden_rejection(r->mesh_pass, !r->disable_hidden_rejection);
                render_mesh_material_set_model_inspection(r->mesh_pass, &r->inspection);
            }
        }
        int groups = prepare_spatial_groups(r, world, batch, meshes, view);
        if (!groups || !r->mesh_pass) {
            if (!r->error[0]) {
                snprintf(r->error, sizeof r->error, "Mesh group allocation failed");
            }
            cf_pop_gpu_label();
            return false;
        }
        for (int group = 0; group < groups; group++) {
            aclear(r->group_meshes);
            // Preserve original order, including equal-depth ties, inside leaves.
            for (int at = r->spatial_offsets[group];
                 at < r->spatial_offsets[group + 1];
                 at++) {
                apush(r->group_meshes, r->batch_meshes[r->spatial_order[at]]);
            }
            if (!draw_mesh_group(r, world, view, time)) {
                cf_pop_gpu_label();
                return false;
            }
            if (!group && has_previous && groups > 1) {
                // Copy the immutable prior batch once. Each disjoint leaf then
                // writes only its own pixels, without a canvas feedback loop.
                float empty[] = { -2, -2, 1, 1 };
                uniform(r, "u_mesh_rect", empty, CF_UNIFORM_TYPE_FLOAT4, 1);
                compose_batch(r, batch, i, inputs, full, true);
                float rect[4];
                render_mesh_material_rect(r->mesh_pass, rect);
                uniform(r, "u_mesh_rect", rect, CF_UNIFORM_TYPE_FLOAT4, 1);
            }
            bool clear = !group && (!has_previous || groups == 1);
            render_pixel_rect clip = groups == 1 && has_previous
                                       ? full
                                       : r->spatial_rects[group];
            compose_batch(r, batch, i, inputs, clip, clear);
        }
    }
    cf_pop_gpu_label();
    r->result = (acount(r->batches) - 1) & 1;
    return true;
}

bool render_material_raster_draw(
    render_material_raster *r,
    const render_material_entity *entities,
    int count,
    render_material_view view
)
{
    return render_material_raster_draw_mixed(r, entities, count, view, NULL, NULL, 0, 0);
}

render_material_textures render_material_raster_textures(const render_material_raster *r)
{
    assert(r);
    return render_material_canvas_textures(r->canvases[r->result]);
}

CF_Canvas render_material_raster_canvas(const render_material_raster *r)
{
    assert(r);
    return r->canvases[r->result];
}
