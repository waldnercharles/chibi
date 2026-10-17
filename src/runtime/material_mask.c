#include "runtime/material_mask.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static _Atomic uint64_t next_incarnation = 1;

static uint64_t incarnation()
{
    uint64_t value = atomic_fetch_add_explicit(&next_incarnation, 1, memory_order_relaxed);
    assert(value && value != UINT64_MAX);
    return value;
}

struct material_mask_work
{
    unsigned char *tiles, *used;
    int *labels, *queue, *counts;
    bool *edges;
};

material_mask_work *material_mask_work_new()
{
    return xcalloc(1, sizeof(material_mask_work));
}

static void free_work(material_mask_work *work)
{
    afree(work->tiles);
    afree(work->used);
    afree(work->labels);
    afree(work->queue);
    afree(work->counts);
    afree(work->edges);
}

void material_mask_work_free(material_mask_work *work)
{
    free_work(work);
    free(work);
}

static int halo_index(int width, int height, int radius, int x, int y)
{
    int stride = width + 2 * radius, px = x + radius, py = y + radius;
    if (py < radius) {
        return py * stride + px;
    }
    if (py >= radius + height) {
        return radius * stride + height * 2 * radius +
               (py - radius - height) * stride + px;
    }
    return radius * stride + (py - radius) * 2 * radius +
           (px < radius ? px : px - width);
}

bool material_occupancy_sample(material_occupancy_view v, int x, int y, bool *occupied, float *distance)
{
    int index;
    if (x >= 0 && y >= 0 && x < v.width && y < v.height) {
        index = y * v.width + x;
        *occupied = v.cells[index] != 0;
        *distance = v.distance[index];
        return true;
    }
    int r = v.halo_radius;
    if (!r || x < -r || y < -r || x >= v.width + r || y >= v.height + r) {
        return false;
    }
    index = halo_index(v.width, v.height, r, x, y);
    if (v.halo_cells[index] == MATERIAL_HALO_UNAVAILABLE) {
        return false;
    }
    *occupied = v.halo_cells[index] != 0;
    *distance = v.halo_distance[index];
    return true;
}
bool material_mask_sample(const material_mask *mask, int x, int y, bool *occupied, float *distance)
{
    return material_occupancy_sample(material_mask_occupancy(mask), x, y, occupied, distance);
}
static void surface_sample(const material_mask *mask, int x, int y, bool *occupied, float *distance)
{
    if (material_mask_sample(mask, x, y, occupied, distance)) {
        return;
    }
    int cx = x < 0 ? 0 : x >= mask->width ? mask->width - 1 : x;
    int cy = y < 0 ? 0 : y >= mask->height ? mask->height - 1 : y;
    // At a finite outer edge, keep the neighbor on the other axis.
    if (material_mask_sample(mask, x, cy, occupied, distance) ||
        material_mask_sample(mask, cx, y, occupied, distance)) {
        return;
    }
    material_mask_sample(mask, cx, cy, occupied, distance);
}
static float grid_position(int index, int size, float cell)
{
    return (index - .5f - size * .5f) * cell;
}
/* A marching polygon has at most five vertices; four rectangle clips add at
 * most four. Use generous bounded work storage, then fan any result larger
 * than the renderer's six-vertex polygon contract. */
