#include "render/material_raster.h"

#include "render/fullscreen.h"
#include "render/shader_catalog.h"
#include "render/shader_program.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The GPU entity is the prefix before the separately uploaded parameters. */
enum
{
    GPU_ENTITY_BYTES = offsetof(render_material_entity, material_parameters)
};
_Static_assert(sizeof(render_material_entity) == 1376, "entity std430 stride");
_Static_assert(offsetof(render_material_entity, points) == 80, "entity point offset");
_Static_assert(offsetof(render_material_entity, scene) == 144, "entity material offset");
_Static_assert(offsetof(render_material_entity, time) == 304, "entity clock offset");
_Static_assert(GPU_ENTITY_BYTES == 352, "entity GPU size");

typedef struct
{
    int first, last, group, program;
    render_shader_set set;
} material_batch;

struct render_material_raster
{
    /* The first batch's programs, and later batches', which continue from
     * the previous batch's targets. */
    render_programs *programs, *accumulate;
    material_batch *batches;
    char error[256];
    CF_Material material;
    CF_Mesh mesh;
    CF_StorageBuffer entities, offsets, indices, parameters;
    unsigned char *entity_data;
    float *parameter_data;
    uint32_t *tile_offsets, *tile_cursor, *tile_indices;
    /* Batches alternate between these; see render_material_textures. The
     * second is made when a frame first has several batches. */
    CF_Canvas canvases[2];
    int result;
    render_material_entity *sorted;
    int width, height, tiles, capacity, index_capacity;
};

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
    if (r->mesh.id) {
        cf_destroy_mesh(r->mesh);
    }
    afree(r->batches);
    if (r->material.id) {
        cf_destroy_material(r->material);
    }
    afree(r->sorted);
    afree(r->tile_offsets);
    afree(r->tile_cursor);
    afree(r->tile_indices);
    free(r->entity_data);
    free(r->parameter_data);
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
    if (count > INT_MAX / (int)sizeof(render_material_entity) || count > INT_MAX / 1024) {
        return false;
    }
    CF_StorageBuffer entities = buffer(count * GPU_ENTITY_BYTES);
    CF_StorageBuffer parameters = buffer(count * 1024);
    if (!entities.id || !parameters.id) {
        if (parameters.id) {
            cf_destroy_storage_buffer(parameters);
        }
        if (entities.id) {
            cf_destroy_storage_buffer(entities);
        }
        return false;
    }
    if (r->entities.id) {
        cf_destroy_storage_buffer(r->entities);
    }
    if (r->parameters.id) {
        cf_destroy_storage_buffer(r->parameters);
    }
    r->parameters = parameters;
    r->entity_data = xrealloc(r->entity_data, (size_t)count * GPU_ENTITY_BYTES);
    r->parameter_data = xrealloc(r->parameter_data, (size_t)count * 1024);
    r->entities = entities;
    r->capacity = count;
    return true;
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
           entity->wobble[0] == 0 && entity->pluck[0] == 0 &&
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
        float pad = circle_emission_filterable(entity)
                      ? circle_filter_half_width(entity->aa, view.width / r->width, view.height / r->height)
                      : 0;
        if (!entity_tile_rect(entity, view, px, py, pad, tiles_x, tiles_y, &rect)) {
            continue;
        }
        for (int y = rect.y0; y <= rect.y1; y++) {
            for (int x = rect.x0; x <= rect.x1; x++) {
                int tile = y * tiles_x + x;
                r->tile_indices[r->tile_cursor[tile]++] = (uint32_t)i;
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
    if (!circle_emission_filterable(entity) || !isfinite(entity->radius) ||
        entity->radius <= 0) {
        return 1;
    }
    float half_width = circle_filter_half_width(entity->aa, view.width / width, view.height / height);
    return circle_filter_scale(entity->radius, half_width);
}

static int compare(const void *a, const void *b)
{
    const render_material_entity *x = a, *y = b;
    if (x->wobble[3] != y->wobble[3]) {
        return x->wobble[3] < y->wobble[3] ? -1 : 1;
    }
    return (x->time[1] > y->time[1]) - (x->time[1] < y->time[1]);
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

static bool add_images(render_shader_set *set, const render_material_entity *entity, shader_error *error)
{
    return render_shader_set_add(set, (int)entity->reserved[0], error) &&
           render_shader_set_add(set, (int)entity->reserved[1], error);
}

static bool prepare_batches(render_material_raster *r)
{
    aclear(r->batches);
    r->error[0] = 0;
    material_batch ordinary = { 0 };
    shader_error error = { 0 };
    for (int i = 0; i < acount(r->sorted); i++) {
        const render_material_entity *entity = &r->sorted[i];
        if (entity->metaball_group) {
            continue;
        }
        render_shader_set set = ordinary.set;
        if (!add_images(&set, entity, &error)) {
            if (ordinary.last > ordinary.first) {
                apush(r->batches, ordinary);
            }
            ordinary = (material_batch){ .first = i };
            set = (render_shader_set){ 0 };
            if (!add_images(&set, entity, &error)) {
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
        bool found = false;
        for (int i = 0; i < acount(r->sorted); i++) {
            if (r->sorted[i].metaball_group != (unsigned)group) {
                continue;
            }
            found = true;
            if (!add_images(&batch.set, &r->sorted[i], &error)) {
                snprintf(
                    r->error,
                    sizeof r->error,
                    "Metaball group %d: %s",
                    group,
                    error.message
                );
                return false;
            }
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
        render_programs *programs = i ? r->accumulate : r->programs;
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

bool render_material_raster_draw(
    render_material_raster *r,
    const render_material_entity *entities,
    int count,
    render_material_view view
)
{
    assert(r && count >= 0 && (entities || !count));
    if (!r->width || !isfinite(view.min_x) || !isfinite(view.max_y) ||
        !isfinite(view.width) || !isfinite(view.height) || view.width <= 0 ||
        view.height <= 0) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (!isfinite(entities[i].wobble[3]) ||
            entities[i].shape_type > RENDER_SHAPE_POLY_FILL ||
            entities[i].point_count > 8 || entities[i].metaball_group > 4) {
            return false;
        }
    }
    if (!reserve_entities(r, count ? count : 1)) {
        return false;
    }
    asetlen(r->sorted, count);
    for (int i = 0; i < count; i++) {
        r->sorted[i] = entities[i];
        r->sorted[i].time[1] = (float)i;
    }
    if (count) {
        qsort(r->sorted, (size_t)count, sizeof *r->sorted, compare);
    }
    for (int i = 0; i < count; i++) {
        r->sorted[i].time[2] = entity_emission_scale(&r->sorted[i], view, r->width, r->height);
    }
    if (!prepare_batches(r)) {
        return false;
    }
    if (count) {
        for (int i = 0; i < count; i++) {
            memcpy(r->entity_data + (size_t)i * GPU_ENTITY_BYTES, &r->sorted[i], GPU_ENTITY_BYTES);
            memcpy(r->parameter_data + (size_t)i * 256, r->sorted[i].material_parameters, 1024);
        }
        cf_update_storage_buffer(r->entities, r->entity_data, count * GPU_ENTITY_BYTES);
        cf_update_storage_buffer(r->parameters, r->parameter_data, count * 1024);
    }
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
    if (acount(r->batches) > 1 && !r->canvases[1].id) {
        r->canvases[1] = render_material_canvas(r->width, r->height);
        if (!r->canvases[1].id) {
            snprintf(r->error, sizeof r->error, "Material raster could not allocate its second canvas");
            return false;
        }
    }
    uniform(r, "u_world_min_x", &view.min_x, CF_UNIFORM_TYPE_FLOAT, 1);
    uniform(r, "u_world_max_y", &view.max_y, CF_UNIFORM_TYPE_FLOAT, 1);
    uniform(r, "u_unit_per_px", units, CF_UNIFORM_TYPE_FLOAT2, 1);
    uniform(r, "u_tiles_x", &tx, CF_UNIFORM_TYPE_INT, 1);
    CF_StorageBuffer inputs[] = { r->entities, r->offsets, r->indices, r->parameters };
    /* Every target but visibility, which the raster leaves zero. */
    const char *previous[] = { "u_prev_scene",
                               "u_prev_source",
                               "u_prev_albedo",
                               "u_prev_normal",
                               "u_prev_cover" };
    const int targets[] = { 0, 1, 2, 3, 5 };
    cf_push_gpu_label("Material raster batches");
    for (int i = 0; i < acount(r->batches); i++) {
        const material_batch *batch = &r->batches[i];
        uniform(r, "u_first_entity", (void *)&batch->first, CF_UNIFORM_TYPE_INT, 1);
        uniform(r, "u_last_entity", (void *)&batch->last, CF_UNIFORM_TYPE_INT, 1);
        uniform(r, "u_metaball_group", (void *)&batch->group, CF_UNIFORM_TYPE_INT, 1);
        for (int t = 0; i && t < 5; t++) {
            cf_material_set_texture_fs(
                r->material,
                previous[t],
                cf_canvas_get_target2(r->canvases[(i - 1) & 1], targets[t])
            );
        }
        CF_Shader shader = render_programs_bind_raster(
            i ? r->accumulate : r->programs,
            batch->program,
            r->material
        );
        cf_apply_canvas(r->canvases[i & 1], true);
        cf_apply_mesh(r->mesh);
        cf_apply_shader(shader, r->material);
        cf_apply_fs_storage_buffers(inputs, 4);
        cf_apply_viewport(0, 0, r->width, r->height);
        cf_apply_scissor(0, 0, r->width, r->height);
        cf_draw_elements();
    }
    cf_pop_gpu_label();
    r->result = (acount(r->batches) - 1) & 1;
    return true;
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
