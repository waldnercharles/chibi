

#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

static float sample(unsigned seed, int x, int y)
{
    unsigned hash = seed ^ (unsigned)x * 0x9e3779b9u ^ (unsigned)y * 0x85ebca6bu;
    hash = (hash ^ (hash >> 16)) * 0x7feb352du;
    hash = (hash ^ (hash >> 15)) * 0x846ca68bu;
    return ((hash ^ (hash >> 16)) & 65535) / 32767.5f - 1;
}

static float mix(float a, float b, float t)
{
    return a + (b - a) * t;
}

static float noise(unsigned seed, float x, float y)
{
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float tx = x - ix, ty = y - iy;
    tx = tx * tx * (3 - 2 * tx);
    ty = ty * ty * (3 - 2 * ty);
    return mix(
        mix(sample(seed, ix, iy), sample(seed, ix + 1, iy), tx),
        mix(sample(seed, ix, iy + 1), sample(seed, ix + 1, iy + 1), tx),
        ty
    );
}

static float wall_noise(unsigned seed, float x, float y)
{
    return .57f * noise(seed, x / 170, y / 170) +
           .28f * noise(seed + 1, x / 65, y / 65) +
           .15f * noise(seed + 2, x / 23, y / 23);
}

/* Fill isolated air pockets. Every remaining opening belongs to the same
 * navigable cave, even when surface noise cuts a tiny pocket off a passage. */
static void connect_air(unsigned char *cells, int width, int height, int start)
{
    int *queue = xmalloc((size_t)width * height * sizeof *queue);
    int end = 1;
    queue[0] = start;
    cells[start] = 2;
    for (int head = 0; head < end; head++) {
        int at = queue[head];
        int neighbors[] = { at - 1, at + 1, at - width, at + width };
        for (int n = 0; n < 4; n++) {
            int next = neighbors[n];
            if (next < 0 || next >= width * height || (n == 0 && at % width == 0) ||
                (n == 1 && at % width == width - 1) || cells[next]) {
                continue;
            }
            cells[next] = 2;
            queue[end++] = next;
        }
    }
    for (int i = 0; i < width * height; i++) {
        cells[i] = cells[i] != 2;
    }
    free(queue);
}

float *reference_cave_generate(unsigned seed, int width, int height, float cell_size, float radius, float roughness)
{
    assert(width >= 64 && width <= 4096 && height >= 64 && height <= 4096);
    assert(
        cell_size > 0 && radius >= 16 * cell_size && roughness >= 0 &&
        roughness <= radius * .5f
    );
    unsigned char *cells = xmalloc((size_t)width * height);
    float *field = xmalloc((size_t)width * height * sizeof *field);
    int start = -1;
    for (int x = 0; x < width; x++) {
        float px = (x + .5f - width * .5f) * cell_size;
        float center = radius * .55f * noise(seed + 3, px / 700, 0);
        float span = radius * (1 + .4f * noise(seed + 4, px / 420, 0));
        float sector = px / (radius * 5);
        int branch = (int)floorf(sector);
        float arc = sinf((sector - branch) * 3.14159265f);
        float side = sample(seed + 5, branch, 0) > 0 ? 1 : -1;
        float branch_y = center + side * radius * 2.15f * arc;
        for (int y = 0; y < height; y++) {
            float py = (y + .5f - height * .5f) * cell_size;
            float density = fminf(fabsf(py - center) - span, fabsf(py - branch_y) - radius * .43f);
            density += roughness * wall_noise(seed, px, py);
            bool border = x < 8 || y < 8 || x >= width - 8 || y >= height - 8;
            int at = y * width + x;
            cells[at] = border || density > 0;
            field[at] = border ? -cell_size : -density;
            if (!cells[at] && start < 0) {
                start = at;
            }
        }
    }
    assert(start >= 0);
    connect_air(cells, width, height, start);
    for (int i = 0; i < width * height; i++) {
        if (cells[i] && field[i] >= 0) {
            field[i] = -cell_size;
        }
    }
    free(cells);
    return field;
}