static void append_clipped(material_mask *mask, material_polygon polygon)
{
    float a[32], b[32];
    int count = polygon.count;
    memcpy(a, polygon.points, (size_t)count * 2 * sizeof(float));
    for (int plane = 0; plane < 4 && count; plane++) {
        int axis = plane / 2;
        float extent = (axis ? mask->height : mask->width) * mask->cell_size * .5f;
        float bound = plane % 2 ? extent : -extent;
        int next_count = 0;
        for (int i = 0, previous = count - 1; i < count; previous = i++) {
            float *p = a + previous * 2, *q = a + i * 2;
            bool pin = plane % 2 ? p[axis] <= bound : p[axis] >= bound;
            bool qin = plane % 2 ? q[axis] <= bound : q[axis] >= bound;
            if (pin != qin) {
                float t = (bound - p[axis]) / (q[axis] - p[axis]);
                assert(next_count < 16);
                b[next_count * 2] = p[0] + t * (q[0] - p[0]);
                b[next_count * 2 + 1] = p[1] + t * (q[1] - p[1]);
                b[next_count * 2 + axis] = bound;
                next_count++;
            }
            if (qin) {
                assert(next_count < 16);
                b[next_count * 2] = q[0];
                b[next_count * 2 + 1] = q[1];
                next_count++;
            }
        }
        count = next_count;
        memcpy(a, b, (size_t)count * 2 * sizeof(float));
    }
    if (count < 3) {
        return;
    }
    double area = 0;
    for (int i = 0, previous = count - 1; i < count; previous = i++) {
        area += (double)a[previous * 2] * a[i * 2 + 1] -
                (double)a[i * 2] * a[previous * 2 + 1];
    }
    if (area == 0) {
        return;
    }
    if (count <= 6) {
        polygon.count = count;
        memcpy(polygon.points, a, (size_t)count * 2 * sizeof(float));
        apush(mask->surface, polygon);
        return;
    }
    for (int i = 1; i + 1 < count; i++) {
        polygon.count = 3;
        memcpy(polygon.points, a, 2 * sizeof(float));
        memcpy(polygon.points + 2, a + i * 2, 4 * sizeof(float));
        apush(mask->surface, polygon);
    }
}

static void surface_polygon(material_mask *mask, int bits, int x, int y, int w, int h)
{
    float left = grid_position(x, mask->width, mask->cell_size);
    float right = grid_position(x + w, mask->width, mask->cell_size);
    float top = grid_position(y, mask->height, mask->cell_size);
    float bottom = grid_position(y + h, mask->height, mask->cell_size);
    float corners[8] = { left, top, right, top, right, bottom, left, bottom };
    int x0 = x - 1, x1 = x + w - 1, y0 = y - 1, y1 = y + h - 1;
    float values[4];
    bool occupied;
    surface_sample(mask, x0, y0, &occupied, &values[0]);
    surface_sample(mask, x1, y0, &occupied, &values[1]);
    surface_sample(mask, x1, y1, &occupied, &values[2]);
    surface_sample(mask, x0, y1, &occupied, &values[3]);
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
    append_clipped(mask, polygon);
}

static void surface(material_mask *mask, material_mask_work *work)
{
    aclear(mask->surface);
    int width = mask->width + 1, height = mask->height + 1;
    asetlen(work->tiles, width * height);
    unsigned char *tiles = work->tiles;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int coordinates[4][2] = { { x - 1, y - 1 },
                                      { x, y - 1 },
                                      { x, y },
                                      { x - 1, y } };
            int bits = 0;
            for (int i = 0; i < 4; i++) {
                bool occupied;
                float distance;
                surface_sample(mask, coordinates[i][0], coordinates[i][1], &occupied, &distance);
                if (occupied) {
                    bits |= 1 << i;
                }
            }
            tiles[y * width + x] = (unsigned char)bits;
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
}

