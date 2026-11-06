#include "render/scene3d_terrain.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    render_scene3d_terrain_surface surface;
    CF_M4x4 *boxes;
    bool used, proxies_ready;
} terrain_entry;

struct render_scene3d_terrain
{
    render_material_mesh_cache *geometry;
    terrain_entry **entries;
    int rebuilds;
};

render_scene3d_terrain *render_scene3d_terrain_new()
{
    render_scene3d_terrain *terrain = xcalloc(1, sizeof *terrain);
    terrain->geometry = render_material_mesh_cache_new();
    return terrain;
}

void render_scene3d_terrain_reset(render_scene3d_terrain *terrain)
{
    for (int i = 0; i < acount(terrain->entries); i++) {
        afree(terrain->entries[i]->boxes);
        free(terrain->entries[i]);
    }
    aclear(terrain->entries);
    render_material_mesh_cache_clear(terrain->geometry, NULL);
    terrain->rebuilds = 0;
}

void render_scene3d_terrain_free(render_scene3d_terrain *terrain)
{
    render_scene3d_terrain_reset(terrain);
    render_material_mesh_cache_free(terrain->geometry, NULL);
    afree(terrain->entries);
    free(terrain);
}

void render_scene3d_terrain_begin(render_scene3d_terrain *terrain)
{
    terrain->rebuilds = 0;
    render_material_mesh_cache_begin(terrain->geometry);
    for (int i = 0; i < acount(terrain->entries); i++) {
        terrain->entries[i]->used = false;
    }
}

void render_scene3d_terrain_end(render_scene3d_terrain *terrain)
{
    for (int i = acount(terrain->entries) - 1; i >= 0; i--) {
        if (terrain->entries[i]->used) {
            continue;
        }
        terrain->rebuilds++;
        afree(terrain->entries[i]->boxes);
        free(terrain->entries[i]);
        adel(terrain->entries, i);
    }
    render_material_mesh_cache_end(terrain->geometry, NULL);
}

int render_scene3d_terrain_rebuilds(const render_scene3d_terrain *terrain)
{
    return terrain->rebuilds;
}

const render_scene3d_terrain_surface *render_scene3d_terrain_get(
    render_scene3d_terrain *terrain,
    const material_mask *mask,
    MaterialSurface3D options,
    MaterialFrame source,
    GlobalTransform2D placement,
    ecs_world *world,
    ecs_entity entity,
    bool indirect,
    char *error,
    size_t size
)
{
    material_collision_view collision = material_mask_collision(mask);
    if (indirect && collision.count > RENDER_TERRAIN_GI_PROXY_LIMIT) {
        snprintf(
            error,
            size,
            "Terrain indirect visibility needs %d occupied-cell rectangles; "
            "limit is %d (no opening-closing simplification)",
            collision.count,
            RENDER_TERRAIN_GI_PROXY_LIMIT
        );
        return NULL;
    }
    const render_mesh_surface *surface = render_material_mesh_cache_get(
        terrain->geometry,
        material_mask_occupancy(mask),
        options,
        source,
        placement,
        world,
        entity,
        error,
        size
    );
    if (!surface) {
        return NULL;
    }
    uint64_t revision;
    const render_mesh_asset *mesh = render_material_mesh_cache_asset(
        terrain->geometry,
        mask->incarnation,
        &revision
    );
    terrain_entry *entry = NULL;
    for (int i = 0; i < acount(terrain->entries); i++) {
        if (terrain->entries[i]->surface.identity == mask->incarnation) {
            entry = terrain->entries[i];
            break;
        }
    }
    if (!entry) {
        entry = xcalloc(1, sizeof *entry);
        entry->surface.identity = mask->incarnation;
        apush(terrain->entries, entry);
    }
    entry->used = true;
    if (entry->surface.revision != revision) {
        terrain->rebuilds++;
        aclear(entry->boxes);
        entry->proxies_ready = false;
    }
    if (indirect && !entry->proxies_ready && acount(mesh->indices)) {
        /* Published collision rectangles exactly partition surviving cells.
         * Extruding them keeps all 2D openings, at bounded shallow depth.
         * Relief is approximated vertically by this mesh's min/max slab. */
        for (int i = 0; i < collision.count; i++) {
            material_rect rect = collision.rects[i];
            CF_M4x4 box = cf_mul_m4(
                cf_m4_translate((CF_V3){ rect.x, (mesh->min.y + mesh->max.y) * .5f, rect.y }),
                cf_m4_scale((CF_V3){ rect.width, mesh->max.y - mesh->min.y, rect.height })
            );
            apush(entry->boxes, box);
        }
        entry->proxies_ready = true;
    }
    entry->surface.mesh = mesh;
    entry->surface.revision = revision;
    entry->surface.frame = acount(surface->parts) ? surface->parts[0].visual.frame
                                                  : cf_m4_identity();
    entry->surface.proxies = entry->boxes;
    entry->surface.proxy_count = indirect ? acount(entry->boxes) : 0;
    return &entry->surface;
}
