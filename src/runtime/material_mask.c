#include "runtime/material_mask.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static float grid_position(int index, int size, float cell)
{
    return (fmaxf(0, fminf(size, index - .5f)) - size * .5f) * cell;
}

static void surface_polygon(material_mask *mask, int bits, int x, int y, int w, int h)
{
    float left = grid_position(x, mask->width, mask->cell_size);
    float right = grid_position(x + w, mask->width, mask->cell_size);
    float top = grid_position(y, mask->height, mask->cell_size);
    float bottom = grid_position(y + h, mask->height, mask->cell_size);
    float corners[8] = { left, top, right, top, right, bottom, left, bottom };
    int x0 = x > 0 ? x - 1 : 0, x1 = x < mask->width ? x : x - 1;
    int y0 = y > 0 ? y - 1 : 0, y1 = y < mask->height ? y : y - 1;
    float values[4] = { mask->distance[y0 * mask->width + x0],
                        mask->distance[y0 * mask->width + x1],
                        mask->distance[y1 * mask->width + x1],
                        mask->distance[y1 * mask->width + x0] };
    material_polygon polygon = { 0 };
    for (int i = 0; i < 4; i++) {
        int next = (i + 1) % 4;
        if (bits & (1 << i)) {
            polygon.points[2 * polygon.count] = corners[2 * i];
            polygon.points[2 * polygon.count++ + 1] = corners[2 * i + 1];
        }
        if (!!(bits & (1 << i)) != !!(bits & (1 << next))) {
            float sum = fabsf(values[i]) + fabsf(values[next]);
            float t = sum > 0 ? fabsf(values[i]) / sum : .5f;
            polygon.points[2 * polygon.count] = corners[2 * i] +
                                                (corners[2 * next] - corners[2 * i]) * t;
            polygon.points[2 * polygon.count++ + 1] = corners[2 * i + 1] +
                                                      (corners[2 * next + 1] -
                                                       corners[2 * i + 1]) *
                                                          t;
        }
    }
    apush(mask->surface, polygon);
}

static void surface(material_mask *mask)
{
    aclear(mask->surface);
    int width = mask->width + 1, height = mask->height + 1;
    unsigned char *tiles = xcalloc((size_t)width * height, 1);
    for (int y = 0; y < height; y++) {
        int top = y > 0 ? y - 1 : 0, bottom = y < mask->height ? y : y - 1;
        for (int x = 0; x < width; x++) {
            int left = x > 0 ? x - 1 : 0, right = x < mask->width ? x : x - 1;
            tiles[y * width + x] = mask->cells[top * mask->width + left] |
                                   (mask->cells[top * mask->width + right] << 1) |
                                   (mask->cells[bottom * mask->width + right] << 2) |
                                   (mask->cells[bottom * mask->width + left] << 3);
        }
    }
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int bits = tiles[y * width + x];
            if (!bits) {
                continue;
            }
            if (bits == 5 || bits == 10) {
                /* Keep diagonal islands separate, matching four-neighbor physics. */
                surface_polygon(mask, bits & 3, x, y, 1, 1);
                surface_polygon(mask, bits & 12, x, y, 1, 1);
                continue;
            }
            int w = 1, h = 1;
            if (bits == 15) {
                while (x + w < width && tiles[y * width + x + w] == 15) {
                    w++;
                }
                while (y + h < height) {
                    int row = 0;
                    while (row < w && tiles[(y + h) * width + x + row] == 15) {
                        row++;
                    }
                    if (row < w) {
                        break;
                    }
                    h++;
                }
                for (int row = 0; row < h; row++) {
                    memset(tiles + (y + row) * width + x, 0, (size_t)w);
                }
            }
            surface_polygon(mask, bits, x, y, w, h);
        }
    }
    free(tiles);
}

static void rectangles(material_mask *mask)
{
    aclear(mask->rects);
    int length = mask->width * mask->height;
    unsigned char *used = xcalloc((size_t)length, 1);
    for (int y = 0; y < mask->height; y++) {
        for (int x = 0; x < mask->width; x++) {
            int i = y * mask->width + x;
            if (!mask->cells[i] || used[i]) {
                continue;
            }
            int w = 1, h = 1;
            while (x + w < mask->width && mask->cells[i + w] && !used[i + w]) {
                w++;
            }
            while (y + h < mask->height) {
                bool full = true;
                for (int j = 0; j < w; j++) {
                    int next = (y + h) * mask->width + x + j;
                    if (!mask->cells[next] || used[next]) {
                        full = false;
                        break;
                    }
                }
                if (!full) {
                    break;
                }
                h++;
            }
            for (int row = 0; row < h; row++) {
                memset(used + (y + row) * mask->width + x, 1, (size_t)w);
            }
            float cell = mask->cell_size;
            apush(
                mask->rects,
                ((material_rect){ (x + w * .5f - mask->width * .5f) * cell,
                                  (y + h * .5f - mask->height * .5f) * cell,
                                  w * cell,
                                  h * cell })
            );
        }
    }
    free(used);
    surface(mask);
    mask->revision++;
}

