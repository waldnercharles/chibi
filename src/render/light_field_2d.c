#include "render/light_field_2d.h"

#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIGHT_WG 16
#define LIGHT_BIN_WG 64
#define LIGHT_STRIDE 32

struct render_light_field_2d
{
    CF_ComputeShader bin_shader;
    CF_ComputeShader field_shader;
    CF_ComputeShader composite_shader;
    CF_Material bin_material;
    CF_Material field_material;
    CF_Material composite_material;
    CF_StorageBuffer sources;
    CF_StorageBuffer counts;
    CF_StorageBuffer indices;
    CF_Texture emission;
    CF_Texture combined;
    int width;
    int height;
    int tiles_x;
    int tiles_y;
    int capacity;
    render_light_field_stats stats;
    char error[256];
};

static int ceil_div(int value, int divisor)
{
    return (value + divisor - 1) / divisor;
}

static CF_Texture make_texture(int width, int height)
{
    CF_TextureParams params = cf_texture_defaults(width, height);
    params.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    params.usage = CF_TEXTURE_USAGE_SAMPLER_BIT |
                   CF_TEXTURE_USAGE_COMPUTE_STORAGE_READ_BIT |
                   CF_TEXTURE_USAGE_COMPUTE_STORAGE_WRITE_BIT;
    params.filter = CF_FILTER_LINEAR;
    params.wrap_u = params.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    return cf_make_texture(params);
}

static CF_StorageBuffer make_buffer(size_t bytes)
{
    assert(bytes > 0 && bytes <= INT_MAX);
    CF_StorageBufferParams params = cf_storage_buffer_defaults((int)bytes);
    params.compute_readable = true;
    params.compute_writable = true;
    return cf_make_storage_buffer(params);
}

static void destroy_buffer(CF_StorageBuffer *buffer)
{
    if (buffer->id) {
        cf_destroy_storage_buffer(*buffer);
        *buffer = (CF_StorageBuffer){ 0 };
    }
}

static void destroy_targets(render_light_field_2d *field)
{
    destroy_buffer(&field->sources);
    destroy_buffer(&field->counts);
    destroy_buffer(&field->indices);
    if (field->emission.id) {
        cf_destroy_texture(field->emission);
        field->emission = (CF_Texture){ 0 };
    }
    if (field->combined.id) {
        cf_destroy_texture(field->combined);
        field->combined = (CF_Texture){ 0 };
    }
    field->width = field->height = field->tiles_x = field->tiles_y = field->capacity = 0;
}

render_light_field_2d *render_light_field_2d_new()
{
    render_light_field_2d *field = xcalloc(1, sizeof *field);
    field->bin_shader = cf_make_compute_shader("hrc/light_bin.shd");
    field->field_shader = cf_make_compute_shader("hrc/light_field.shd");
    field->composite_shader = cf_make_compute_shader("hrc/light_composite.shd");
    field->bin_material = cf_make_material();
    field->field_material = cf_make_material();
    field->composite_material = cf_make_material();
    if (!field->bin_shader.id || !field->field_shader.id ||
        !field->composite_shader.id || !field->bin_material.id ||
        !field->field_material.id || !field->composite_material.id) {
        render_light_field_2d_free(field);
        return NULL;
    }
    return field;
}

void render_light_field_2d_free(render_light_field_2d *field)
{
    if (!field) {
        return;
    }
    destroy_targets(field);
    CF_ComputeShader shaders[] = { field->bin_shader,
                                   field->field_shader,
                                   field->composite_shader };
    CF_Material materials[] = { field->bin_material,
                                field->field_material,
                                field->composite_material };
    for (int i = 0; i < 3; i++) {
        if (shaders[i].id) {
            cf_destroy_compute_shader(shaders[i]);
        }
        if (materials[i].id) {
            cf_destroy_material(materials[i]);
        }
    }
    free(field);
}

const char *render_light_field_2d_error(const render_light_field_2d *field)
{
    return field && field->error[0] ? field->error : "";
}

bool render_light_field_2d_resize(render_light_field_2d *field, int width, int height)
{
    assert(field);
    if (width < 2 || height < 2 || width > 16384 || height > 16384) {
        return false;
    }
    if (field->width == width && field->height == height) {
        return true;
    }
    destroy_targets(field);
    field->emission = make_texture(width, height);
    field->combined = make_texture(width, height);
    field->tiles_x = ceil_div(width, LIGHT_WG);
    field->tiles_y = ceil_div(height, LIGHT_WG);
    field->counts = make_buffer((size_t)field->tiles_x * field->tiles_y * sizeof(uint32_t));
    if (!field->emission.id || !field->combined.id || !field->counts.id) {
        destroy_targets(field);
        return false;
    }
    field->width = width;
    field->height = height;
    render_light_field_2d_reset(field);
    return true;
}

