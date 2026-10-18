#include "render/material_mesh.h"

#include "render/material_height.h"
#include "runtime/geometry/material_surface_mesh.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdlib.h>

typedef struct
{
    uint64_t incarnation, revision;
    unsigned mask_revision;
    MaterialSurface3D options;
    MaterialFrame source;
    render_mesh_asset mesh;
    render_mesh_surface surface;
    render_material_height *height;
    bool used;
} material_mesh_entry;

struct render_material_mesh_cache
{
    material_mesh_entry **entries;
    render_shader_parameter_cache *parameters;
};

render_material_mesh_cache *render_material_mesh_cache_new()
{
    return xcalloc(1, sizeof(render_material_mesh_cache));
}

static void retire(material_mesh_entry *entry, render_material_raster *raster)
{
    if (raster) {
        render_material_raster_forget_mesh(raster, entry->incarnation);
    }
    render_mesh_surface_free(&entry->surface);
    render_mesh_asset_free(&entry->mesh);
    render_material_height_free(entry->height);
    free(entry);
}

void render_material_mesh_cache_clear(render_material_mesh_cache *cache, render_material_raster *raster)
{
    for (int i = 0; i < acount(cache->entries); i++) {
        retire(cache->entries[i], raster);
    }
    aclear(cache->entries);
}

void render_material_mesh_cache_free(render_material_mesh_cache *cache, render_material_raster *raster)
{
    render_material_mesh_cache_clear(cache, raster);
    afree(cache->entries);
    render_shader_parameter_cache_free(cache->parameters);
    free(cache);
}

void render_material_mesh_cache_begin(render_material_mesh_cache *cache)
{
    for (int i = 0; i < acount(cache->entries); i++) {
        cache->entries[i]->used = false;
    }
}

void render_material_mesh_cache_end(render_material_mesh_cache *cache, render_material_raster *raster)
{
    for (int i = acount(cache->entries) - 1; i >= 0; i--) {
        if (!cache->entries[i]->used) {
            retire(cache->entries[i], raster);
            adel(cache->entries, i);
            continue;
        }
        if (raster && !acount(cache->entries[i]->surface.parts)) {
            render_material_raster_forget_mesh(raster, cache->entries[i]->incarnation);
        }
    }
}

const render_mesh_asset *render_material_mesh_cache_asset(
    const render_material_mesh_cache *cache,
    uint64_t incarnation,
    uint64_t *revision
)
{
    for (int i = 0; i < acount(cache->entries); i++) {
        const material_mesh_entry *entry = cache->entries[i];
        if (entry->incarnation == incarnation) {
            *revision = entry->revision;
            return &entry->mesh;
        }
    }
    return NULL;
}

static bool same_options(MaterialSurface3D a, MaterialSurface3D b)
{
    return a.enabled == b.enabled && a.material_height == b.material_height &&
           a.elevation == b.elevation && a.depth == b.depth &&
           a.bevel_width == b.bevel_width && a.relief == b.relief &&
           a.relief_scale == b.relief_scale && a.spacing == b.spacing &&
           a.seed == b.seed && a.recess_width == b.recess_width &&
           a.recess_depth == b.recess_depth &&
           a.recess_smoothing == b.recess_smoothing;
}

static void place(material_mesh_entry *entry, GlobalTransform2D placement)
{
    float c = cosf(placement.radians), s = sinf(placement.radians);
    CF_M4x4 frame = cf_m4_identity();
    frame.elements[0] = c;
    frame.elements[2] = s;
    frame.elements[8] = -s;
    frame.elements[10] = c;
    frame.elements[12] = placement.position.x;
    frame.elements[14] = placement.position.y;
    if (acount(entry->surface.parts)) {
        entry->surface.parts[0].visual.frame = frame;
    }
    CF_V3 lo = entry->mesh.min, hi = entry->mesh.max;
    entry->surface.min = (CF_V3){ INFINITY, lo.y, INFINITY };
    entry->surface.max = (CF_V3){ -INFINITY, hi.y, -INFINITY };
    for (int i = 0; i < 4; i++) {
        float x = i & 1 ? hi.x : lo.x, z = i & 2 ? hi.z : lo.z;
        float wx = c * x - s * z + placement.position.x;
        float wz = s * x + c * z + placement.position.y;
        entry->surface.min.x = fminf(entry->surface.min.x, wx);
        entry->surface.min.z = fminf(entry->surface.min.z, wz);
        entry->surface.max.x = fmaxf(entry->surface.max.x, wx);
        entry->surface.max.z = fmaxf(entry->surface.max.z, wz);
    }
}

const render_mesh_surface *render_material_mesh_cache_get(
    render_material_mesh_cache *cache,
    material_occupancy_view mask,
    MaterialSurface3D options,
    MaterialFrame source,
    GlobalTransform2D placement,
    ecs_world *world,
    ecs_entity entity,
    char *error,
    size_t size
)
{
    material_mesh_entry *entry = NULL;
    for (int i = 0; i < acount(cache->entries); i++) {
        if (cache->entries[i]->incarnation == mask.incarnation) {
            entry = cache->entries[i];
            break;
        }
    }
    if (!entry) {
        entry = xcalloc(1, sizeof *entry);
        entry->incarnation = mask.incarnation;
        apush(cache->entries, entry);
    }
    entry->used = true;
    render_material_height *height = NULL;
    if (options.material_height && !render_material_height_prepare(
                                       world,
                                       entity,
                                       Shader_get_const(world, entity),
                                       &cache->parameters,
                                       entry->height,
                                       &height,
                                       error,
                                       size
                                   )) {
        return NULL;
    }
    if (!entry->revision || entry->mask_revision != mask.revision ||
        height != entry->height || !same_options(entry->options, options) ||
        entry->source.x != source.x || entry->source.y != source.y ||
        entry->source.radians != source.radians) {
        render_mesh_asset next = { 0 };
        material_surface_height sampler = height
                                            ? render_material_height_sampler(height)
                                            : (material_surface_height){ 0 };
        if (!material_surface_mesh_build_height(mask, options, source, height ? &sampler : NULL, &next, error, size)) {
            if (height != entry->height) {
                render_material_height_free(height);
            }
            return NULL;
        }
        render_mesh_surface_free(&entry->surface);
        render_mesh_asset_free(&entry->mesh);
        if (height != entry->height) {
            render_material_height_free(entry->height);
        }
        entry->height = height;
        entry->mesh = next;
        entry->options = options;
        entry->source = source;
        entry->mask_revision = mask.revision;
        entry->revision++;
        if (acount(next.indices)) {
            render_mesh_surface_part part = {
                .visual.part = { .primitive = PRIMITIVE_BOX, .dimensions = { 1, 1, 1 } },
                .vertices = next.vertices,
                .indices = next.indices,
                .corner_count = acount(next.indices),
                .dynamic_identity = mask.incarnation,
                .dynamic_revision = entry->revision,
                .height_consumed = options.material_height,
                .part_coordinates = next.has_part_coordinates,
                .uv_bounds = next.has_uv_bounds,
                .uv_min = next.uv_min,
                .uv_max = next.uv_max,
                .borrowed = true
            };
            apush(entry->surface.parts, part);
        }
    }
    place(entry, placement);
    return &entry->surface;
}