bool material_mask_set_halo(material_mask *mask, material_mask_halo halo)
{
    assert(mask && halo.radius >= 0 && halo.radius <= MATERIAL_HALO_RADIUS_MAX);
    assert(halo.lod_limit >= 0 && halo.lod_limit <= 7);
    assert(!halo.radius || (halo.cells && halo.distance));
    bool changed = mask->halo_radius != halo.radius || mask->lod_limit != halo.lod_limit;
    int width = mask->width + 2 * halo.radius, height = mask->height + 2 * halo.radius;
    for (int y = 0; halo.radius && y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (x >= halo.radius && y >= halo.radius &&
                x < width - halo.radius && y < height - halo.radius) {
                continue;
            }
            int input = y * width + x;
            int owned = halo_index(mask->width, mask->height, halo.radius, x - halo.radius, y - halo.radius);
            assert(halo.cells[input] <= MATERIAL_HALO_UNAVAILABLE);
            assert(
                halo.cells[input] == MATERIAL_HALO_UNAVAILABLE ||
                isfinite(halo.distance[input])
            );
            if (!changed && (mask->halo_cells[owned] != halo.cells[input] ||
                             (halo.cells[input] != MATERIAL_HALO_UNAVAILABLE &&
                              mask->halo_distance[owned] != halo.distance[input]))) {
                changed = true;
            }
        }
    }
    if (!changed) {
        return false;
    }
    size_t count = (size_t)width * height - (size_t)mask->width * mask->height;
    bool resized = mask->halo_radius != halo.radius;
    unsigned char *cells = resized ? (halo.radius ? xmalloc(count) : NULL)
                                   : mask->halo_cells;
    float *distance = resized ? (halo.radius ? xmalloc(count * sizeof(float)) : NULL)
                              : mask->halo_distance;
    for (int y = 0; halo.radius && y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (x >= halo.radius && y >= halo.radius &&
                x < width - halo.radius && y < height - halo.radius) {
                continue;
            }
            int input = y * width + x;
            int owned = halo_index(mask->width, mask->height, halo.radius, x - halo.radius, y - halo.radius);
            cells[owned] = halo.cells[input];
            distance[owned] = halo.cells[input] == MATERIAL_HALO_UNAVAILABLE
                                ? 0
                                : halo.distance[input];
        }
    }
    if (resized) {
        free(mask->halo_cells);
        free(mask->halo_distance);
    }
    mask->halo_cells = cells;
    mask->halo_distance = distance;
    mask->halo_radius = halo.radius;
    mask->lod_limit = halo.radius ? halo.lod_limit : 0;
    mask->halo_signature = 0;
    material_mask_work work = { 0 };
    surface(mask, &work);
    free_work(&work);
    if (++mask->revision == 0) {
        mask->incarnation = incarnation();
        mask->revision = 1;
    }
    return true;
}

void material_mask_rebuild_work(material_mask *mask, material_mask_work *work)
{
    aclear(mask->rects);
    int length = mask->width * mask->height;
    asetlen(work->used, length);
    unsigned char *used = work->used;
    memset(used, 0, (size_t)length);
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
    surface(mask, work);
    if (++mask->content_revision == 0) {
        mask->incarnation = incarnation();
    }
    mask->halo_signature = 0;
    if (++mask->revision == 0) {
        mask->incarnation = incarnation();
        mask->revision = 1;
    }
}

material_mask material_mask_new(int width, int height, float cell_size, bool ellipse)
{
    assert(width > 0 && height > 0 && width <= 256 && height <= 256);
    assert(isfinite(cell_size) && cell_size > 0);
    material_mask mask = { .width = width,
                           .height = height,
                           .cell_size = cell_size,
                           .incarnation = incarnation() };
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
    material_mask_rebuild(&mask);
    return mask;
}

void material_mask_free(material_mask *mask)
{
    free(mask->cells);
    free(mask->damage);
    free(mask->distance);
    free(mask->halo_cells);
    free(mask->halo_distance);
    afree(mask->rects);
    afree(mask->surface);
    *mask = (material_mask){ 0 };
}

material_mask material_mask_copy_cells(int width, int height, float cell_size, const unsigned char *cells)
{
    assert(width > 0 && height > 0 && width <= 256 && height <= 256 && cells);
    assert(isfinite(cell_size) && cell_size > 0);
    material_mask mask = { .width = width,
                           .height = height,
                           .cell_size = cell_size,
                           .incarnation = incarnation() };
    mask.cells = xmalloc((size_t)width * height);
    mask.distance = xmalloc((size_t)width * height * sizeof *mask.distance);
    for (int i = 0; i < width * height; i++) {
        mask.cells[i] = cells[i] != 0;
        mask.distance[i] = mask.cells[i] ? -cell_size * .5f : cell_size * .5f;
        mask.count += mask.cells[i];
    }
    return mask;
}

material_mask material_mask_from_cells(int width, int height, float cell_size, const unsigned char *cells)
{
    material_mask mask = material_mask_copy_cells(width, height, cell_size, cells);
    material_mask_rebuild(&mask);
    return mask;
}