material_mask material_mask_new(int width, int height, float cell_size, bool ellipse)
{
    assert(width > 0 && height > 0 && width <= 256 && height <= 256);
    assert(isfinite(cell_size) && cell_size > 0);
    material_mask mask = { .width = width, .height = height, .cell_size = cell_size };
    mask.cells = xcalloc((size_t)width * height, 1);
    mask.distance = xmalloc((size_t)width * height * sizeof *mask.distance);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float px = (2.f * x + 1) / width - 1;
            float py = (2.f * y + 1) / height - 1;
            bool filled = !ellipse || px * px + py * py <= 1;
            mask.cells[y * width + x] = filled;
            mask.distance[y * width + x] = ellipse
                                             ? (sqrtf(px * px + py * py) - 1) *
                                                   fminf(width, height) * cell_size * .5f
                                             : fmaxf((fabsf(px) - 1) * width, (fabsf(py) - 1) * height) *
                                                   cell_size * .5f;
            mask.count += filled;
        }
    }
    rectangles(&mask);
    return mask;
}

void material_mask_free(material_mask *mask)
{
    free(mask->cells);
    free(mask->damage);
    free(mask->distance);
    afree(mask->rects);
    afree(mask->surface);
    *mask = (material_mask){ 0 };
}

material_mask material_mask_from_cells(int width, int height, float cell_size, const unsigned char *cells)
{
    assert(width > 0 && height > 0 && width <= 256 && height <= 256 && cells);
    assert(isfinite(cell_size) && cell_size > 0);
    material_mask mask = { .width = width, .height = height, .cell_size = cell_size };
    mask.cells = xmalloc((size_t)width * height);
    mask.distance = xmalloc((size_t)width * height * sizeof *mask.distance);
    for (int i = 0; i < width * height; i++) {
        mask.cells[i] = cells[i] != 0;
        mask.distance[i] = mask.cells[i] ? -cell_size * .5f : cell_size * .5f;
        mask.count += mask.cells[i];
    }
    rectangles(&mask);
    return mask;
}

material_mask material_mask_from_field(int width, int height, float cell_size, const float *distance)
{
    assert(width > 0 && height > 0 && width <= 256 && height <= 256 && distance);
    assert(isfinite(cell_size) && cell_size > 0);
    material_mask mask = { .width = width, .height = height, .cell_size = cell_size };
    mask.cells = xmalloc((size_t)width * height);
    mask.distance = xmalloc((size_t)width * height * sizeof *mask.distance);
    for (int i = 0; i < width * height; i++) {
        assert(isfinite(distance[i]));
        mask.distance[i] = distance[i];
        mask.cells[i] = distance[i] <= 0;
        mask.count += mask.cells[i];
    }
    rectangles(&mask);
    return mask;
}

bool material_mask_contains(const material_mask *mask, float x, float y)
{
    float px = floorf(x / mask->cell_size + mask->width * .5f);
    float py = floorf(y / mask->cell_size + mask->height * .5f);
    if (!(px >= 0 && py >= 0 && px < mask->width && py < mask->height)) {
        return false;
    }
    return mask->cells[(int)py * mask->width + (int)px] != 0;
}

void material_mask_rebuild(material_mask *mask)
{
    rectangles(mask);
}

int material_mask_damage(material_mask *mask, float x, float y, float radius, float energy, float hardness)
{
    assert(
        isfinite(x + y + radius + energy + hardness) && radius >= 0 &&
        energy >= 0 && hardness > 0
    );
    if (!mask->damage) {
        mask->damage = xcalloc((size_t)mask->width * mask->height, sizeof *mask->damage);
    }
    int left = (int)fminf(
        mask->width,
        fmaxf(0, floorf((x - radius) / mask->cell_size + mask->width * .5f))
    );
    int right = (int)fmaxf(
        -1,
        fminf(mask->width - 1, floorf((x + radius) / mask->cell_size + mask->width * .5f))
    );
    int top = (int)fminf(
        mask->height,
        fmaxf(0, floorf((y - radius) / mask->cell_size + mask->height * .5f))
    );
    int bottom = (int)fmaxf(
        -1,
        fminf(mask->height - 1, floorf((y + radius) / mask->cell_size + mask->height * .5f))
    );
    int removed = 0;
    for (int row = top; row <= bottom; row++) {
        float dy = (row + .5f - mask->height * .5f) * mask->cell_size - y;
        for (int col = left; col <= right; col++) {
            int i = row * mask->width + col;
            float dx = (col + .5f - mask->width * .5f) * mask->cell_size - x;
            if (!mask->cells[i] || dx * dx + dy * dy > radius * radius) {
                continue;
            }
            mask->damage[i] += energy;
            if (mask->damage[i] < hardness) {
                continue;
            }
            mask->cells[i] = 0;
            removed++;
        }
    }
    mask->count -= removed;
    if (removed) {
        /* SDF subtraction retains a circular cut between the samples. Resistance
         * still owns cell removal, so an unbroken cell must retain its sign. */
        int y0 = top > 0 ? top - 1 : 0,
            y1 = bottom + 1 < mask->height ? bottom + 1 : bottom;
        int x0 = left > 0 ? left - 1 : 0,
            x1 = right + 1 < mask->width ? right + 1 : right;
        for (int row = y0; row <= y1; row++) {
            float dy = (row + .5f - mask->height * .5f) * mask->cell_size - y;
            for (int col = x0; col <= x1; col++) {
                int i = row * mask->width + col;
                float dx = (col + .5f - mask->width * .5f) * mask->cell_size - x;
                float cut = radius - hypotf(dx, dy);
                if (mask->cells[i]) {
                    cut = fminf(cut, -.0001f);
                }
                mask->distance[i] = fmaxf(mask->distance[i], cut);
            }
        }
    }
    return removed;
}