void render_light_field_2d_reset(render_light_field_2d *field)
{
    assert(field);
    field->stats = (render_light_field_stats){ .tile_count = field->tiles_x *
                                                             field->tiles_y,
                                               .capacity = field->capacity };
    field->error[0] = 0;
    if (!field->width || !field->height) {
        return;
    }
    size_t bytes = (size_t)field->width * (size_t)field->height * 8;
    void *zeros = xcalloc(1, bytes);
    cf_texture_update(field->emission, zeros, (int)bytes);
    cf_texture_update(field->combined, zeros, (int)bytes);
    free(zeros);
}

static bool reserve(render_light_field_2d *field, int count)
{
    if (count <= field->capacity) {
        return true;
    }
    size_t source_bytes = (size_t)count * LIGHT_STRIDE;
    size_t index_bytes = (size_t)field->tiles_x * field->tiles_y * count *
                         sizeof(uint32_t);
    if (source_bytes > INT_MAX || index_bytes > INT_MAX) {
        return false;
    }
    CF_StorageBuffer sources = make_buffer(source_bytes);
    CF_StorageBuffer indices = make_buffer(index_bytes);
    if (!sources.id || !indices.id) {
        destroy_buffer(&sources);
        destroy_buffer(&indices);
        return false;
    }
    destroy_buffer(&field->sources);
    destroy_buffer(&field->indices);
    field->sources = sources;
    field->indices = indices;
    field->capacity = count;
    return true;
}

static bool valid_source(const render_point_light_2d *source)
{
    return isfinite(source->position.x) && isfinite(source->position.y) &&
           isfinite(source->intensity) && isfinite(source->radius) &&
           source->intensity >= 0 && source->radius > 0 &&
           isfinite(source->color[0]) && isfinite(source->color[1]) &&
           isfinite(source->color[2]) && isfinite(source->color[3]);
}

static void diagnostics(
    render_light_field_2d *field,
    const render_point_light_2d *sources,
    int count,
    render_view_2d view
)
{
    field->stats = (render_light_field_stats){ .source_count = count,
                                               .tile_count = field->tiles_x *
                                                             field->tiles_y,
                                               .capacity = field->capacity };
    float unit_x = view.width / field->width;
    float unit_y = view.height / field->height;
    for (int tile_y = 0; tile_y < field->tiles_y; tile_y++) {
        for (int tile_x = 0; tile_x < field->tiles_x; tile_x++) {
            float min_x = view.min_x + tile_x * LIGHT_WG * unit_x;
            float max_x = fminf(view.min_x + view.width, min_x + LIGHT_WG * unit_x);
            float max_y = view.max_y - tile_y * LIGHT_WG * unit_y;
            float min_y = fmaxf(view.max_y - view.height, max_y - LIGHT_WG * unit_y);
            int occupancy = 0;
            for (int i = 0; i < count; i++) {
                const render_point_light_2d *source = &sources[i];
                bool overlaps = source->position.x + source->radius >= min_x &&
                                source->position.x - source->radius <= max_x &&
                                source->position.y + source->radius >= min_y &&
                                source->position.y - source->radius <= max_y;
                occupancy += overlaps;
            }
            field->stats.max_tile_occupancy = field->stats.max_tile_occupancy > occupancy
                                                ? field->stats.max_tile_occupancy
                                                : occupancy;
        }
    }
    for (int i = 0; i < count; i++) {
        const render_point_light_2d *source = &sources[i];
        bool overlaps = source->position.x + source->radius >= view.min_x &&
                        source->position.x - source->radius <= view.min_x + view.width &&
                        source->position.y + source->radius >= view.max_y - view.height &&
                        source->position.y - source->radius <= view.max_y;
        if (overlaps) {
            field->stats.accepted_source_count++;
        }
        bool inside = source->position.x - source->radius >= view.min_x &&
                      source->position.x + source->radius <= view.min_x + view.width &&
                      source->position.y - source->radius >= view.max_y - view.height &&
                      source->position.y + source->radius <= view.max_y;
        if (!inside) {
            field->stats.clipped_source_count++;
        }
    }
    if (field->stats.clipped_source_count) {
        snprintf(
            field->error,
            sizeof field->error,
            "light field clipped %d of %d source bounds",
            field->stats.clipped_source_count,
            count
        );
    }
}