material_mask material_mask_from_field(int width, int height, float cell_size, const float *distance)
{
    assert(width > 0 && height > 0 && width <= 256 && height <= 256 && distance);
    assert(isfinite(cell_size) && cell_size > 0);
    material_mask mask = { .width = width,
                           .height = height,
                           .cell_size = cell_size,
                           .incarnation = incarnation() };
    mask.cells = xmalloc((size_t)width * height);
    mask.distance = xmalloc((size_t)width * height * sizeof *mask.distance);
    for (int i = 0; i < width * height; i++) {
        assert(isfinite(distance[i]));
        mask.distance[i] = distance[i];
        mask.cells[i] = distance[i] <= 0;
        mask.count += mask.cells[i];
    }
    material_mask_rebuild(&mask);
    return mask;
}

material_mask material_mask_reduce(const material_mask *mask)
{
    assert(mask->width % 2 == 0 && mask->height % 2 == 0);
    int width = mask->width / 2, height = mask->height / 2;
    material_mask half = { .width = width,
                           .height = height,
                           .cell_size = mask->cell_size * 2,
                           .incarnation = incarnation() };
    half.cells = xmalloc((size_t)width * height);
    half.distance = xmalloc((size_t)width * height * sizeof *half.distance);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const float *block = mask->distance + 2 * y * mask->width + 2 * x;
            int i = y * width + x;
            half.distance[i] = (block[0] + block[1] + block[mask->width] +
                                block[mask->width + 1]) *
                               .25f;
            half.cells[i] = half.distance[i] <= 0;
            half.count += half.cells[i];
        }
    }
    material_mask_rebuild(&half);
    int radius = mask->halo_radius / 2;
    if (radius) {
        int padded_width = width + 2 * radius, padded_height = height + 2 * radius;
        unsigned char *cells = xmalloc((size_t)padded_width * padded_height);
        float *distance = xcalloc((size_t)padded_width * padded_height, sizeof(float));
        memset(cells, MATERIAL_HALO_UNAVAILABLE, (size_t)padded_width * padded_height);
        for (int y = -radius; y < height + radius; y++) {
            for (int x = -radius; x < width + radius; x++) {
                if (x >= 0 && y >= 0 && x < width && y < height) {
                    continue;
                }
                bool known = true, occupied;
                float samples[4] = { 0 };
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        float value;
                        if (!material_mask_sample(mask, 2 * x + dx, 2 * y + dy, &occupied, &value)) {
                            known = false;
                        } else {
                            samples[dy * 2 + dx] = value;
                        }
                    }
                }
                int i = (y + radius) * padded_width + x + radius;
                if (known) {
                    distance[i] = (samples[0] + samples[1] + samples[2] + samples[3]) *
                                  .25f;
                    cells[i] = distance[i] <= 0;
                }
            }
        }
        material_mask_set_halo(
            &half,
            (
                material_mask_halo
            ){ radius, mask->lod_limit > 0 ? mask->lod_limit - 1 : 0, cells, distance }
        );
        free(cells);
        free(distance);
    }
    return half;
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
    material_mask_work work = { 0 };
    material_mask_rebuild_work(mask, &work);
    free_work(&work);
}

int material_mask_apply_damage(material_mask *mask, float x, float y, float radius, float energy, float hardness)
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
        material_mask_cut_field(mask, x, y, radius);
    }
    return removed;
}

bool material_mask_cut_field(material_mask *mask, float x, float y, float radius)
{
    assert(mask && isfinite(x) && isfinite(y) && isfinite(radius) && radius >= 0);
    int left = (int)fmax(
        0,
        fmin(mask->width, floor(((double)x - radius) / mask->cell_size + mask->width * .5) - 1)
    );
    int right = (int)fmax(
        -1,
        fmin(mask->width - 1, floor(((double)x + radius) / mask->cell_size + mask->width * .5) + 1)
    );
    int top = (int)fmax(
        0,
        fmin(mask->height, floor(((double)y - radius) / mask->cell_size + mask->height * .5) - 1)
    );
    int bottom = (int)fmax(
        -1,
        fmin(mask->height - 1, floor(((double)y + radius) / mask->cell_size + mask->height * .5) + 1)
    );
    bool changed = false;
    for (int row = top; row <= bottom; row++) {
        float dy = (row + .5f - mask->height * .5f) * mask->cell_size - y;
        for (int col = left; col <= right; col++) {
            int i = row * mask->width + col;
            float dx = (col + .5f - mask->width * .5f) * mask->cell_size - x;
            float cut = radius - (float)sqrt((double)dx * dx + (double)dy * dy);
            // Occupancy owns breakage; SDF-only synchronization cannot break cells
            // or turn an already empty cell solid at a zero-distance boundary.
            cut = mask->cells[i] ? fminf(cut, -.0001f) : fmaxf(cut, .0001f);
            float value = fmaxf(mask->distance[i], cut);
            if (value != mask->distance[i]) {
                mask->distance[i] = value;
                changed = true;
            }
        }
    }
    return changed;
}

