#include "render/fluid_raster.h"

#include "diagnostics/tracy.h"
#include "render/fullscreen.h"
#include "render/particles.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <string.h>

typedef struct
{
    float position[4], color[4], optical[4], detail[4];
} raster_particle;

typedef struct
{
    int x0, y0, x1, y1;
} tile_rect;

struct render_fluid_raster
{
    CF_Shader shader, copy_shader;
    CF_Material material;
    /* Two triangles over each tile that fluid covers; passes draw only there. */
    CF_Mesh tiles;
    float *tile_vertices;
    int tile_capacity;
    CF_StorageBuffer particles, offsets, indices;
    /* The material targets of fluid's tiles before fluid. */
    CF_Canvas before;
    int width, height;
    /* Whether a burning or emissive parcel lay in the last view. */
    bool hot;
    float time, caustics;
    int particle_capacity, offset_capacity, index_capacity;
    raster_particle *data;
    tile_rect *rects;
    int *starts, *cursor, *references;
};

render_fluid_raster *render_fluid_raster_new()
{
    render_fluid_raster *r = xcalloc(1, sizeof *r);
    r->shader = cf_make_shader("/chibi-render/hrc/pass.vert", "/chibi-render/hrc/fluid_raster.frag");
    r->copy_shader = cf_make_shader("/chibi-render/hrc/pass.vert", "/chibi-render/hrc/material_copy.frag");
    r->material = cf_make_material();
    if (!r->shader.id || !r->copy_shader.id || !r->material.id) {
        render_fluid_raster_free(r);
        return NULL;
    }
    return r;
}

void render_fluid_raster_free(render_fluid_raster *r)
{
    if (!r) {
        return;
    }
    if (r->shader.id) {
        cf_destroy_shader(r->shader);
    }
    if (r->copy_shader.id) {
        cf_destroy_shader(r->copy_shader);
    }
    if (r->tiles.id) {
        cf_destroy_mesh(r->tiles);
    }
    afree(r->tile_vertices);
    if (r->material.id) {
        cf_destroy_material(r->material);
    }
    if (r->particles.id) {
        cf_destroy_storage_buffer(r->particles);
    }
    if (r->offsets.id) {
        cf_destroy_storage_buffer(r->offsets);
    }
    if (r->indices.id) {
        cf_destroy_storage_buffer(r->indices);
    }
    if (r->before.id) {
        cf_destroy_canvas(r->before);
    }
    afree(r->data);
    afree(r->rects);
    afree(r->starts);
    afree(r->cursor);
    afree(r->references);
    free(r);
}

/* Clip-space triangles over the tiles with particles. Returns their vertex
 * count, zero when fluid covers nothing. */
static int s_tile_mesh(render_fluid_raster *r, int tx, int ty, int width, int height)
{
    aclear(r->tile_vertices);
    for (int tile = 0; tile < tx * ty; tile++) {
        if (r->starts[tile] == r->starts[tile + 1]) {
            continue;
        }
        int x = tile % tx, y = tile / tx;
        float x0 = 32.0f * x / width - 1, x1 = fminf(32.0f * (x + 1) / width - 1, 1);
        float y0 = 1 - 32.0f * y / height,
              y1 = fmaxf(1 - 32.0f * (y + 1) / height, -1);
        float quad[12] = { x0, y0, x1, y0, x0, y1, x1, y0, x1, y1, x0, y1 };
        for (int i = 0; i < 12; i++) {
            apush(r->tile_vertices, quad[i]);
        }
    }
    int vertices = acount(r->tile_vertices) / 2;
    if (vertices > r->tile_capacity) {
        if (r->tiles.id) {
            cf_destroy_mesh(r->tiles);
        }
        CF_VertexAttribute attribute = { .name = "in_pos",
                                         .format = CF_VERTEX_FORMAT_FLOAT2 };
        r->tiles = cf_make_mesh(vertices * 2 * (int)sizeof(float), &attribute, 1, 2 * (int)sizeof(float));
        r->tile_capacity = vertices;
    }
    if (vertices) {
        cf_mesh_update_vertex_data(r->tiles, r->tile_vertices, vertices);
    }
    return vertices;
}

static void s_draw_tiles(
    render_fluid_raster *r,
    CF_Shader shader,
    CF_Canvas target,
    bool clear,
    int width,
    int height
)
{
    cf_apply_canvas(target, clear);
    cf_apply_mesh(r->tiles);
    cf_apply_shader(shader, r->material);
    CF_StorageBuffer inputs[] = { r->particles, r->offsets, r->indices };
    if (shader.id != r->copy_shader.id) {
        cf_apply_fs_storage_buffers(inputs, 3);
    }
    cf_apply_viewport(0, 0, width, height);
    cf_apply_scissor(0, 0, width, height);
    cf_draw_elements();
}

static void s_set_material(CF_Material material, render_material_textures textures)
{
    cf_material_set_texture_fs(material, "u_scene", textures.scene);
    cf_material_set_texture_fs(material, "u_source", textures.source);
    cf_material_set_texture_fs(material, "u_albedo", textures.albedo);
    cf_material_set_texture_fs(material, "u_normal", textures.normal);
    cf_material_set_texture_fs(material, "u_visibility", textures.visibility);
    cf_material_set_texture_fs(material, "u_cover", textures.cover);
}

