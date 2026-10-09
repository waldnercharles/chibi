#include "runtime/images/sky.h"

#include "support/array.h"
#include "support/xalloc.h"
#include "types/value_wire.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SKY_MAGIC UINT64_C(0x31594b5342484343)
#define HALF_MAX 65504.0f
static const float pi = 3.14159265f;

typedef struct
{
    char *key;
    render_sky sky;
} sky_entry;
struct render_sky_table
{
    sky_entry *entries;
};
static render_sky_table resident;

void render_sky_free(render_sky *sky)
{
    free(sky->texels);
    *sky = (render_sky){ 0 };
}

bool render_sky_sides_valid(int width, int height)
{
    return width >= 2 && width <= RENDER_SKY_WIDTH_MAX && width % 2 == 0 &&
           height == width / 2;
}

int render_sky_face_size(const render_sky *sky)
{
    int size = 1;
    while (size * 2 <= sky->width / 4) {
        size *= 2;
    }
    return size;
}

static void texel(const render_sky *sky, int x, int y, float rgb[3])
{
    const unsigned char *t = sky->texels + ((size_t)y * sky->width + x) * 4;
    float scale = t[3] ? ldexpf(1, t[3] - 136) : 0;
    for (int i = 0; i < 3; i++) {
        rgb[i] = t[i] * scale;
    }
}

/* Bilinear radiance at equirectangular position (u, v) in texels, wrapping
 * around the horizon and clamping at the poles. */
static void sample(const render_sky *sky, float u, float v, float rgb[3])
{
    float x = u - .5f, y = v - .5f;
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - (float)x0, fy = y - (float)y0;
    int columns[2] = { ((x0 % sky->width) + sky->width) % sky->width, 0 };
    columns[1] = (columns[0] + 1) % sky->width;
    int rows[2] = { y0 < 0 ? 0 : y0, y0 + 1 >= sky->height ? sky->height - 1 : y0 + 1 };
    float weights[2][2] = { { (1 - fx) * (1 - fy), fx * (1 - fy) },
                            { (1 - fx) * fy, fx * fy } };
    rgb[0] = rgb[1] = rgb[2] = 0;
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < 2; i++) {
            float tap[3];
            texel(sky, columns[i], rows[j], tap);
            for (int c = 0; c < 3; c++) {
                rgb[c] += tap[c] * weights[j][i];
            }
        }
    }
}

static uint16_t half(float value)
{
    assert(value >= 0);
    if (value >= HALF_MAX) {
        return 0x7bff;
    }
    if (value < 0x1p-14f) {
        return (uint16_t)lrintf(value * 0x1p24f);
    }
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    uint32_t result = ((bits >> 23) - 112) << 10 | (bits & 0x7fffff) >> 13;
    uint32_t rest = bits & 0x1fff;
    result += rest > 0x1000 || (rest == 0x1000 && result & 1);
    return (uint16_t)result;
}

void render_sky_face(const render_sky *sky, int face, int size, uint16_t *texels)
{
    assert(face >= 0 && face < 6 && size > 0);
    for (int j = 0; j < size; j++) {
        for (int i = 0; i < size; i++) {
            float s = 2 * (i + .5f) / size - 1, t = 2 * (j + .5f) / size - 1;
            float d[6][3] = { { 1, -t, -s }, { -1, -t, s }, { s, 1, t },
                              { s, -1, -t }, { s, -t, 1 },  { -s, -t, -1 } };
            float *ray = d[face];
            float length = sqrtf(ray[0] * ray[0] + ray[1] * ray[1] + ray[2] * ray[2]);
            float u = .5f + atan2f(ray[0], -ray[2]) / (2 * pi);
            float v = .5f - asinf(ray[1] / length) / pi;
            float rgb[3];
            sample(sky, u * sky->width, v * sky->height, rgb);
            uint16_t *out = texels + ((size_t)j * size + i) * 4;
            for (int c = 0; c < 3; c++) {
                out[c] = half(rgb[c]);
            }
            out[3] = 0x3c00;
        }
    }
}

static int find(const render_sky_table *table, const char *key)
{
    for (int i = 0; i < acount(table->entries); i++) {
        if (!strcmp(table->entries[i].key, key)) {
            return i;
        }
    }
    return -1;
}

const render_sky *render_sky_find(const char *key)
{
    assert(key);
    int index = find(&resident, key);
    return index < 0 ? NULL : &resident.entries[index].sky;
}

/* FNV-1a over the sky's size and texels, never zero. */
static uint64_t content_identity(const render_sky *sky)
{
    uint64_t hash = UINT64_C(0xcbf29ce484222325) ^ (uint64_t)sky->width;
    size_t bytes = (size_t)sky->width * sky->height * 4;
    for (size_t i = 0; i < bytes; i++) {
        hash = (hash ^ sky->texels[i]) * UINT64_C(0x100000001b3);
    }
    return hash ? hash : 1;
}

