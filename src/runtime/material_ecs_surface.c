#include "runtime/material_ecs.h"
#include "runtime/transform_propagation.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

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

/* Marks plane samples where a tile of the source has open cells. Samples
 * outside every tile stay closed, so the carve never reaches past the mask. */
static void open_plane(
    materials *materials,
    ecs_entity source,
    const material_record *record,
    unsigned char *open
)
{
    const field_grid *plane = &record->depth_volume->plane;
    CF_V2 origin = transform_world_2d(materials->world, source).position;
    memset(open, 0, (size_t)plane->width * plane->height);
    for (int t = 0; t < acount(materials->records); t++) {
        const material_record *tile = &materials->records[t];
        if (!tile->mask || tile->fragment ||
            ecs_entity_to_u64(tile->terrain_source) != ecs_entity_to_u64(source) ||
            !ecs_entity_is_alive(materials->world, tile->entity)) {
            continue;
        }
        const material_mask *mask = tile->mask;
        CF_V2 at = transform_world_2d(materials->world, tile->entity).position;
        at = cf_sub_v2(at, origin);
        for (int j = 0; j < plane->height; j++) {
            float y = (j + .5f - plane->height * .5f) * plane->cell_size - at.y;
            int cy = (int)floorf(y / mask->cell_size + mask->height * .5f);
            if (cy < 0 || cy >= mask->height) {
                continue;
            }
            for (int i = 0; i < plane->width; i++) {
                float x = (i + .5f - plane->width * .5f) * plane->cell_size - at.x;
                int cx = (int)floorf(x / mask->cell_size + mask->width * .5f);
                if (cx >= 0 && cx < mask->width) {
                    open[j * plane->width + i] = !mask->cells[cy * mask->width + cx];
                }
            }
        }
    }
}

/* Recomputes each depth volume's carve where its open plane samples changed,
 * the whole plane the first time. */
static void sync_depth_volumes(materials *materials)
{
    for (int i = 0; i < acount(materials->records); i++) {
        material_record *record = &materials->records[i];
        if (!record->depth_volume ||
            !ecs_entity_is_alive(materials->world, record->entity)) {
            continue;
        }
        uint64_t hash = UINT64_C(14695981039346656037);
        for (int t = 0; t < acount(materials->records); t++) {
            const material_record *tile = &materials->records[t];
            if (tile->mask && !tile->fragment &&
                ecs_entity_to_u64(tile->terrain_source) ==
                    ecs_entity_to_u64(record->entity)) {
                hash = hash_bytes(
                    hash,
                    &tile->mask->incarnation,
                    sizeof tile->mask->incarnation
                );
                hash = hash_bytes(
                    hash,
                    &tile->mask->content_revision,
                    sizeof tile->mask->content_revision
                );
            }
        }
        if (hash == record->depth_hash) {
            continue;
        }
        record->depth_hash = hash;
        const field_grid *plane = &record->depth_volume->plane;
        unsigned char *open = xmalloc((size_t)plane->width * plane->height);
        open_plane(materials, record->entity, record, open);
        int x0 = 0, y0 = 0, x1 = plane->width, y1 = plane->height;
        if (record->depth_open) {
            x0 = plane->width;
            y0 = plane->height;
            x1 = y1 = 0;
            for (int y = 0; y < plane->height; y++) {
                for (int x = 0; x < plane->width; x++) {
                    size_t i = (size_t)y * plane->width + x;
                    if (open[i] != record->depth_open[i]) {
                        x0 = x < x0 ? x : x0;
                        y0 = y < y0 ? y : y0;
                        x1 = x + 1 > x1 ? x + 1 : x1;
                        y1 = y + 1;
                    }
                }
            }
        }
        free(record->depth_open);
        record->depth_open = open;
        if (x0 < x1) {
            terrain_volume_removed(
                record->depth_volume,
                open,
                x0,
                y0,
                x1,
                y1,
                record->depth_removed
            );
            record->depth_revision++;
        }
    }
}

static int border_radius(const ecs_world *world, ecs_entity entity, float cell)
{
    const Appearance *appearance = Appearance_get_const(world, entity);
    const MaterialSurface3D *surface = MaterialSurface3D_get_const(world, entity);
    float width = appearance ? appearance->border_width : 0;
    /* A surface recess measures occupancy beyond its contour, with a corner
     * margin for the contour samples just outside each chunk. */
    if (surface && surface->enabled && surface->recess_width > 0) {
        width = fmaxf(width, surface->recess_width + surface->recess_smoothing + 2 * cell);
    }
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
        int radius = border_radius(materials->world, record->entity, terrain->cell_size);
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
            int reach = border_radius(materials->world, tile->entity, terrain->cell_size);
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
    sync_depth_volumes(materials);
}