static bool reserve(CF_StorageBuffer *buffer, int *capacity, int count, int stride)
{
    if (*capacity >= count) {
        return true;
    }
    int size = 256;
    while (size < count) {
        size *= 2;
    }
    CF_StorageBuffer next = cf_make_storage_buffer(cf_storage_buffer_defaults(size * stride));
    if (!next.id) {
        return false;
    }
    if (buffer->id) {
        cf_destroy_storage_buffer(*buffer);
    }
    *buffer = next;
    *capacity = size;
    return true;
}

static void collect(
    render_fluid_raster *r,
    const material_fluid *fluid,
    render_material_view view,
    int width,
    int height
)
{
    aclear(r->data);
    aclear(r->rects);
    r->hot = false;
    int count;
    const fluid_particle *particles = material_fluid_particles(fluid, &count);
    fluid_heat heat = material_fluid_heat(fluid);
    float px = width / view.width, py = height / view.height;
    int tx = (width + 15) / 16, ty = (height + 15) / 16;
    for (int i = 0; i < count; i++) {
        fluid_particle p = particles[i];
        float age = p.style.duration > 0 ? 1 - p.lifetime / p.style.duration : 0;
        float radius = p.style.duration > 0 && !p.billow
                         ? game_particle_curve_value(
                               p.radius,
                               p.style.size_end,
                               p.style.size_exponent,
                               age
                           )
                         : p.radius;
        /* Powder draws as a smooth union of grains rather than a liquid
         * surface. */
        float blend = p.phase == FLUID_POWDER ? radius * .5f : p.style.blend;
        if (p.billow) {
            radius *= 1 + age * .6f;
        }
        /* Only hot gas draws separate hot cores; hot liquid just glows. */
        float cores = p.phase == FLUID_GAS ? p.glow : 0;
        float speed = hypotf(p.velocity.x, p.velocity.y);
        float stretch = cores > 0 ? fminf(1.5f, speed * .012f) : 0;
        bool wisp = p.phase == FLUID_GAS && blend == 0 && p.emission == 0 &&
                    p.glow == 0;
        if (wisp) {
            /* Broader support connects sparse parcels. Stretch preserves area;
             * the shader compensates density for the increased support radius. */
            radius *= 1.5f;
            stretch = 1 + fminf(.5f, speed * .008f);
        }
        if (radius <= .001f) {
            continue;
        }
        float x = (p.position.x - view.min_x) * px;
        /* Physics and emitters use game Y-down, like GameTransform. */
        float y = (view.max_y + p.position.y) * py;
        float rx = (radius + blend) * px, ry = (radius + blend) * py;
        if (wisp) {
            float dx = speed > .001f ? p.velocity.x / speed : 1;
            float dy = speed > .001f ? p.velocity.y / speed : 0;
            float along = stretch * stretch, across = 1 / along;
            rx *= sqrtf(dx * dx * along + dy * dy * across);
            ry *= sqrtf(dy * dy * along + dx * dx * across);
        }
        if (x + rx < 0 || y + ry < 0 || x - rx >= width || y - ry >= height) {
            continue;
        }
        tile_rect rect = { (int)fmaxf(0, floorf((x - rx) / 16)),
                           (int)fmaxf(0, floorf((y - ry) / 16)),
                           (int)fminf(tx - 1, floorf((x + rx) / 16)),
                           (int)fminf(ty - 1, floorf((y + ry) / 16)) };
        raster_particle data = {
            .position = { p.position.x, -p.position.y, radius, p.phase != FLUID_GAS },
            .optical = { p.absorption, p.emission + heat.glow_emission * p.glow, blend, cores },
            .detail = { speed > .001f ? p.velocity.x / speed : 1,
                        speed > .001f ? -p.velocity.y / speed : 0,
                        p.phase != FLUID_GAS ? p.refraction
                        : wisp               ? -stretch
                                             : stretch,
                        p.visibility }
        };
        for (int channel = 0; channel < 4; channel++) {
            int shift = 24 - 8 * channel;
            data.color[channel] = ((p.color >> shift) & 255) / 255.f;
            if (p.style.duration > 0 && !p.billow) {
                float end = ((p.style.end_color >> shift) & 255) / 255.f;
                data.color[channel] += (end - data.color[channel]) * age;
            }
            /* Hot matter shifts toward the glow color. */
            if (p.glow > 0) {
                float hot = ((heat.glow_color >> shift) & 255) / 255.f;
                data.color[channel] += (hot - data.color[channel]) * p.glow;
            }
        }
        if (p.billow) {
            float remaining = fmaxf(0, 1 - age);
            float opacity = remaining * remaining /
                            ((1 + age * .6f) * (1 + age * .6f));
            data.color[3] *= opacity;
            data.optical[0] *= opacity;
            data.optical[1] *= remaining;
        } else if (p.style.duration > 0) {
            data.optical[1] = game_particle_curve_value(
                p.emission,
                p.style.emission_end,
                p.style.emission_exponent,
                age
            );
        } else if (p.lifetime > 0) {
            data.color[3] *= fminf(1, p.lifetime * 2);
            data.optical[1] *= fminf(1, p.lifetime * 2);
            data.optical[0] *= fminf(1, p.lifetime * 2);
        }
        r->hot |= data.optical[1] > 0 || data.optical[3] > 0;
        apush(r->data, data);
        apush(r->rects, rect);
    }
}

