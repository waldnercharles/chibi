#include "runtime/material_ecs.h"
#include "runtime/transform_propagation.h"
#include "support/array.h"

#include <math.h>

void material_ecs_queue_terrain_cut(void *user, const material_damage *damage)
{
    materials *materials = user;
    ecs_entity entity = ecs_entity_from_u64(damage->target);
    material_record *record = &materials->records[entity.slot];
    if (record->fragment || !ecs_entity_to_u64(record->terrain_source)) {
        return;
    }
    material_damage cut = *damage;
    cut.target = ecs_entity_to_u64(record->terrain_source);
    for (int i = 0; i < acount(materials->terrain_cuts); i++) {
        material_damage previous = materials->terrain_cuts[i];
        if (previous.target == cut.target && previous.point.x == cut.point.x &&
            previous.point.y == cut.point.y && previous.radius == cut.radius) {
            return;
        }
    }
    apush(materials->terrain_cuts, cut);
}

static void cut_mask(material_mask *mask, float x, float y, material_damage cut)
{
    /* The kernel includes the one-cell ring outside the disk. Those positive
     * samples affect contours even when this chunk lost no occupied cell. */
    if (material_mask_cut_field(mask, cut.point.x - x, cut.point.y - y, cut.radius)) {
        material_mask_rebuild(mask);
    }
}

void material_ecs_apply_terrain_cuts(materials *materials)
{
    for (int i = 0; i < acount(materials->terrain_cuts); i++) {
        material_damage cut = materials->terrain_cuts[i];
        ecs_entity source = ecs_entity_from_u64(cut.target);
        if (!ecs_entity_is_alive(materials->world, source)) {
            continue;
        }
        material_record *record = &materials->records[source.slot];
        if (record->stream) {
            CF_V2 origin = transform_world_2d(materials->world, source).position;
            int count;
            const material_stream_tile *tiles = material_stream_tiles(record->stream, &count);
            for (int j = 0; j < count; j++) {
                cut_mask(
                    tiles[j].mask,
                    origin.x + tiles[j].grid.center_x,
                    origin.y + tiles[j].grid.center_y,
                    cut
                );
            }
            continue;
        }
        for (int j = 0; j < acount(materials->records); j++) {
            material_record *tile = &materials->records[j];
            if (!tile->mask || tile->fragment ||
                ecs_entity_to_u64(tile->terrain_source) != cut.target ||
                !ecs_entity_is_alive(materials->world, tile->entity)) {
                continue;
            }
            CF_V2 position = transform_world_2d(materials->world, tile->entity).position;
            cut_mask(tile->mask, position.x, position.y, cut);
        }
    }
    aclear(materials->terrain_cuts);
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t count)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < count; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t neighborhood_hash(const material_terrain_view *views, int count, int radius, int lod)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    hash = hash_bytes(hash, &count, sizeof count);
    hash = hash_bytes(hash, &radius, sizeof radius);
    hash = hash_bytes(hash, &lod, sizeof lod);
    for (int i = 0; i < count; i++) {
        const material_mask *mask = views[i].mask;
        hash = hash_bytes(hash, &views[i].x, sizeof views[i].x);
        hash = hash_bytes(hash, &views[i].y, sizeof views[i].y);
        hash = hash_bytes(hash, &mask->incarnation, sizeof mask->incarnation);
        hash = hash_bytes(hash, &mask->revision, sizeof mask->revision);
    }
    return hash;
}

static int border_radius(const Appearance *appearance, float cell)
{
    float width = appearance ? appearance->border_width : 0;
    return (int)ceilf(fmaxf(32, fminf(MATERIAL_HALO_RADIUS_MAX, width / cell)));
}

static int dimension_lod(int cells)
{
    if (!cells) {
        return 5;
    }
    int level = 0;
    while (level < 5 && !(cells & 1)) {
        cells /= 2;
        level++;
    }
    return level;
}

void material_ecs_sync_terrain_surfaces(materials *materials)
{
    for (int source = 0; source < acount(materials->records); source++) {
        material_record *record = &materials->records[source];
        if (!record->terrain || !ecs_entity_is_alive(materials->world, record->entity)) {
            continue;
        }
        const MaterialTerrain
            *terrain = MaterialTerrain_get_const(materials->world, record->entity);
        if (!terrain) {
            continue;
        }
        aclear(materials->surface_views);
        int radius = border_radius(
            Appearance_get_const(materials->world, record->entity),
            terrain->cell_size
        );
        if (record->stream) {
            int count;
            const material_stream_tile *tiles = material_stream_tiles(record->stream, &count);
            for (int i = 0; i < count; i++) {
                apush(
                    materials->surface_views,
                    ((material_terrain_view){ tiles[i].mask,
                                              tiles[i].grid.center_x,
                                              tiles[i].grid.center_y })
                );
            }
        }
        for (int i = 0; i < acount(materials->records); i++) {
            material_record *tile = &materials->records[i];
            if (!tile->mask || tile->fragment ||
                ecs_entity_to_u64(tile->terrain_source) !=
                    ecs_entity_to_u64(record->entity) ||
                !ecs_entity_is_alive(materials->world, tile->entity)) {
                continue;
            }
            int reach = border_radius(
                Appearance_get_const(materials->world, tile->entity),
                terrain->cell_size
            );
            if (reach > radius) {
                radius = reach;
            }
            if (!record->stream) {
                const Transform *position = Transform_get_const(materials->world, tile->entity);
                apush(
                    materials->surface_views,
                    ((material_terrain_view){ tile->mask, position->x, position->y })
                );
            }
        }
        int width = (int)ceilf(terrain->width / terrain->cell_size);
        int height = (int)ceilf(terrain->height / terrain->cell_size);
        int x_lod = dimension_lod(width), y_lod = dimension_lod(height);
        int lod = x_lod < y_lod ? x_lod : y_lod;
        int count = acount(materials->surface_views);
        uint64_t hash = neighborhood_hash(materials->surface_views, count, radius, lod);
        if (hash == record->terrain_surface_hash) {
            continue;
        }
        material_terrain_sync_views(materials->surface_views, count, radius, lod);
        /* Halo updates advance revisions too; retain their published values so
         * an unchanged preparation does not resample or allocate again. */
        record->terrain_surface_hash = neighborhood_hash(materials->surface_views, count, radius, lod);
    }
    aclear(materials->surface_views);
}
