#include "render/scene3d_terrain_volume.h"

#include "render/scene3d_terrain.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* The volume's share of the scene's indirect proxies, and the slab tiles'
 * share of that; rock beyond the carve's reach takes the rest. */
#define VOLUME_BOX_LIMIT (RENDER_TERRAIN_GI_PROXY_LIMIT / 2)
#define SLAB_BOX_LIMIT (VOLUME_BOX_LIMIT / 2)

/* Layers [z0, z1) of a chunk column, all on one side of the play plane. Its
 * surfaces are the span of its column's drawn mesh for that side; mesh holds
 * them only between re-meshing and the join. */
typedef struct
{
    render_mesh_asset mesh;
    int z0, z1;
    int vertex, vertices, index, indices;
    bool behind, dirty;
} volume_chunk;

/* A chunk column: one drawn mesh per side, ahead of and behind the plane. */
typedef struct
{
    render_mesh_asset drawn[2];
    render_scene3d_volume_chunk record[2];
    bool dirty, joined[2];
} volume_column;

/* Boxes for the slab the carve can reach under a square of chunk columns. */
typedef struct
{
    terrain_volume_box *boxes;
    bool dirty;
} volume_tile;

/* Plane samples [x0, x1) x [y0, y1). */
typedef struct
{
    int x0, y0, x1, y1;
} plane_rect;

typedef struct
{
    uint64_t identity, revision;
    float elevation;
    CF_V2 origin;
    int across, down;
    /* Each column's chunks, in column order. */
    int per_column;
    volume_chunk *chunks;
    volume_column *columns;
    /* The carve the meshes and boxes were built from; NULL before the first. */
    float *meshed;
    /* Slab box tiles of tile x tile columns, tiles_across per row, each
     * holding at most tile_limit boxes. */
    int tile, tiles_across, tile_limit;
    volume_tile *tiles;
    /* Boxes for rock beyond the carve's reach, fitted once, and every box. */
    terrain_volume_box *fixed, *boxes;
    render_scene3d_volume_chunk *views, *casters;
    bool used;
} volume_entry;

struct render_scene3d_terrain_volume
{
    volume_entry **entries;
    uint64_t next_revision;
    int rebuilds;
};

render_scene3d_terrain_volume *render_scene3d_terrain_volume_new()
{
    return xcalloc(1, sizeof(render_scene3d_terrain_volume));
}

static void free_entry(volume_entry *entry)
{
    for (int i = 0; i < acount(entry->chunks); i++) {
        render_mesh_asset_free(&entry->chunks[i].mesh);
    }
    for (int i = 0; i < acount(entry->columns); i++) {
        render_mesh_asset_free(&entry->columns[i].drawn[0]);
        render_mesh_asset_free(&entry->columns[i].drawn[1]);
    }
    for (int i = 0; i < acount(entry->tiles); i++) {
        afree(entry->tiles[i].boxes);
    }
    afree(entry->chunks);
    afree(entry->columns);
    afree(entry->tiles);
    free(entry->meshed);
    afree(entry->fixed);
    afree(entry->boxes);
    afree(entry->views);
    afree(entry->casters);
    free(entry);
}

void render_scene3d_terrain_volume_reset(render_scene3d_terrain_volume *volumes)
{
    for (int i = 0; i < acount(volumes->entries); i++) {
        free_entry(volumes->entries[i]);
    }
    aclear(volumes->entries);
    volumes->rebuilds = 0;
}

void render_scene3d_terrain_volume_free(render_scene3d_terrain_volume *volumes)
{
    render_scene3d_terrain_volume_reset(volumes);
    afree(volumes->entries);
    free(volumes);
}

void render_scene3d_terrain_volume_begin(render_scene3d_terrain_volume *volumes)
{
    volumes->rebuilds = 0;
    for (int i = 0; i < acount(volumes->entries); i++) {
        volumes->entries[i]->used = false;
    }
}

