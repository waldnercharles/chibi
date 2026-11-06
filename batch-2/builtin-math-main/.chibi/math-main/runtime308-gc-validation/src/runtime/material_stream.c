#include "runtime/material_stream.h"

#include "diagnostics/tracy.h"
#include "runtime/material_checkpoint.h"
#include "runtime/material_terrain.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    /* Rows of a prefetched tile each unit of budget bakes per update. */
    STREAM_SLICE_ROWS = 16
};

static char *copy_text(arena *storage, const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = arena_alloc(storage, size);
    memcpy(copy, text, size);
    return copy;
}

typedef struct
{
    uint64_t key;
    long offset;
} stored_tile;
/* A prefetched tile baked a slice of rows per update; NULL samples when none. */
typedef struct
{
    uint64_t key;
    field_grid grid;
    float *samples;
    int baked_rows;
} pending_tile;
struct material_stream
{
    material_stream_desc desc;
    arena argument_storage;
    material_stream_tile *tiles;
    pending_tile pending;
    material_field_session *field_session;
    stored_tile *stored;
    FILE *file;
    int columns, rows;
    material_stream_state state;
    bool waiting;
};

material_stream *material_stream_new(material_stream_desc desc)
{
    assert(desc.grid.cell_size > 0);
    assert(
        (desc.grid.width == 0 && desc.grid.height == 0) ||
        (desc.grid.width > 0 && desc.grid.height > 0)
    );
    material_stream *s = xcalloc(1, sizeof *s);
    s->desc = desc;
    s->desc.field = xstrdup(desc.field);
    assert(desc.context.parameter_count >= 0 && desc.context.parameter_count <= 4096);
    assert(!desc.context.parameter_count || desc.context.parameters);
    field_parameter *parameters = arena_alloc(
        &s->argument_storage,
        (size_t)desc.context.parameter_count * sizeof *parameters
    );
    for (int i = 0; i < desc.context.parameter_count; i++) {
        parameters[i] = desc.context.parameters[i];
        assert(parameters[i].name && strlen(parameters[i].name) <= FIELD_PARAMETER_NAME_MAX);
        assert(
            !parameters[i].value.type_name ||
            strlen(parameters[i].value.type_name) <= FIELD_PARAMETER_NAME_MAX
        );
        assert(parameters[i].value.container == PIN_CONTAINER_SINGLE);
        assert(
            parameters[i].value.type == PIN_F32 ||
            parameters[i].value.type == PIN_I32 || parameters[i].value.type == PIN_U32
        );
        parameters[i].name = copy_text(&s->argument_storage, parameters[i].name);
        if (parameters[i].value.type_name) {
            parameters[i].value.type_name = copy_text(
                &s->argument_storage,
                parameters[i].value.type_name
            );
        }
    }
    s->desc.context.parameters = parameters;
    s->columns = (desc.grid.width + MATERIAL_TERRAIN_CHUNK_CELLS - 1) /
                 MATERIAL_TERRAIN_CHUNK_CELLS;
    s->rows = (desc.grid.height + MATERIAL_TERRAIN_CHUNK_CELLS - 1) /
              MATERIAL_TERRAIN_CHUNK_CELLS;
    return s;
}

static void free_tile(material_stream_tile tile)
{
    material_mask_free(tile.mask);
    free(tile.mask);
}

void material_stream_free(material_stream *s)
{
    for (int i = 0; i < acount(s->tiles); i++) {
        free_tile(s->tiles[i]);
    }
    afree(s->tiles);
    free(s->pending.samples);
    material_field_session_free(s->field_session);
    afree(s->stored);
    if (s->file) {
        fclose(s->file);
    }
    free((char *)s->desc.field);
    arena_free(&s->argument_storage);
    free(s);
}