int material_mask_carve(material_mask *mask, float x, float y, float radius)
{
    int removed = material_mask_apply_damage(mask, x, y, radius, 1, 1);
    bool changed = material_mask_cut_field(mask, x, y, radius);
    if (removed || changed) {
        material_mask_rebuild(mask);
    }
    return removed;
}

material_mask *material_mask_detach_fragments(
    material_mask *mask,
    int min_cells,
    bool anchored,
    int *dust,
    material_mask_work *work
)
{
    assert(min_cells > 0 && dust);
    *dust = 0;
    int length = mask->width * mask->height;
    asetlen(work->labels, length);
    asetlen(work->queue, length);
    memset(work->labels, 0, (size_t)length * sizeof *work->labels);
    int *labels = work->labels, *queue = work->queue;
    aclear(work->counts);
    aclear(work->edges);
    apush(work->counts, 0);
    apush(work->edges, false);
    int largest = 0;
    for (int i = 0; i < length; i++) {
        if (!mask->cells[i] || labels[i]) {
            continue;
        }
        int label = acount(work->counts), end = 1;
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
        apush(work->counts, end);
        apush(work->edges, edge);
        if (end > work->counts[largest]) {
            largest = label;
        }
    }
    material_mask *pieces = NULL;
    for (int label = 1; label < acount(work->counts); label++) {
        if (anchored ? work->edges[label] : label == largest) {
            continue;
        }
        if (work->counts[label] < min_cells) {
            *dust += work->counts[label];
            continue;
        }
        material_mask piece = { .width = mask->width,
                                .height = mask->height,
                                .cell_size = mask->cell_size,
                                .count = work->counts[label],
                                .incarnation = incarnation() };
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
        material_mask_rebuild_work(&piece, work);
        apush(pieces, piece);
    }
    if (acount(work->counts) > 2 || anchored) {
        mask->count = 0;
        for (int i = 0; i < length; i++) {
            mask->cells[i] = labels[i] && (anchored ? work->edges[labels[i]]
                                                    : labels[i] == largest);
            mask->count += mask->cells[i];
            if (!mask->cells[i]) {
                mask->distance[i] = fmaxf(.0001f, fabsf(mask->distance[i]));
            }
        }
    }
    return pieces;
}

material_mask *material_mask_extract_fragments(material_mask *mask, int minimum, bool anchored, int *dust)
{
    material_mask_work work = { 0 };
    int before = mask->count;
    material_mask *pieces = material_mask_detach_fragments(mask, minimum, anchored, dust, &work);
    if (before != mask->count) {
        material_mask_rebuild_work(mask, &work);
    }
    free_work(&work);
    return pieces;
}

material_occupancy_view material_mask_occupancy(const material_mask *mask)
{
    return (material_occupancy_view){ .cells = mask->cells,
                                      .distance = mask->distance,
                                      .width = mask->width,
                                      .height = mask->height,
                                      .count = mask->count,
                                      .cell_size = mask->cell_size,
                                      .revision = mask->revision,
                                      .incarnation = mask->incarnation,
                                      .halo_cells = mask->halo_cells,
                                      .halo_distance = mask->halo_distance,
                                      .halo_radius = mask->halo_radius,
                                      .lod_limit = mask->lod_limit };
}
material_collision_view material_mask_collision(const material_mask *mask)
{
    return (material_collision_view){ mask->rects, acount(mask->rects), mask->revision };
}
material_surface_view material_mask_surface(const material_mask *mask)
{
    return (material_surface_view){ mask->surface, acount(mask->surface), mask->revision };
}