static bool bin(render_fluid_raster *r, int tx, int ty)
{
    int tiles = tx * ty;
    asetlen(r->starts, tiles + 1);
    memset(r->starts, 0, (size_t)(tiles + 1) * sizeof *r->starts);
    for (int i = 0; i < acount(r->rects); i++) {
        tile_rect rect = r->rects[i];
        for (int y = rect.y0; y <= rect.y1; y++) {
            for (int x = rect.x0; x <= rect.x1; x++) {
                r->starts[y * tx + x + 1]++;
            }
        }
    }
    for (int i = 0; i < tiles; i++) {
        r->starts[i + 1] += r->starts[i];
        /* Bound pathological zoom/population combinations without silently
         * discarding visible material. Ordinary views need far fewer entries. */
        if (r->starts[i + 1] > 8000000) {
            return false;
        }
    }
    acopy(r->cursor, r->starts);
    asetlen(r->references, r->starts[tiles]);
    for (int i = 0; i < acount(r->rects); i++) {
        tile_rect rect = r->rects[i];
        for (int y = rect.y0; y <= rect.y1; y++) {
            for (int x = rect.x0; x <= rect.x1; x++) {
                r->references[r->cursor[y * tx + x]++] = i;
            }
        }
    }
    return true;
}

void render_fluid_raster_set_caustics(render_fluid_raster *r, float time, float strength)
{
    r->time = time;
    r->caustics = strength;
}

bool render_fluid_raster_hot(const render_fluid_raster *r)
{
    return r->hot;
}

bool render_fluid_raster_draw(
    render_fluid_raster *r,
    const material_fluid *fluid,
    CF_Canvas material,
    render_material_view view,
    int width,
    int height
)
{
    assert(width > 0 && height > 0);
    if (r->width != width || r->height != height) {
        CF_Canvas before = render_material_canvas(width, height);
        if (!before.id) {
            return false;
        }
        if (r->before.id) {
            cf_destroy_canvas(r->before);
        }
        r->before = before;
        r->width = width;
        r->height = height;
    }
    CHIBI_TRACE_ZONE_BEGIN(collect_zone, "Fluid raster collect");
    collect(r, fluid, view, width, height);
    CHIBI_TRACE_ZONE_END(collect_zone);
    CHIBI_TRACE_ZONE_BEGIN(bin_zone, "Fluid raster bin");
    int tx = (width + 15) / 16, ty = (height + 15) / 16;
    bool binned = bin(r, tx, ty);
    CHIBI_TRACE_ZONE_END(bin_zone);
    if (!binned ||
        !reserve(
            &r->particles,
            &r->particle_capacity,
            acount(r->data) + 1,
            sizeof *r->data
        ) ||
        !reserve(&r->offsets, &r->offset_capacity, acount(r->starts), sizeof *r->starts) ||
        !reserve(
            &r->indices,
            &r->index_capacity,
            acount(r->references) + 1,
            sizeof *r->references
        )) {
        return false;
    }
    CHIBI_TRACE_ZONE_BEGIN(submit_zone, "Fluid raster submission");
    if (acount(r->data)) {
        cf_update_storage_buffer(
            r->particles,
            r->data,
            acount(r->data) * sizeof *r->data
        );
    }
    cf_update_storage_buffer(
        r->offsets,
        r->starts,
        acount(r->starts) * sizeof *r->starts
    );
    if (acount(r->references)) {
        cf_update_storage_buffer(
            r->indices,
            r->references,
            acount(r->references) * sizeof *r->references
        );
    }
    float camera[4] = { view.min_x, view.max_y, view.width / width, view.height / height };
    cf_material_set_uniform_fs(r->material, "u_view", camera, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(r->material, "u_tiles_x", &tx, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_fs(r->material, "u_time", &r->time, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_fs(r->material, "u_caustics", &r->caustics, CF_UNIFORM_TYPE_FLOAT, 1);
    /* Fluid reads a copy of its tiles and writes the material itself. */
    cf_push_gpu_label("Fluid density raster");
    if (s_tile_mesh(r, tx, ty, width, height)) {
        s_set_material(r->material, render_material_canvas_textures(material));
        s_draw_tiles(r, r->copy_shader, r->before, true, width, height);
        s_set_material(r->material, render_material_canvas_textures(r->before));
        s_draw_tiles(r, r->shader, material, false, width, height);
    }
    cf_pop_gpu_label();
    CHIBI_TRACE_ZONE_END(submit_zone);
    return true;
}
