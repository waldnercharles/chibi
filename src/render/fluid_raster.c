#include "render/fluid_raster.h"

#include "diagnostics/tracy.h"
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
    CF_ComputeShader shader;
    CF_Material material;
    CF_StorageBuffer particles, offsets, indices;
    CF_Texture visibility;
    int width, height;
    int particle_capacity, offset_capacity, index_capacity;
    raster_particle *data;
    tile_rect *rects;
    int *starts, *cursor, *references;
};

render_fluid_raster *render_fluid_raster_new()
{
    render_fluid_raster *r = xcalloc(1, sizeof *r);
    r->shader = cf_make_compute_shader("hrc/fluid_raster.shd");
    r->material = cf_make_material();
    if (!r->shader.id || !r->material.id) {
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
        cf_destroy_compute_shader(r->shader);
    }
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
    if (r->visibility.id) {
        cf_destroy_texture(r->visibility);
    }
    afree(r->data);
    afree(r->rects);
    afree(r->starts);
    afree(r->cursor);
    afree(r->references);
    free(r);
}

CF_Texture render_fluid_raster_visibility(const render_fluid_raster *r)
{
    return r->visibility;
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
    int count;
    const fluid_particle *particles = material_fluid_particles(fluid, &count);
    float px = width / view.width, py = height / view.height;
    int tx = (width + 15) / 16, ty = (height + 15) / 16;
    for (int i = 0; i < count; i++) {
        fluid_particle p = particles[i];
        float age = p.style.duration > 0 ? 1 - p.lifetime / p.style.duration : 0;
        float radius = p.style.duration > 0 && !p.smoke
                         ? game_particle_curve_value(
                               p.radius,
                               p.style.size_end,
                               p.style.size_exponent,
                               age
                           )
                         : p.radius;
        float blend = p.style.blend;
        if (p.smoke) {
            radius *= 1 + age * .6f;
        }
        float speed = hypotf(p.velocity.x, p.velocity.y);
        float stretch = p.glow > 0 ? fminf(1.5f, speed * .012f) : 0;
        bool wisp = p.kind == FLUID_GAS && blend == 0 && p.emission == 0 &&
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
            .position = { p.position.x, -p.position.y, radius, p.kind },
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
    render_material_textures textures,
    render_material_view view,
    int width,
    int height
)
{
    assert(width > 0 && height > 0);
    if (r->width != width || r->height != height) {
        CF_TextureParams params = cf_texture_defaults(width, height);
        params.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
        params.usage = CF_TEXTURE_USAGE_SAMPLER_BIT |
                       CF_TEXTURE_USAGE_COMPUTE_STORAGE_WRITE_BIT;
        params.filter = CF_FILTER_LINEAR;
        CF_Texture next = cf_make_texture(params);
        if (!next.id) {
            return false;
        }
        if (r->visibility.id) {
            cf_destroy_texture(r->visibility);
        }
        r->visibility = next;
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
    int dimensions[2] = { width, height };
    cf_material_set_uniform_cs(r->material, "u_view", camera, CF_UNIFORM_TYPE_FLOAT, 4);
    cf_material_set_uniform_cs(r->material, "u_dimensions", dimensions, CF_UNIFORM_TYPE_INT, 2);
    cf_material_set_uniform_cs(r->material, "u_tiles_x", &tx, CF_UNIFORM_TYPE_INT, 1);
    CF_StorageBuffer inputs[] = { r->particles, r->offsets, r->indices };
    CF_Texture outputs[] = { textures.scene,      textures.emission,
                             textures.absorption, textures.albedo,
                             textures.normal,     r->visibility };
    CF_ComputeDispatch dispatch = cf_compute_dispatch_defaults(tx, ty, 1);
    dispatch.ro_buffers = inputs;
    dispatch.ro_buffer_count = 3;
    dispatch.rw_textures = outputs;
    dispatch.rw_texture_count = 6;
    cf_push_gpu_label("Fluid density raster");
    cf_dispatch_compute(r->shader, r->material, dispatch);
    cf_pop_gpu_label();
    CHIBI_TRACE_ZONE_END(submit_zone);
    return true;
}