void render_scene3d_terrain_volume_end(render_scene3d_terrain_volume *volumes)
{
    for (int i = acount(volumes->entries) - 1; i >= 0; i--) {
        if (!volumes->entries[i]->used) {
            free_entry(volumes->entries[i]);
            adel(volumes->entries, i);
        }
    }
}

int render_scene3d_terrain_volume_rebuilds(const render_scene3d_terrain_volume *volumes)
{
    return volumes->rebuilds;
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t count)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < count; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

static int max_int(int a, int b)
{
    return a > b ? a : b;
}

/* Bricks per axis of the volume's coarse lattice. */
static int bricks(int samples, int refine)
{
    return (samples - 1) / refine;
}

/* Plane samples of the n x n chunk columns from column (cx, cy). */
static plane_rect chunk_samples(const terrain_volume *v, int cx, int cy, int n)
{
    int chunk = RENDER_TERRAIN_VOLUME_CHUNK;
    return (plane_rect){ cx * chunk,
                         cy * chunk,
                         min_int((cx + n) * chunk, v->plane.width),
                         min_int((cy + n) * chunk, v->plane.height) };
}

static plane_rect column_samples(const volume_entry *entry, const terrain_volume *v, int c)
{
    return chunk_samples(v, c % entry->across, c / entry->across, 1);
}

static plane_rect tile_samples(const volume_entry *entry, const terrain_volume *v, int t)
{
    int tile = entry->tile;
    return chunk_samples(v, t % entry->tiles_across * tile, t / entry->tiles_across * tile, tile);
}

/* Bricks under plane samples s through the slab the carve can reach; the
 * last row and column also take any bricks past the final chunk boundary. */
static terrain_volume_bricks slab_bricks(const terrain_volume *v, plane_rect s)
{
    int r = v->refine, reach = terrain_volume_reach(v);
    int bw = bricks(v->plane.width, r), bh = bricks(v->plane.height, r),
        bd = bricks(v->layers, r);
    return (terrain_volume_bricks){
        .x0 = s.x0 / r,
        .y0 = s.y0 / r,
        .z0 = max_int(v->front - reach, 0) / r,
        .x1 = s.x1 == v->plane.width ? bw : s.x1 / r,
        .y1 = s.y1 == v->plane.height ? bh : s.y1 / r,
        .z1 = min_int((v->front + reach + r) / r, bd),
    };
}

static int tiles_along(int columns, int tile)
{
    return (columns + tile - 1) / tile;
}

/* Chunks behind the plane from it to the deepest layer, then ahead of it to
 * the frontmost layer, for every column, and slab box tiles: squares of the
 * fewest columns that leave every tile at least one box. */
static void add_chunks(volume_entry *entry, const terrain_volume *v)
{
    int chunk = RENDER_TERRAIN_VOLUME_CHUNK;
    for (int c = 0; c < entry->across * entry->down; c++) {
        for (int z = v->front; z < v->layers; z += chunk) {
            apush(
                entry->chunks,
                ((volume_chunk){ .z0 = z,
                                 .z1 = min_int(z + chunk, v->layers),
                                 .behind = true,
                                 .dirty = true })
            );
        }
        for (int z = v->front; z > 0; z -= chunk) {
            apush(
                entry->chunks,
                ((volume_chunk){ .z0 = max_int(z - chunk, 0), .z1 = z, .dirty = true })
            );
        }
        apush(entry->columns, ((volume_column){ .dirty = true }));
    }
    entry->per_column = acount(entry->chunks) / acount(entry->columns);
    entry->tile = 1;
    while (tiles_along(entry->across, entry->tile) *
               tiles_along(entry->down, entry->tile) >
           SLAB_BOX_LIMIT) {
        entry->tile++;
    }
    entry->tiles_across = tiles_along(entry->across, entry->tile);
    int tiles = entry->tiles_across * tiles_along(entry->down, entry->tile);
    for (int t = 0; t < tiles; t++) {
        apush(entry->tiles, ((volume_tile){ .dirty = true }));
    }
    entry->tile_limit = SLAB_BOX_LIMIT / tiles;
}