int material_mask_carve(material_mask *mask, float x, float y, float radius)
{
    int removed = material_mask_damage(mask, x, y, radius, 1, 1);
    if (removed) {
        rectangles(mask);
    }
    return removed;
}

static material_mask *split(material_mask *mask, int min_cells, int *dust, bool anchored)
{
    assert(min_cells > 0 && dust);
    *dust = 0;
    int length = mask->width * mask->height;
    int *labels = xcalloc((size_t)length, sizeof *labels);
    int *queue = xmalloc((size_t)length * sizeof *queue);
    int *counts = NULL;
    bool *edges = NULL;
    apush(counts, 0);
    apush(edges, false);
    int largest = 0;
    for (int i = 0; i < length; i++) {
        if (!mask->cells[i] || labels[i]) {
            continue;
        }
        int label = acount(counts), end = 1;
        queue[0] = i;
        labels[i] = label;
        bool edge = false;
        for (int head = 0; head < end; head++) {
            int at = queue[head];
            edge |= at < mask->width || at >= length - mask->width ||
                    at % mask->width == 0 || at % mask->width == mask->width - 1;
            int neighbors[] = { at - 1, at + 1, at - mask->width, at + mask->width };
            for (int n = 0; n < 4; n++) {
                int next = neighbors[n];
                if (next < 0 || next >= length || (n == 0 && at % mask->width == 0) ||
                    (n == 1 && at % mask->width == mask->width - 1) ||
                    !mask->cells[next] || labels[next]) {
                    continue;
                }
                labels[next] = label;
                queue[end++] = next;
            }
        }
        apush(counts, end);
        apush(edges, edge);
        if (end > counts[largest]) {
            largest = label;
        }
    }
    material_mask *pieces = NULL;
    for (int label = 1; label < acount(counts); label++) {
        if (anchored ? edges[label] : label == largest) {
            continue;
        }
        if (counts[label] < min_cells) {
            *dust += counts[label];
            continue;
        }
        material_mask piece = { .width = mask->width,
                                .height = mask->height,
                                .cell_size = mask->cell_size,
                                .count = counts[label] };
        piece.cells = xcalloc((size_t)length, 1);
        piece.distance = xmalloc((size_t)length * sizeof *piece.distance);
        if (mask->damage) {
            piece.damage = xcalloc((size_t)length, sizeof *piece.damage);
        }
        for (int i = 0; i < length; i++) {
            piece.cells[i] = labels[i] == label;
            piece.distance[i] = piece.cells[i]
                                  ? mask->distance[i]
                                  : fmaxf(.0001f, fabsf(mask->distance[i]));
            if (piece.damage && piece.cells[i]) {
                piece.damage[i] = mask->damage[i];
            }
        }
        rectangles(&piece);
        apush(pieces, piece);
    }
    if (acount(counts) > 2 || anchored) {
        mask->count = 0;
        for (int i = 0; i < length; i++) {
            mask->cells[i] = labels[i] &&
                             (anchored ? edges[labels[i]] : labels[i] == largest);
            mask->count += mask->cells[i];
            if (!mask->cells[i]) {
                mask->distance[i] = fmaxf(.0001f, fabsf(mask->distance[i]));
            }
        }
        rectangles(mask);
    }
    afree(counts);
    afree(edges);
    free(queue);
    free(labels);
    return pieces;
}

material_mask *material_mask_split(material_mask *mask, int min_cells, int *dust)
{
    return split(mask, min_cells, dust, false);
}

material_mask *material_mask_split_anchored(material_mask *mask, int min_cells, int *dust)
{
    return split(mask, min_cells, dust, true);
}
