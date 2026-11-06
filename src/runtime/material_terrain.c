#include "runtime/material_terrain.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

void material_terrain_connect(float *field, field_grid grid, int connectivity, int border)
{
    int width = grid.width, height = grid.height, start = -1;
    unsigned char *visited = xcalloc((size_t)width * height, 1);
    /* Column order preserves the authored cave's first opening. Empty and solid
     * fields are valid and need no flood fill. */
    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            int at = y * width + x;
            if (x < border || y < border || x >= width - border || y >= height - border) {
                field[at] = -grid.cell_size;
            }
            if (field[at] >= 0 && start < 0) {
                start = at;
            }
        }
    }
    if (connectivity == MATERIAL_CONNECTIVITY_ALL || start < 0) {
        free(visited);
        return;
    }
    assert(connectivity == MATERIAL_CONNECTIVITY_FIRST_AIR);
    int *queue = xmalloc((size_t)width * height * sizeof(int));
    int end = 1;
    queue[0] = start;
    visited[start] = 1;
    for (int head = 0; head < end; head++) {
        int at = queue[head];
        int neighbors[] = { at - 1, at + 1, at - width, at + width };
        for (int i = 0; i < 4; i++) {
            int next = neighbors[i];
            if (next < 0 || next >= width * height || (i == 0 && at % width == 0) ||
                (i == 1 && at % width == width - 1) || visited[next] ||
                field[next] < 0) {
                continue;
            }
            visited[next] = 1;
            queue[end++] = next;
        }
    }
    for (int i = 0; i < width * height; i++) {
        if (!visited[i] && field[i] >= 0) {
            field[i] = -grid.cell_size;
        }
    }
    free(queue);
    free(visited);
}

void material_terrain_tidy(float *field, field_grid grid, int island_cells)
{
    if (island_cells <= 0) {
        return;
    }
    int width = grid.width, height = grid.height, count = width * height;
    unsigned char *visited = xcalloc((size_t)count, 1);
    int *queue = xmalloc((size_t)count * sizeof(int));
    for (int start = 0; start < count; start++) {
        if (visited[start]) {
            continue;
        }
        /* Flood one component of equal occupancy; solids join diagonally so a
         * corner contact keeps a piece attached. */
        bool solid = field[start] < 0, edge = false;
        int end = 1;
        queue[0] = start;
        visited[start] = 1;
        for (int head = 0; head < end; head++) {
            int at = queue[head], x = at % width, y = at / width;
            edge |= x == 0 || y == 0 || x == width - 1 || y == height - 1;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy, next = ny * width + nx;
                    if ((dx == 0 && dy == 0) || (!solid && dx != 0 && dy != 0) ||
                        nx < 0 || ny < 0 || nx >= width || ny >= height ||
                        visited[next] || (field[next] < 0) != solid) {
                        continue;
                    }
                    visited[next] = 1;
                    queue[end++] = next;
                }
            }
        }
        if (edge || end >= island_cells) {
            continue;
        }
        for (int i = 0; i < end; i++) {
            field[queue[i]] = solid ? grid.cell_size : -grid.cell_size;
        }
    }
    free(queue);
    free(visited);
}

material_terrain_chunk *material_terrain_chunks(const float *field, field_grid grid)
{
    material_terrain_chunk *chunks = NULL;
    float *samples = xmalloc(
        MATERIAL_TERRAIN_CHUNK_CELLS * MATERIAL_TERRAIN_CHUNK_CELLS * sizeof(float)
    );
    for (int y = 0; y < grid.height; y += MATERIAL_TERRAIN_CHUNK_CELLS) {
        for (int x = 0; x < grid.width; x += MATERIAL_TERRAIN_CHUNK_CELLS) {
            int w = grid.width - x < MATERIAL_TERRAIN_CHUNK_CELLS
                      ? grid.width - x
                      : MATERIAL_TERRAIN_CHUNK_CELLS;
            int h = grid.height - y < MATERIAL_TERRAIN_CHUNK_CELLS
                      ? grid.height - y
                      : MATERIAL_TERRAIN_CHUNK_CELLS;
            for (int row = 0; row < h; row++) {
                memcpy(samples + row * w, field + (y + row) * grid.width + x, (size_t)w * sizeof(float));
            }
            material_terrain_chunk chunk = {
                .mask = material_mask_from_field(w, h, grid.cell_size, samples),
                .x = grid.center_x + (x + w * .5f - grid.width * .5f) * grid.cell_size,
                .y = grid.center_y + (y + h * .5f - grid.height * .5f) * grid.cell_size
            };
            apush(chunks, chunk);
        }
    }
    free(samples);
    return chunks;
}
void material_terrain_chunks_free(material_terrain_chunk *chunks)
{
    for (int i = 0; i < acount(chunks); i++) {
        material_mask_free(&chunks[i].mask);
    }
    afree(chunks);
}