bool material_stream_tile_grid(field_grid grid, uint64_t key, field_grid *tile)
{
    int columns = (grid.width + MATERIAL_TERRAIN_CHUNK_CELLS - 1) /
                  MATERIAL_TERRAIN_CHUNK_CELLS;
    if (!columns) {
        int x = (int32_t)(uint32_t)key;
        int y = (int32_t)(uint32_t)(key >> 32);
        float span = grid.cell_size * MATERIAL_TERRAIN_CHUNK_CELLS;
        *tile = (field_grid){ MATERIAL_TERRAIN_CHUNK_CELLS,
                              MATERIAL_TERRAIN_CHUNK_CELLS,
                              grid.cell_size,
                              grid.center_x + (x + .5f) * span,
                              grid.center_y + (y + .5f) * span,
                              0 };
        return isfinite(tile->center_x) && isfinite(tile->center_y);
    }
    int rows = (grid.height + MATERIAL_TERRAIN_CHUNK_CELLS - 1) /
               MATERIAL_TERRAIN_CHUNK_CELLS;
    if (key >= (uint64_t)columns * rows) {
        return false;
    }
    int x = (int)(key % columns) * MATERIAL_TERRAIN_CHUNK_CELLS;
    int y = (int)(key / columns) * MATERIAL_TERRAIN_CHUNK_CELLS;
    int w = grid.width - x < MATERIAL_TERRAIN_CHUNK_CELLS
              ? grid.width - x
              : MATERIAL_TERRAIN_CHUNK_CELLS;
    int h = grid.height - y < MATERIAL_TERRAIN_CHUNK_CELLS
              ? grid.height - y
              : MATERIAL_TERRAIN_CHUNK_CELLS;
    *tile = (field_grid){ w,
                          h,
                          grid.cell_size,
                          grid.center_x + (x + w * .5f - grid.width * .5f) * grid.cell_size,
                          grid.center_y + (y + h * .5f - grid.height * .5f) * grid.cell_size,
                          0 };
    return true;
}
static field_grid tile_grid(const material_stream *s, uint64_t key)
{
    field_grid grid;
    bool ok = material_stream_tile_grid(s->desc.grid, key, &grid);
    assert(ok);
    (void)ok;
    return grid;
}

static float view_distance(field_grid grid, material_stream_view view)
{
    float dx = fmaxf(0, fabsf(grid.center_x - view.x) - grid.width * grid.cell_size * .5f);
    float dy = fmaxf(0, fabsf(grid.center_y - view.y) - grid.height * grid.cell_size * .5f);
    return fmaxf(dx, dy) - view.radius;
}

static bool wanted(field_grid grid, const material_stream_view *views, int count, float margin)
{
    for (int i = 0; i < count; i++) {
        if (view_distance(grid, views[i]) <= margin) {
            return true;
        }
    }
    return false;
}

static int stored_index(const material_stream *s, uint64_t key)
{
    for (int i = 0; i < acount(s->stored); i++) {
        if (s->stored[i].key == key) {
            return i;
        }
    }
    return -1;
}

static bool store_tile(material_stream *s, material_stream_tile tile)
{
    if (!s->file) {
        s->file = tmpfile();
        if (!s->file) {
            return false;
        }
    }
    int index = stored_index(s, tile.key);
    bool append = index < 0;
    if (fseek(s->file, append ? 0 : s->stored[index].offset, append ? SEEK_END : SEEK_SET)) {
        return false;
    }
    long offset = ftell(s->file);
    if (offset < 0) {
        return false;
    }
    material_mask *m = tile.mask;
    int cells = tile.grid.width * tile.grid.height;
    /* Fixed record size permits rewriting a tile without growing the file.
     * Preserve partial damage even when no occupancy cell has changed yet. */
    float *data = xcalloc((size_t)cells * 2, sizeof(float));
    unsigned char *occupancy = xcalloc((size_t)cells, 1);
    for (int i = 0; i < cells; i++) {
        data[i] = tile.grid.cell_size * .5f;
    }
    if (m->width) {
        memcpy(occupancy, m->cells, (size_t)cells);
        memcpy(data, m->distance, (size_t)cells * sizeof(float));
        if (m->damage) {
            memcpy(data + cells, m->damage, (size_t)cells * sizeof(float));
        }
    }
    bool ok = fwrite(occupancy, 1, (size_t)cells, s->file) == (size_t)cells &&
              fwrite(data, sizeof(float), (size_t)cells * 2, s->file) ==
                  (size_t)cells * 2;
    free(occupancy);
    free(data);
    if (ok && append) {
        apush(s->stored, ((stored_tile){ tile.key, offset }));
        s->state.stored_bytes += (size_t)cells * (1 + 2 * sizeof(float));
    }
    return ok;
}

