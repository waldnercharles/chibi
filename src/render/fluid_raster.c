#include "render/fluid_raster.h"

#include "diagnostics/tracy.h"
#include "render/fullscreen.h"
#include "runtime/particles.h"
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

enum
{
    FLUID_RASTER_TILE_SIZE = 8,
    FLUID_RASTER_GRAIN_GAS = 2,
    FLUID_RASTER_GRAIN_LIQUID = 3
};

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
    int particle_capacity, offset_capacity, index_capacity;
    raster_particle *data;
    tile_rect *rects;
    int *starts, *cursor, *references;
    int *visible;
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
    afree(r->visible);
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
        float extent = 2.f * FLUID_RASTER_TILE_SIZE;
        float x0 = extent * x / width - 1,
              x1 = fminf(extent * (x + 1) / width - 1, 1);
        float y0 = 1 - extent * y / height,
              y1 = fmaxf(1 - extent * (y + 1) / height, -1);
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

/* Principal stretches ignore rigid rotation. Log stretch treats compression
 * and extension equally; a factor of two fills the diagnostic scale. */
static float strain(fluid_particle p)
{
    if (p.constitutive.shear_modulus <= 0) {
        return 0;
    }
    const float *f = p.deformation.f;
    float a = f[0] * f[0] + f[2] * f[2];
    float b = f[0] * f[1] + f[2] * f[3];
    float d = f[1] * f[1] + f[3] * f[3];
    float gap = hypotf(a - d, 2 * b);
    float high = fmaxf(.000001f, (a + d + gap) * .5f);
    float low = fmaxf(.000001f, (a + d - gap) * .5f);
    return fmaxf(fabsf(logf(high)), fabsf(logf(low))) / (2 * logf(2));
}

static void diagnostic(raster_particle *data, fluid_particle p, int mode)
{
    float value = mode == FLUID_VIEW_HEAT ? p.heat
                : mode == FLUID_VIEW_SPEED ? hypotf(p.velocity.x, p.velocity.y) / 64
                                           : strain(p);
    static const float palette[][3] = { { .015f, .035f, .25f },
                                        { 0, .55f, .8f },
                                        { .85f, .75f, .015f },
                                        { .9f, .035f, .015f } };
    float t = fminf(1, fmaxf(0, value)) * 3;
    int stop = (int)fminf(2, t);
    for (int channel = 0; channel < 3; channel++) {
        data->color[channel] = palette[stop][channel] +
                               (palette[stop + 1][channel] - palette[stop][channel]) *
                                   (t - stop);
    }
    data->color[3] = 1;
    data->optical[1] = 0;
    data->optical[3] = 0;
    data->detail[2] = p.kind == FLUID_LIQUID ? 0 : data->detail[2];
    data->detail[3] = 1;
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
    int count;
    const fluid_particle *particles = material_fluid_particles(fluid, &count);
    float px = width / view.width, py = height / view.height;
    float support = material_fluid_visual_support(fluid) + fmaxf(1 / px, 1 / py);
    material_fluid_query(
        fluid,
        (fluid_region){ view.min_x - support,
                        -view.max_y - support,
                        view.width + 2 * support,
                        view.height + 2 * support },
        &r->visible
    );
    int mode = material_fluid_debug_view(fluid);
    int tx = (width + FLUID_RASTER_TILE_SIZE - 1) / FLUID_RASTER_TILE_SIZE,
        ty = (height + FLUID_RASTER_TILE_SIZE - 1) / FLUID_RASTER_TILE_SIZE;
    for (int i = 0; i < acount(r->visible); i++) {
        int index = r->visible[i];
        assert(index >= 0 && index < count);
        fluid_particle p = particles[index];
        float age = p.style.duration > 0 ? 1 - p.lifetime / p.style.duration : 0;
        float radius = p.style.duration > 0 && !p.smoke ? particle_curve_value(
                                                              p.radius,
                                                              p.style.size_end,
                                                              p.style.size_exponent,
                                                              age
                                                          )
                                                        : p.radius;
        float blend = p.style.blend;
        bool grain = p.grain_radius > 0 && !p.smoke;
        if (grain) {
            radius = p.grain_radius;
            blend = 0;
        }
        if (p.smoke) {
            radius *= 1 + age * .6f;
        }
        float speed = hypotf(p.velocity.x, p.velocity.y);
        float stretch = !grain && p.glow > 0 ? fminf(1.5f, speed * .012f) : 0;
        bool wisp = !grain && p.kind == FLUID_GAS && blend == 0 &&
                    p.emission == 0 && p.glow == 0;
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
        /* Physics and emitters use game Y-down, like Transform. */
        float y = (view.max_y + p.position.y) * py;
        float rx = (radius + blend) * px, ry = (radius + blend) * py;
        if (grain) {
            /* Include the disc's half-pixel AA fringe in culling and binning. */
            float aa = fmaxf(1 / px, 1 / py) * .5f;
            rx += aa * px;
            ry += aa * py;
        }
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
        tile_rect rect = {
            (int)fmaxf(0, floorf((x - rx) / FLUID_RASTER_TILE_SIZE)),
            (int)fmaxf(0, floorf((y - ry) / FLUID_RASTER_TILE_SIZE)),
            (int)fminf(tx - 1, floorf((x + rx) / FLUID_RASTER_TILE_SIZE)),
            (int)fminf(ty - 1, floorf((y + ry) / FLUID_RASTER_TILE_SIZE))
        };
        raster_particle data = {
            .position = { p.position.x,
                          -p.position.y,
                          radius,
                          grain ? (p.kind == FLUID_LIQUID ? FLUID_RASTER_GRAIN_LIQUID
                                                          : FLUID_RASTER_GRAIN_GAS)
                                : p.kind },
            .optical = { p.absorption, p.emission, blend, p.glow },
            .detail = { speed > .001f ? p.velocity.x / speed : 1,
                        speed > .001f ? -p.velocity.y / speed : 0,
                        p.kind == FLUID_LIQUID ? p.refraction
                        : wisp                 ? -stretch
                                               : stretch,
                        p.visibility }
        };
        for (int channel = 0; channel < 4; channel++) {
            data.color[channel] = ((p.color >> (24 - 8 * channel)) & 255) / 255.f;
            if (p.style.duration > 0 && !p.smoke) {
                float end = ((p.style.end_color >> (24 - 8 * channel)) & 255) / 255.f;
                data.color[channel] += (end - data.color[channel]) * age;
            }
        }
        if (p.smoke) {
            float remaining = fmaxf(0, 1 - age);
            float opacity = remaining * remaining /
                            ((1 + age * .6f) * (1 + age * .6f));
            data.color[3] *= opacity;
            data.optical[0] *= opacity;
            data.optical[1] *= remaining;
        } else if (p.style.duration > 0) {
            data.optical[1] = particle_curve_value(
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
        if (mode != FLUID_VIEW_MATERIAL) {
            diagnostic(&data, p, mode);
        }
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
    int tx = (width + FLUID_RASTER_TILE_SIZE - 1) / FLUID_RASTER_TILE_SIZE,
        ty = (height + FLUID_RASTER_TILE_SIZE - 1) / FLUID_RASTER_TILE_SIZE;
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
    int tile_size = FLUID_RASTER_TILE_SIZE;
    cf_material_set_uniform_fs(r->material, "u_tile_size", &tile_size, CF_UNIFORM_TYPE_INT, 1);
    int debug = material_fluid_debug_view(fluid) != FLUID_VIEW_MATERIAL;
    cf_material_set_uniform_fs(r->material, "u_debug", &debug, CF_UNIFORM_TYPE_INT, 1);
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