static uint64_t halo_hash(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}
void material_terrain_sync_views(const material_terrain_view *views, int count, int radius, int lod_limit)
{
    assert(count >= 0 && (!count || views));
    assert(radius > 0 && radius <= MATERIAL_HALO_RADIUS_MAX);
    int side = MATERIAL_TERRAIN_CHUNK_CELLS + 2 * radius;
    unsigned char *cells = NULL;
    float *distance = NULL;
    int *neighbors = NULL;
    for (int i = 0; i < count; i++) {
        material_mask *mask = views[i].mask;
        assert(
            mask && mask->width <= MATERIAL_TERRAIN_CHUNK_CELLS &&
            mask->height <= MATERIAL_TERRAIN_CHUNK_CELLS
        );
        int width = mask->width + 2 * radius, height = mask->height + 2 * radius;
        double reach_x = (mask->width * .5 + radius) * mask->cell_size;
        double reach_y = (mask->height * .5 + radius) * mask->cell_size;
        uint64_t signature = UINT64_C(1469598103934665603);
        signature = halo_hash(signature, &radius, sizeof radius);
        signature = halo_hash(signature, &lod_limit, sizeof lod_limit);
        signature = halo_hash(signature, &views[i].x, sizeof views[i].x);
        signature = halo_hash(signature, &views[i].y, sizeof views[i].y);
        aclear(neighbors);
        for (int j = 0; j < count; j++) {
            const material_mask *neighbor = views[j].mask;
            assert(neighbor && neighbor->cell_size == mask->cell_size);
            if (i == j ||
                fabs((double)views[i].x - views[j].x) >=
                    reach_x + neighbor->width * mask->cell_size * .5 ||
                fabs((double)views[i].y - views[j].y) >=
                    reach_y + neighbor->height * mask->cell_size * .5) {
                continue;
            }
            apush(neighbors, j);
            signature = halo_hash(
                signature,
                &neighbor->incarnation,
                sizeof neighbor->incarnation
            );
            signature = halo_hash(
                signature,
                &neighbor->content_revision,
                sizeof neighbor->content_revision
            );
            signature = halo_hash(signature, &views[j].x, sizeof views[j].x);
            signature = halo_hash(signature, &views[j].y, sizeof views[j].y);
        }
        if (!signature) {
            signature = 1;
        }
        if (mask->halo_signature == signature && mask->halo_radius == radius &&
            mask->lod_limit == lod_limit) {
            continue;
        }
        if (!cells) {
            cells = xmalloc((size_t)side * side);
            distance = xcalloc((size_t)side * side, sizeof(float));
        }
        memset(cells, MATERIAL_HALO_UNAVAILABLE, (size_t)width * height);
        for (int y = -radius; y < mask->height + radius; y++) {
            for (int x = -radius; x < mask->width + radius; x++) {
                if (x >= 0 && y >= 0 && x < mask->width && y < mask->height) {
                    continue;
                }
                double wx = views[i].x + (x + .5 - mask->width * .5) * mask->cell_size;
                double wy = views[i].y + (y + .5 - mask->height * .5) * mask->cell_size;
                for (int n = 0; n < acount(neighbors); n++) {
                    int j = neighbors[n];
                    const material_mask *neighbor = views[j].mask;
                    double nx = (wx - views[j].x) / mask->cell_size +
                                neighbor->width * .5;
                    double ny = (wy - views[j].y) / mask->cell_size +
                                neighbor->height * .5;
                    if (!(nx >= 0 && nx < neighbor->width && ny >= 0 &&
                          ny < neighbor->height)) {
                        continue;
                    }
                    int at = (int)ny * neighbor->width + (int)nx;
                    int out = (y + radius) * width + x + radius;
                    cells[out] = neighbor->cells[at];
                    distance[out] = neighbor->distance[at];
                    break;
                }
            }
        }
        material_mask_set_halo(mask, (material_mask_halo){ radius, lod_limit, cells, distance });
        mask->halo_signature = signature;
    }
    free(cells);
    free(distance);
    afree(neighbors);
}