void render_sky_publish(const char *key, render_sky *sky)
{
    assert(key && render_sky_sides_valid(sky->width, sky->height) && sky->texels);
    sky->identity = content_identity(sky);
    int index = find(&resident, key);
    if (index < 0) {
        apush(resident.entries, ((sky_entry){ .key = xstrdup(key) }));
        index = acount(resident.entries) - 1;
    }
    render_sky_free(&resident.entries[index].sky);
    resident.entries[index].sky = *sky;
    *sky = (render_sky){ 0 };
}

static void free_entries(sky_entry *entries)
{
    for (int i = 0; i < acount(entries); i++) {
        free(entries[i].key);
        render_sky_free(&entries[i].sky);
    }
    afree(entries);
}

void render_skies_clear()
{
    free_entries(resident.entries);
    resident.entries = NULL;
}

static void header(execution_wire *w, int *width, int *height)
{
    uint64_t magic = SKY_MAGIC, version = RENDER_SKY_PREPARED_VERSION;
    execution_wire_u64(w, &magic);
    execution_wire_u64(w, &version);
    execution_wire_int(w, width);
    execution_wire_int(w, height);
    if (!w->error && (magic != SKY_MAGIC || version != RENDER_SKY_PREPARED_VERSION ||
                      !render_sky_sides_valid(*width, *height))) {
        w->error = "Prepared sky header or dimensions are invalid";
    }
}

static bool failure(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}

bool render_sky_prepared_write(
    const render_sky *sky,
    unsigned char **data,
    size_t *length,
    char *error,
    size_t size
)
{
    *data = NULL;
    *length = 0;
    int width = sky->width, height = sky->height;
    const unsigned char *texels = sky->texels;
    size_t bytes = (size_t)width * height * 4;
    execution_wire w = { 0 };
    header(&w, &width, &height);
    execution_wire_blob(&w, &texels, &bytes);
    if (w.error) {
        afree(w.data);
        return failure(error, size, w.error);
    }
    *data = xmalloc(w.size);
    memcpy(*data, w.data, w.size);
    *length = w.size;
    afree(w.data);
    return true;
}

render_sky_table *render_sky_table_new()
{
    return xcalloc(1, sizeof(render_sky_table));
}

size_t render_sky_table_header_bytes()
{
    return sizeof(render_sky_table);
}

/* The bytes a push adds to entries when it must grow them. */
static size_t growth(const sky_entry *entries)
{
    int count = acount(entries), capacity = acap(entries);
    if (count < capacity) {
        return 0;
    }
    int next = capacity ? capacity * 2 : 16;
    return (size_t)(next - capacity) * sizeof *entries +
           (capacity ? 0 : sizeof(dyna_hdr));
}

bool render_sky_table_decode(
    render_sky_table *table,
    const char *key,
    const void *data,
    size_t length,
    size_t *remaining,
    char *error,
    size_t size
)
{
    assert(find(table, key) < 0);
    if (strncmp(key, "content/", 8) || strstr(key, "..") || strchr(key, '\\')) {
        return failure(error, size, "Prepared sky key must be a content/ path without parent traversal");
    }
    execution_wire w = { .data = (unsigned char *)data, .size = length, .reading = true };
    int width = 0, height = 0;
    const unsigned char *texels = NULL;
    size_t bytes = 0;
    header(&w, &width, &height);
    execution_wire_blob(&w, &texels, &bytes);
    if (w.error) {
        return failure(error, size, w.error);
    }
    size_t retained = bytes + strlen(key) + 1 + growth(table->entries);
    if (bytes != (size_t)width * height * 4 || w.offset != length ||
        retained > *remaining) {
        return failure(error, size, "Prepared sky byte count or ownership budget differs");
    }
    render_sky sky = { .width = width, .height = height, .texels = xmalloc(bytes) };
    memcpy(sky.texels, texels, bytes);
    sky.identity = content_identity(&sky);
    apush(table->entries, ((sky_entry){ .key = xstrdup(key), .sky = sky }));
    *remaining -= retained;
    return true;
}

bool render_sky_table_contains(const render_sky_table *table, const char *key)
{
    return find(table, key) >= 0;
}

void render_sky_table_exchange(render_sky_table *table)
{
    assert(table && table != &resident);
    sky_entry *entries = resident.entries;
    resident.entries = table->entries;
    table->entries = entries;
}

void render_sky_table_free(render_sky_table *table)
{
    if (!table) {
        return;
    }
    free_entries(table->entries);
    free(table);
}