/* Boxes for rock ahead of and behind the slab, which no carve reaches,
 * within what the slab tiles leave of the volume's budget. */
static void fit_fixed(volume_entry *entry, const terrain_volume *v, const float *removed)
{
    int r = v->refine;
    int limit = VOLUME_BOX_LIMIT - entry->tile_limit * acount(entry->tiles);
    terrain_volume_bricks slab = slab_bricks(v, chunk_samples(v, 0, 0, 1));
    terrain_volume_bricks all = { 0,
                                  0,
                                  0,
                                  bricks(v->plane.width, r),
                                  bricks(v->plane.height, r),
                                  bricks(v->layers, r) };
    terrain_volume_bricks ahead = all, behind = all;
    ahead.z1 = slab.z0;
    behind.z0 = slab.z1;
    int count = terrain_volume_boxes(
        v,
        removed,
        ahead,
        limit / 2,
        entry->elevation,
        &entry->fixed
    );
    terrain_volume_boxes(
        v,
        removed,
        behind,
        limit - count,
        entry->elevation,
        &entry->fixed
    );
}

/* Rebuilds column c's drawn mesh for one side from its new chunk meshes and
 * the unchanged spans of the old one, when any chunk there changed. */
static void join(volume_entry *entry, int c, bool behind)
{
    volume_column *column = &entry->columns[c];
    volume_chunk *chunks = entry->chunks + c * entry->per_column;
    column->joined[behind] = false;
    for (int i = 0; i < entry->per_column; i++) {
        column->joined[behind] |= chunks[i].behind == behind && chunks[i].dirty;
    }
    if (!column->joined[behind]) {
        return;
    }
    render_mesh_asset *old = &column->drawn[behind];
    render_mesh_asset joined = { .has_part_coordinates = true };
    for (int i = 0; i < entry->per_column; i++) {
        volume_chunk *chunk = &chunks[i];
        if (chunk->behind != behind) {
            continue;
        }
        const render_mesh_asset *from = chunk->dirty ? &chunk->mesh : old;
        int vertex = chunk->dirty ? 0 : chunk->vertex,
            vertices = chunk->dirty ? acount(chunk->mesh.vertices) : chunk->vertices,
            index = chunk->dirty ? 0 : chunk->index,
            indices = chunk->dirty ? acount(chunk->mesh.indices) : chunk->indices;
        int base = acount(joined.vertices), first = acount(joined.indices);
        asetlen(joined.vertices, base + vertices);
        if (vertices) {
            memcpy(
                joined.vertices + base,
                from->vertices + vertex,
                (size_t)vertices * sizeof *joined.vertices
            );
        }
        asetlen(joined.indices, first + indices);
        for (int n = 0; n < indices; n++) {
            joined.indices[first + n] = from->indices[index + n] - vertex + base;
        }
        chunk->vertex = base;
        chunk->vertices = vertices;
        chunk->index = first;
        chunk->indices = indices;
    }
    for (int i = 0; i < acount(joined.vertices); i++) {
        CF_V3 p = joined.vertices[i].position;
        joined.min = i ? cf_min_v3(joined.min, p) : p;
        joined.max = i ? cf_max_v3(joined.max, p) : p;
    }
    render_mesh_asset_free(old);
    *old = joined;
}

typedef enum
{
    STEP_MESH,
    STEP_JOIN
} rebuild_step;

typedef struct
{
    const terrain_volume *volume;
    const float *removed;
    volume_entry *entry;
    rebuild_step step;
    /* Meshing: chunk indices, then tile indices to refit. Joining: column
     * indices. */
    const int *work;
    int chunks;
} rebuild_job;

