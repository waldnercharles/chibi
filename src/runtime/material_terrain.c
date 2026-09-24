#include "runtime/material_terrain.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
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
            int count = 0;
            for (int row = 0; row < h; row++) {
                memcpy(samples + row * w, field + (y + row) * grid.width + x, (size_t)w * sizeof(float));
                for (int col = 0; col < w; col++) {
                    count += samples[row * w + col] <= 0;
                }
            }
            if (!count) {
                continue;
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