static void add_tile(material_stream *s, uint64_t key, field_grid grid, material_mask mask)
{
    material_mask *owned = xmalloc(sizeof *owned);
    *owned = mask;
    apush(s->tiles, ((material_stream_tile){ key, grid, owned }));
    s->state.loaded++;
}

static pending_tile start_tile(const material_stream *s, uint64_t key)
{
    field_grid grid = tile_grid(s, key);
    return (
        pending_tile
    ){ key, grid, xmalloc((size_t)grid.width * grid.height * sizeof(float)), 0 };
}

static bool bake_rows(
    material_stream *s,
    const material_fields *fields,
    pending_tile *tile,
    int count,
    char *error,
    size_t size
)
{
    if (!s->field_session) {
        s->field_session = material_field_session_new(
            fields,
            s->desc.field,
            s->desc.context,
            s->desc.tasks,
            error,
            size
        );
        if (!s->field_session) {
            return false;
        }
    }
    bool ok = material_field_session_bake_rows(
        s->field_session,
        fields,
        tile->grid,
        tile->baked_rows,
        count,
        tile->samples,
        s->desc.tasks,
        error,
        size
    );
    if (ok) {
        tile->baked_rows += count;
    }
    return ok;
}

/* A fully baked tile becomes resident. */
static void finish_tile(material_stream *s, pending_tile *tile)
{
    field_grid grid = tile->grid;
    material_terrain_tidy(tile->samples, grid, s->desc.island_cells);
    add_tile(
        s,
        tile->key,
        grid,
        material_mask_from_field(grid.width, grid.height, grid.cell_size, tile->samples)
    );
    free(tile->samples);
    *tile = (pending_tile){ 0 };
}

static bool restore_tile(material_stream *s, uint64_t key, int index, char *error, size_t size)
{
    field_grid grid = tile_grid(s, key);
    int cells = grid.width * grid.height;
    float *samples = xmalloc((size_t)cells * sizeof(float));
    unsigned char *occupancy = xmalloc((size_t)cells);
    float *damage = xmalloc((size_t)cells * sizeof(float));
    bool ok = !fseek(s->file, s->stored[index].offset, SEEK_SET) &&
              fread(occupancy, 1, (size_t)cells, s->file) == (size_t)cells &&
              fread(samples, sizeof(float), (size_t)cells, s->file) == (size_t)cells &&
              fread(damage, sizeof(float), (size_t)cells, s->file) == (size_t)cells;
    if (ok) {
        material_mask mask = material_mask_from_field(grid.width, grid.height, grid.cell_size, samples);
        memcpy(mask.cells, occupancy, (size_t)cells);
        mask.count = 0;
        for (int i = 0; i < cells; i++) {
            mask.count += occupancy[i] != 0;
        }
        mask.damage = damage;
        material_mask_rebuild(&mask);
        add_tile(s, key, grid, mask);
    } else {
        free(damage);
        snprintf(error, size, "Cannot restore streamed terrain");
    }
    free(occupancy);
    free(samples);
    return ok;
}

/* Restores a stored tile, or bakes whatever rows of it remain. */
static bool load_tile(material_stream *s, const material_fields *fields, uint64_t key, char *error, size_t size)
{
    int index = stored_index(s, key);
    if (index >= 0) {
        return restore_tile(s, key, index, error, size);
    }
    bool pending = s->pending.samples && s->pending.key == key;
    pending_tile whole = pending ? (pending_tile){ 0 } : start_tile(s, key);
    pending_tile *tile = pending ? &s->pending : &whole;
    if (!bake_rows(s, fields, tile, tile->grid.height - tile->baked_rows, error, size)) {
        free(whole.samples);
        return false;
    }
    finish_tile(s, tile);
    return true;
}

static bool resident(const material_stream *s, uint64_t key)
{
    for (int i = 0; i < acount(s->tiles); i++) {
        if (s->tiles[i].key == key) {
            return true;
        }
    }
    return false;
}