static void rebuild_range(int start, int end, uint32_t, void *user)
{
    rebuild_job *job = user;
    volume_entry *entry = job->entry;
    for (int n = start; n < end; n++) {
        int index = job->work[n];
        if (job->step == STEP_JOIN) {
            join(entry, index, false);
            join(entry, index, true);
            continue;
        }
        if (n >= job->chunks) {
            volume_tile *tile = &entry->tiles[index];
            aclear(tile->boxes);
            terrain_volume_boxes(
                job->volume,
                job->removed,
                slab_bricks(job->volume, tile_samples(entry, job->volume, index)),
                entry->tile_limit,
                entry->elevation,
                &tile->boxes
            );
            continue;
        }
        volume_chunk *slot = &entry->chunks[index];
        plane_rect s = column_samples(entry, job->volume, index / entry->per_column);
        terrain_volume_mesh(
            job->volume,
            job->removed,
            s.x0,
            s.y0,
            s.x1,
            s.y1,
            slot->z0,
            slot->z1,
            entry->elevation,
            entry->origin,
            &slot->mesh
        );
    }
}

static void run(task_pool *pool, rebuild_job *job, int count)
{
    if (!pool || count < 2) {
        rebuild_range(0, count, 0, job);
        return;
    }
    const task_system *tasks = task_pool_tasks(pool);
    task_pool_begin(pool, true);
    void *task = tasks->enqueue(rebuild_range, count, 1, job, tasks->user);
    tasks->finish(task, tasks->user);
    task_pool_end(pool);
}

/* Marks the chunks, columns and tiles a change from the meshed carve can
 * reshape. */
static void mark_changed(volume_entry *entry, const terrain_volume *v, const float *removed)
{
    int r = v->refine, k0, k1;
    for (int c = 0; c < acount(entry->columns); c++) {
        plane_rect s = column_samples(entry, v, c);
        if (!terrain_volume_changed_layers(v, entry->meshed, removed, s.x0, s.y0, s.x1, s.y1, &k0, &k1)) {
            continue;
        }
        entry->columns[c].dirty = true;
        volume_chunk *column = entry->chunks + c * entry->per_column;
        for (int i = 0; i < entry->per_column; i++) {
            column[i].dirty |= column[i].z0 < k1 && k0 < column[i].z1;
        }
    }
    for (int t = 0; t < acount(entry->tiles); t++) {
        plane_rect s = tile_samples(entry, v, t);
        terrain_volume_bricks b = slab_bricks(v, s);
        /* The boxes read every sample of their bricks, far faces too. */
        s.x0 = min_int(s.x0, b.x0 * r);
        s.y0 = min_int(s.y0, b.y0 * r);
        s.x1 = max_int(s.x1, min_int(b.x1 * r + 1, v->plane.width));
        s.y1 = max_int(s.y1, min_int(b.y1 * r + 1, v->plane.height));
        entry->tiles[t].dirty |= terrain_volume_changed_layers(
            v,
            entry->meshed,
            removed,
            s.x0,
            s.y0,
            s.x1,
            s.y1,
            &k0,
            &k1
        );
    }
}