static void set_view_uniforms(CF_Material material, render_light_field_2d *field, render_view_2d view, int count)
{
    float units[] = { view.width / field->width, view.height / field->height };
    int values[] = { count,           field->tiles_x, field->tiles_y,
                     field->capacity, field->width,   field->height };
    cf_material_set_uniform_cs(material, "u_source_count", values + 0, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(material, "u_tiles_x", values + 1, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(material, "u_tiles_y", values + 2, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(material, "u_max_per_tile", values + 3, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(material, "u_canvas_w", values + 4, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(material, "u_canvas_h", values + 5, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(material, "u_world_min_x", &view.min_x, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_cs(material, "u_world_max_y", &view.max_y, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_cs(material, "u_unit_per_px", units, CF_UNIFORM_TYPE_FLOAT, 2);
}

bool render_light_field_2d_draw(
    render_light_field_2d *field,
    const render_point_light_2d *sources,
    int count,
    render_view_2d view
)
{
    assert(field && count >= 0 && (sources || !count));
    field->error[0] = 0;
    if (!field->width || !isfinite(view.min_x) || !isfinite(view.max_y) ||
        !isfinite(view.width) || !isfinite(view.height) || view.width <= 0 ||
        view.height <= 0) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (!valid_source(&sources[i])) {
            snprintf(field->error, sizeof field->error, "invalid 2D light source %d", i);
            return false;
        }
    }
    if (!reserve(field, count ? count : 1)) {
        snprintf(field->error, sizeof field->error, "2D light field capacity allocation failed for %d sources", count);
        return false;
    }
    field->stats.capacity = field->capacity;
    diagnostics(field, sources, count, view);
    if (count) {
        cf_update_storage_buffer(field->sources, sources, count * LIGHT_STRIDE);
    }
    set_view_uniforms(field->bin_material, field, view, count);
    CF_StorageBuffer bin_outputs[] = { field->counts, field->indices };
    CF_ComputeDispatch dispatch = cf_compute_dispatch_defaults(
        ceil_div(field->tiles_x * field->tiles_y, LIGHT_BIN_WG),
        1,
        1
    );
    dispatch.ro_buffers = &field->sources;
    dispatch.ro_buffer_count = 1;
    dispatch.rw_buffers = bin_outputs;
    dispatch.rw_buffer_count = 2;
    cf_dispatch_compute(field->bin_shader, field->bin_material, dispatch);

    set_view_uniforms(field->field_material, field, view, count);
    CF_StorageBuffer field_inputs[] = { field->sources, field->counts, field->indices };
    dispatch = cf_compute_dispatch_defaults(
        ceil_div(field->width, LIGHT_WG),
        ceil_div(field->height, LIGHT_WG),
        1
    );
    dispatch.ro_buffers = field_inputs;
    dispatch.ro_buffer_count = 3;
    dispatch.rw_textures = &field->emission;
    dispatch.rw_texture_count = 1;
    cf_dispatch_compute(field->field_shader, field->field_material, dispatch);
    return true;
}

bool render_light_field_2d_composite(render_light_field_2d *field, CF_Texture base_emission)
{
    assert(field);
    if (!field->combined.id || !base_emission.id) {
        return false;
    }
    int values[] = { field->width, field->height };
    cf_material_set_uniform_cs(field->composite_material, "u_canvas_w", values + 0, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(field->composite_material, "u_canvas_h", values + 1, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_texture_cs(field->composite_material, "u_base", base_emission);
    cf_material_set_texture_cs(field->composite_material, "u_field", field->emission);
    CF_ComputeDispatch dispatch = cf_compute_dispatch_defaults(
        ceil_div(field->width, LIGHT_WG),
        ceil_div(field->height, LIGHT_WG),
        1
    );
    dispatch.rw_textures = &field->combined;
    dispatch.rw_texture_count = 1;
    cf_dispatch_compute(field->composite_shader, field->composite_material, dispatch);
    return true;
}

CF_Texture render_light_field_2d_texture(const render_light_field_2d *field)
{
    assert(field);
    return field->combined;
}

render_light_field_stats render_light_field_2d_stats(const render_light_field_2d *field)
{
    assert(field);
    return field->stats;
}