static bool next_tile(
    const material_stream *s,
    const material_stream_view *views,
    int count,
    bool required,
    uint64_t *key
)
{
    float best = FLT_MAX;
    bool found = false;
    field_grid grid = s->desc.grid;
    float span = grid.cell_size * MATERIAL_TERRAIN_CHUNK_CELLS;
    float left = grid.center_x - grid.width * grid.cell_size * .5f;
    float top = grid.center_y - grid.height * grid.cell_size * .5f;
    for (int v = 0; v < count; v++) {
        material_stream_view view = views[v];
        if (required) {
            if (view.required <= 0) {
                continue;
            }
            view.radius = view.required;
        }
        int x0 = (int)floorf((view.x - view.radius - left) / span);
        int y0 = (int)floorf((view.y - view.radius - top) / span);
        int x1 = (int)floorf((view.x + view.radius - left) / span);
        int y1 = (int)floorf((view.y + view.radius - top) / span);
        if (s->columns) {
            x0 = x0 < 0 ? 0 : x0;
            y0 = y0 < 0 ? 0 : y0;
            x1 = x1 >= s->columns ? s->columns - 1 : x1;
            y1 = y1 >= s->rows ? s->rows - 1 : y1;
        }
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                uint64_t candidate = s->columns
                                       ? (uint64_t)y * s->columns + x
                                       : (uint64_t)(uint32_t)y << 32 | (uint32_t)x;
                if (resident(s, candidate)) {
                    continue;
                }
                field_grid g = tile_grid(s, candidate);
                float distance = hypotf(g.center_x - view.x, g.center_y - view.y);
                if (distance < best || (distance == best && candidate < *key)) {
                    best = distance;
                    *key = candidate;
                    found = true;
                }
            }
        }
    }
    return found;
}

/* Spends budget slices of rows on the pending tile, starting on the nearest
 * missing tile when none is pending. Restoring a stored tile costs a slice. */
static bool prefetch(
    material_stream *s,
    const material_fields *fields,
    const material_stream_view *views,
    int count,
    int budget,
    char *error,
    size_t size
)
{
    pending_tile *tile = &s->pending;
    for (int rows = budget * STREAM_SLICE_ROWS; rows > 0;) {
        uint64_t key = 0;
        if (!tile->samples && !next_tile(s, views, count, false, &key)) {
            return true;
        }
        int index = tile->samples ? -1 : stored_index(s, key);
        if (index >= 0) {
            if (!restore_tile(s, key, index, error, size)) {
                return false;
            }
            rows -= STREAM_SLICE_ROWS;
            continue;
        }
        if (!tile->samples) {
            *tile = start_tile(s, key);
        }
        int left = tile->grid.height - tile->baked_rows;
        int slice = left < rows ? left : rows;
        if (!bake_rows(s, fields, tile, slice, error, size)) {
            return false;
        }
        rows -= slice;
        if (tile->baked_rows == tile->grid.height) {
            finish_tile(s, tile);
        }
    }
    return true;
}

bool material_stream_update(
    material_stream *s,
    const material_fields *fields,
    const material_stream_view *views,
    int count,
    int budget,
    char *error,
    size_t size
)
{
    assert(count >= 0 && budget > 0);
    if (s->field_session && !material_field_session_matches(s->field_session, fields)) {
        snprintf(error, size, "Procedural field changed; recreate material stream");
        return false;
    }
    float margin = s->desc.grid.cell_size * MATERIAL_TERRAIN_CHUNK_CELLS;
    for (int i = acount(s->tiles) - 1; i >= 0; i--) {
        material_stream_tile tile = s->tiles[i];
        if (wanted(tile.grid, views, count, margin)) {
            continue;
        }
        if (!store_tile(s, tile)) {
            snprintf(error, size, "Cannot preserve streamed terrain");
            return false;
        }
        free_tile(tile);
        adel(s->tiles, i);
        s->state.unloaded++;
    }
    /* A pending bake the views have left is abandoned. */
    if (s->pending.samples && !wanted(s->pending.grid, views, count, margin)) {
        free(s->pending.samples);
        s->pending = (pending_tile){ 0 };
    }
    uint64_t key = 0;
    bool loaded = true;
    CHIBI_TRACE_ZONE_BEGIN(required_zone, "Terrain required tiles");
    while (loaded && next_tile(s, views, count, true, &key)) {
        loaded = load_tile(s, fields, key, error, size);
    }
    CHIBI_TRACE_ZONE_END(required_zone);
    CHIBI_TRACE_ZONE_BEGIN(prefetch_zone, "Terrain prefetch");
    loaded = loaded && prefetch(s, fields, views, count, budget, error, size);
    CHIBI_TRACE_ZONE_END(prefetch_zone);
    s->waiting = s->pending.samples || next_tile(s, views, count, false, &key);
    return loaded;
}