static void rebuild(
    render_scene3d_terrain_volume *volumes,
    volume_entry *entry,
    materials_depth_volume view,
    task_pool *pool
)
{
    const terrain_volume *v = view.volume;
    size_t samples = (size_t)v->plane.width * v->plane.height;
    if (!entry->meshed) {
        entry->meshed = xmalloc(samples * sizeof *entry->meshed);
        fit_fixed(entry, v, view.removed);
    } else {
        mark_changed(entry, v, view.removed);
    }
    int *work = NULL, *joins = NULL;
    for (int i = 0; i < acount(entry->chunks); i++) {
        if (entry->chunks[i].dirty) {
            apush(work, i);
        }
    }
    int chunks = acount(work);
    for (int t = 0; t < acount(entry->tiles); t++) {
        if (entry->tiles[t].dirty) {
            apush(work, t);
        }
    }
    for (int c = 0; c < acount(entry->columns); c++) {
        if (entry->columns[c].dirty) {
            apush(joins, c);
        }
    }
    rebuild_job job = { v, view.removed, entry, STEP_MESH, work, chunks };
    run(pool, &job, acount(work));
    job.step = STEP_JOIN;
    job.work = joins;
    run(pool, &job, acount(joins));
    for (int n = 0; n < chunks; n++) {
        volume_chunk *slot = &entry->chunks[work[n]];
        render_mesh_asset_free(&slot->mesh);
        slot->dirty = false;
    }
    for (int n = 0; n < acount(joins); n++) {
        int c = joins[n];
        for (int side = 0; side < 2; side++) {
            if (!entry->columns[c].joined[side]) {
                continue;
            }
            uint64_t identity = hash_bytes(
                hash_bytes(
                    hash_bytes(
                        UINT64_C(14695981039346656037),
                        &view.identity,
                        sizeof view.identity
                    ),
                    &c,
                    sizeof c
                ),
                &side,
                sizeof side
            );
            entry->columns[c].record[side] = (render_scene3d_volume_chunk){
                .mesh = &entry->columns[c].drawn[side],
                .identity = identity,
                .revision = ++volumes->next_revision
            };
        }
        entry->columns[c].dirty = false;
    }
    for (int t = 0; t < acount(entry->tiles); t++) {
        entry->tiles[t].dirty = false;
    }
    volumes->rebuilds += chunks;
    afree(joins);
    afree(work);
    memcpy(entry->meshed, view.removed, samples * sizeof *entry->meshed);
    aclear(entry->views);
    aclear(entry->casters);
    aclear(entry->boxes);
    for (int i = 0; i < acount(entry->fixed); i++) {
        apush(entry->boxes, entry->fixed[i]);
    }
    for (int c = 0; c < acount(entry->columns); c++) {
        volume_column *column = &entry->columns[c];
        if (acount(column->drawn[1].indices)) {
            apush(entry->views, column->record[1]);
        }
        if (acount(column->drawn[0].indices)) {
            apush(entry->casters, column->record[0]);
        }
    }
    for (int t = 0; t < acount(entry->tiles); t++) {
        for (int i = 0; i < acount(entry->tiles[t].boxes); i++) {
            apush(entry->boxes, entry->tiles[t].boxes[i]);
        }
    }
    assert(acount(entry->boxes) <= VOLUME_BOX_LIMIT);
}

render_scene3d_volume_surface render_scene3d_terrain_volume_get(
    render_scene3d_terrain_volume *volumes,
    materials_depth_volume view,
    float elevation,
    CF_V2 origin,
    task_pool *pool
)
{
    volume_entry *entry = NULL;
    for (int i = 0; i < acount(volumes->entries) && !entry; i++) {
        if (volumes->entries[i]->identity == view.identity) {
            entry = volumes->entries[i];
        }
    }
    if (entry && (entry->elevation != elevation ||
                  entry->origin.x != origin.x || entry->origin.y != origin.y)) {
        for (int i = 0; i < acount(volumes->entries); i++) {
            if (volumes->entries[i] == entry) {
                adel(volumes->entries, i);
                break;
            }
        }
        free_entry(entry);
        entry = NULL;
    }
    if (!entry) {
        int chunk = RENDER_TERRAIN_VOLUME_CHUNK;
        entry = xcalloc(1, sizeof *entry);
        *entry = (volume_entry){
            .identity = view.identity,
            .elevation = elevation,
            .origin = origin,
            .across = (view.volume->plane.width + chunk - 1) / chunk,
            .down = (view.volume->plane.height + chunk - 1) / chunk,
        };
        add_chunks(entry, view.volume);
        apush(volumes->entries, entry);
    }
    entry->used = true;
    if (entry->revision != view.revision) {
        rebuild(volumes, entry, view, pool);
        entry->revision = view.revision;
    }
    return (render_scene3d_volume_surface){
        entry->views,           acount(entry->views), entry->casters,
        acount(entry->casters), entry->boxes,         acount(entry->boxes)
    };
}
