#include "material_raster.h"

#include "../core/array.h"
#include "../core/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

_Static_assert(sizeof(render_material_entity) == 320, "entity std430 stride");
_Static_assert(offsetof(render_material_entity, points) == 80, "entity point offset");
_Static_assert(offsetof(render_material_entity, scene) == 144, "entity material offset");
_Static_assert(offsetof(render_material_entity, time) == 304, "entity clock offset");

struct render_material_raster
{
    CF_ComputeShader bin, raster;
    CF_Material material;
    CF_StorageBuffer entities, counts, indices;
    CF_Texture textures[4];
    render_material_entity *sorted;
    int width, height, tiles, capacity;
};

static void free_targets(render_material_raster *r)
{
    for (int i = 0; i < 4; i++) {
        if (r->textures[i].id) {
            cf_destroy_texture(r->textures[i]);
        }
        r->textures[i] = (CF_Texture){ 0 };
    }
    if (r->counts.id) {
        cf_destroy_storage_buffer(r->counts);
    }
    if (r->indices.id) {
        cf_destroy_storage_buffer(r->indices);
    }
    if (r->entities.id) {
        cf_destroy_storage_buffer(r->entities);
    }
    r->counts = r->indices = r->entities = (CF_StorageBuffer){ 0 };
    r->width = r->height = r->tiles = r->capacity = 0;
}

render_material_raster *render_material_raster_new()
{
    render_material_raster *r = xcalloc(1, sizeof *r);
    r->bin = cf_make_compute_shader("hrc/entity_bin.shd");
    r->raster = cf_make_compute_shader("hrc/entity_raster.shd");
    r->material = cf_make_material();
    if (!r->bin.id || !r->raster.id || !r->material.id) {
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
    if (r->bin.id) {
        cf_destroy_compute_shader(r->bin);
    }
    if (r->raster.id) {
        cf_destroy_compute_shader(r->raster);
    }
    if (r->material.id) {
        cf_destroy_material(r->material);
    }
    afree(r->sorted);
    free(r);
}

static CF_StorageBuffer buffer(int bytes)
{
    CF_StorageBufferParams params = cf_storage_buffer_defaults(bytes);
    params.compute_writable = true;
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
    for (int i = 0; i < 4; i++) {
        CF_TextureParams params = cf_texture_defaults(width, height);
        params.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
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
    r->counts = buffer(r->tiles * (int)sizeof(uint32_t));
    if (!r->counts.id) {
        free_targets(r);
        return false;
    }
    r->width = width;
    r->height = height;
    return true;
}

static bool reserve(render_material_raster *r, int count)
{
    if (count <= r->capacity) {
        return true;
    }
    size_t index_bytes = (size_t)r->tiles * count * sizeof(uint32_t);
    if (index_bytes > INT_MAX || count > INT_MAX / (int)sizeof(render_material_entity)) {
        return false;
    }
    CF_StorageBuffer entities = buffer(count * (int)sizeof(render_material_entity));
    CF_StorageBuffer indices = buffer((int)index_bytes);
    if (!entities.id || !indices.id) {
        if (entities.id) {
            cf_destroy_storage_buffer(entities);
        }
        if (indices.id) {
            cf_destroy_storage_buffer(indices);
        }
        return false;
    }
    if (r->entities.id) {
        cf_destroy_storage_buffer(r->entities);
    }
    if (r->indices.id) {
        cf_destroy_storage_buffer(r->indices);
    }
    r->entities = entities;
    r->indices = indices;
    r->capacity = count;
    return true;
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
    if (!reserve(r, count ? count : 1)) {
        return false;
    }
    asetlen(r->sorted, count);
    for (int i = 0; i < count; i++) {
        r->sorted[i] = entities[i];
        r->sorted[i].time[1] = (float)i;
    }
    if (count) {
        qsort(r->sorted, (size_t)count, sizeof *r->sorted, compare);
        cf_update_storage_buffer(r->entities, r->sorted, count * (int)sizeof *r->sorted);
    }
    int tx = (r->width + 15) / 16, ty = (r->height + 15) / 16;
    float units[2] = { view.width / r->width, view.height / r->height };
    float pixels[2] = { r->width / view.width, r->height / view.height };
    uniform(r, "u_entity_count", &count, CF_UNIFORM_TYPE_INT, 1);
    uniform(r, "u_world_min_x", &view.min_x, CF_UNIFORM_TYPE_FLOAT, 1);
    uniform(r, "u_world_max_y", &view.max_y, CF_UNIFORM_TYPE_FLOAT, 1);
    uniform(r, "u_px_per_unit", pixels, CF_UNIFORM_TYPE_FLOAT, 2);
    uniform(r, "u_unit_per_px", units, CF_UNIFORM_TYPE_FLOAT, 2);
    uniform(r, "u_tiles_x", &tx, CF_UNIFORM_TYPE_INT, 1);
    uniform(r, "u_tiles_y", &ty, CF_UNIFORM_TYPE_INT, 1);
    uniform(r, "u_canvas_w", &r->width, CF_UNIFORM_TYPE_INT, 1);
    uniform(r, "u_canvas_h", &r->height, CF_UNIFORM_TYPE_INT, 1);
    uniform(r, "u_max_per_tile", &r->capacity, CF_UNIFORM_TYPE_INT, 1);
    CF_StorageBuffer bins[] = { r->counts, r->indices };
    CF_ComputeDispatch dispatch = cf_compute_dispatch_defaults((r->tiles + 63) / 64, 1, 1);
    dispatch.ro_buffers = &r->entities;
    dispatch.ro_buffer_count = 1;
    dispatch.rw_buffers = bins;
    dispatch.rw_buffer_count = 2;
    cf_dispatch_compute(r->bin, r->material, dispatch);
    CF_StorageBuffer inputs[] = { r->entities, r->counts, r->indices };
    dispatch = cf_compute_dispatch_defaults(tx, ty, 1);
    dispatch.ro_buffers = inputs;
    dispatch.ro_buffer_count = 3;
    dispatch.rw_textures = r->textures;
    dispatch.rw_texture_count = 4;
    cf_dispatch_compute(r->raster, r->material, dispatch);
    return true;
}

render_material_textures render_material_raster_textures(const render_material_raster *r)
{
    assert(r);
    return (
        render_material_textures
    ){ r->textures[0], r->textures[1], r->textures[2], r->textures[3] };
}