bool material_stream_waiting(const material_stream *s)
{
    return s->waiting;
}

const material_stream_tile *material_stream_tiles(const material_stream *s, int *count)
{
    *count = acount(s->tiles);
    return s->tiles;
}

material_stream_state material_stream_inspect(const material_stream *s)
{
    material_stream_state state = s->state;
    state.resident = acount(s->tiles);
    state.stored = acount(s->stored);
    for (int i = 0; i < acount(s->tiles); i++) {
        material_mask *m = s->tiles[i].mask;
        state.resident_bytes += sizeof *m +
                                (size_t)m->width * m->height *
                                    (1 + sizeof(float) * (m->damage ? 2 : 1)) +
                                (size_t)acount(m->rects) * sizeof(material_rect) +
                                (size_t)acount(m->surface) * sizeof(material_polygon);
    }
    return state;
}

bool material_stream_checkpoint(material_stream *s, material_checkpoint *out, char *error, size_t size)
{
    material_checkpoint result = { 0 };
    int total = acount(s->tiles);
    for (int i = 0; i < acount(s->stored); i++) {
        total += !resident(s, s->stored[i].key);
    }
    if (total > MATERIAL_CHECKPOINT_MAX_TILES) {
        goto fail;
    }
    for (int i = 0; i < acount(s->stored); i++) {
        uint64_t key = s->stored[i].key;
        if (resident(s, key)) {
            continue;
        }
        field_grid grid = tile_grid(s, key);
        size_t cells = (size_t)grid.width * grid.height;
        material_mask *mask = material_checkpoint_copy_mask(NULL, grid);
        apush(result.tiles, ((material_stream_tile){ key, grid, mask }));
        if (fseek(s->file, s->stored[i].offset, SEEK_SET) ||
            fread(mask->cells, 1, cells, s->file) != cells ||
            fread(mask->distance, sizeof(float), cells, s->file) != cells ||
            fread(mask->damage, sizeof(float), cells, s->file) != cells) {
            goto fail;
        }
        for (size_t j = 0; j < cells; j++) {
            mask->count += mask->cells[j] != 0;
        }
    }
    for (int i = 0; i < acount(s->tiles); i++) {
        material_stream_tile tile = s->tiles[i];
        tile.mask = material_checkpoint_copy_mask(tile.mask, tile.grid);
        apush(result.tiles, tile);
    }
    *out = result;
    return true;
fail:
    material_checkpoint_free(&result);
    snprintf(error, size, "Cannot snapshot streamed terrain or tile limit exceeded");
    return false;
}
bool material_stream_restore_checkpoint(
    material_stream *s,
    const material_checkpoint *checkpoint,
    char *error,
    size_t size
)
{
    assert(!acount(s->tiles) && !acount(s->stored));
    for (int i = 0; i < acount(checkpoint->tiles); i++) {
        material_stream_tile tile = checkpoint->tiles[i];
        field_grid grid;
        if (!material_stream_tile_grid(s->desc.grid, tile.key, &grid) ||
            grid.width != tile.grid.width || grid.height != tile.grid.height ||
            grid.cell_size != tile.grid.cell_size || grid.center_x != tile.grid.center_x ||
            grid.center_y != tile.grid.center_y || !store_tile(s, tile)) {
            snprintf(error, size, "Cannot stage terrain checkpoint tiles");
            return false;
        }
    }
    return true;
}
