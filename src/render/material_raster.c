#include "render/material_raster.h"

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
_Static_assert(sizeof(render_material_entity) == 1360, "entity std430 stride");
_Static_assert(offsetof(render_material_entity, points) == 80, "entity point offset");
_Static_assert(offsetof(render_material_entity, scene) == 144, "entity material offset");
_Static_assert(offsetof(render_material_entity, time) == 304, "entity clock offset");
_Static_assert(GPU_ENTITY_BYTES == 336, "entity GPU size");

typedef struct
{
    int first, last, group, program;
    render_shader_set set;
} material_batch;

struct render_material_raster
{
    render_programs *programs;
    material_batch *batches;
    char error[256];
    CF_Material material;
    CF_StorageBuffer entities, offsets, indices, parameters;
    unsigned char *entity_data;
    float *parameter_data;
    uint32_t *tile_offsets, *tile_cursor, *tile_indices;
    /* scene, emission, absorption, albedo, normal, relief */
    CF_Texture textures[6];
    render_material_entity *sorted;
    int width, height, tiles, capacity, index_capacity;
};

static void free_targets(render_material_raster *r)
{
    for (int i = 0; i < 6; i++) {
        if (r->textures[i].id) {
            cf_destroy_texture(r->textures[i]);
        }
        r->textures[i] = (CF_Texture){ 0 };
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
    r->programs = render_programs_new(RENDER_PROGRAM_COMPUTE);
    r->material = cf_make_material();
    if (!r->material.id) {
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
    for (int i = 0; i < 6; i++) {
        CF_TextureParams params = cf_texture_defaults(width, height);
        params.pixel_format = i == 5 ? CF_PIXEL_FORMAT_R16G16_FLOAT
                                     : CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
        params.usage = CF_TEXTURE_USAGE_SAMPLER_BIT |
                       CF_TEXTURE_USAGE_COLOR_TARGET_BIT |
                       CF_TEXTURE_USAGE_COMPUTE_STORAGE_READ_BIT |
                       CF_TEXTURE_USAGE_COMPUTE_STORAGE_WRITE_BIT;
        params.filter = CF_FILTER_LINEAR;
        params.wrap_u = params.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
        params.allocate_mipmaps = i == 1 || i == 2;
        r->textures[i] = cf_make_texture(params);
        if (!r->textures[i].id) {
            free_targets(r);
            return false;
        }
    }
    r->tiles = ((width + 15) / 16) * ((height + 15) / 16);
    r->offsets = buffer((r->tiles + 1) * (int)sizeof(uint32_t));
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
    cf_material_set_uniform_cs(r->material, name, data, type, n);
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
        batch->program = render_programs_prepare(r->programs, &batch->set);
        if (render_programs_error(r->programs)[0]) {
            snprintf(r->error, sizeof r->error, "%s", render_programs_error(r->programs));
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
    uniform(r, "u_world_min_x", &view.min_x, CF_UNIFORM_TYPE_FLOAT, 1);
    uniform(r, "u_world_max_y", &view.max_y, CF_UNIFORM_TYPE_FLOAT, 1);
    uniform(r, "u_px_per_unit", pixels, CF_UNIFORM_TYPE_FLOAT, 2);
    uniform(r, "u_unit_per_px", units, CF_UNIFORM_TYPE_FLOAT, 2);
    uniform(r, "u_tiles_x", &tx, CF_UNIFORM_TYPE_INT, 1);
    uniform(r, "u_tiles_y", &ty, CF_UNIFORM_TYPE_INT, 1);
    uniform(r, "u_canvas_w", &r->width, CF_UNIFORM_TYPE_INT, 1);
    uniform(r, "u_canvas_h", &r->height, CF_UNIFORM_TYPE_INT, 1);
    CF_StorageBuffer inputs[] = { r->entities, r->offsets, r->indices, r->parameters };
    CF_ComputeDispatch dispatch = cf_compute_dispatch_defaults(tx, ty, 1);
    dispatch.ro_buffers = inputs;
    dispatch.ro_buffer_count = 4;
    dispatch.rw_textures = r->textures;
    dispatch.rw_texture_count = 6;
    cf_push_gpu_label("Material raster batches");
    for (int i = 0; i < acount(r->batches); i++) {
        const material_batch *batch = &r->batches[i];
        int accumulate = i != 0;
        uniform(r, "u_first_entity", (void *)&batch->first, CF_UNIFORM_TYPE_INT, 1);
        uniform(r, "u_last_entity", (void *)&batch->last, CF_UNIFORM_TYPE_INT, 1);
        uniform(r, "u_metaball_group", (void *)&batch->group, CF_UNIFORM_TYPE_INT, 1);
        uniform(r, "u_accumulate", &accumulate, CF_UNIFORM_TYPE_INT, 1);
        CF_ComputeShader
            shader = render_programs_bind_compute(r->programs, batch->program, r->material);
        cf_dispatch_compute(shader, r->material, dispatch);
    }
    cf_pop_gpu_label();
    return true;
}

render_material_textures render_material_raster_textures(const render_material_raster *r)
{
    assert(r);
    return (render_material_textures){ r->textures[0], r->textures[1],
                                       r->textures[2], r->textures[3],
                                       r->textures[4], r->textures[5] };
}
