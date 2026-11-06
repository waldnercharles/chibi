#include "render/material.h"

#include "support/array.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void unpack(unsigned color, float out[4])
{
    for (int i = 0; i < 4; i++) {
        out[i] = ((color >> (24 - i * 8)) & 255u) / 255.0f;
    }
}

static void emission(unsigned color, float intensity, float out[4])
{
    if (intensity <= 0) {
        return;
    }
    unpack(color, out);
    for (int i = 0; i < 3; i++) {
        out[i] *= intensity;
    }
}

static void absorption(unsigned filter, float alpha, float coefficient, float out[4])
{
    float channels[4];
    unpack(filter, channels);
    float amount = alpha * coefficient;
    for (int i = 0; i < 3; i++) {
        out[i] = amount * (1 - channels[i]);
    }
    out[3] = amount > 0 ? 1 : 0;
}

bool material_extract(const Material *material, material_coefficients *out)
{
    assert(material && out);
    float intensity = material->emission_intensity * material->emission_multiplier;
    if (!isfinite(material->emission_intensity) ||
        !isfinite(material->emission_multiplier) || !isfinite(material->absorption) ||
        !isfinite(intensity) || !isfinite(material->edge_light) ||
        material->edge_light < 0 || material->edge_light > 8) {
        return false;
    }
    material_coefficients result = { 0 };
    unpack(material->scene, result.scene);
    unpack(material->albedo, result.albedo);
    unpack(material->border_scene, result.border_scene);
    unpack(material->border_albedo, result.border_albedo);
    emission(material->emission_color, intensity, result.emission);
    emission(material->border_emission_color, intensity, result.border_emission);
    absorption(material->filter, result.scene[3], material->absorption, result.absorption);
    absorption(
        material->border_filter,
        result.border_scene[3],
        material->absorption,
        result.border_absorption
    );
    *out = result;
    return true;
}

static bool occupied(material_occupancy_view mask, int x, int y)
{
    if (x >= 0 && y >= 0 && x < mask.width && y < mask.height) {
        return mask.cells[y * mask.width + x] != 0;
    }
    bool solid;
    float distance;
    return material_occupancy_sample(mask, x, y, &solid, &distance) && solid;
}

static bool border_cell(material_occupancy_view mask, int reach, int x, int y)
{
    if (!mask.cells[y * mask.width + x]) {
        return false;
    }
    for (int d = 1; d <= reach; d++) {
        const int samples[4][2] = { { x - d, y }, { x + d, y }, { x, y - d }, { x, y + d } };
        for (int i = 0; i < 4; i++) {
            if (!occupied(mask, samples[i][0], samples[i][1])) {
                return true;
            }
        }
    }
    return false;
}

void render_material_border_build(render_material_border *border, material_occupancy_view mask, int reach)
{
    assert(border && mask.cells && mask.width > 0 && mask.height > 0 && reach > 0);
    aclear(border->rects);
    asetlen(border->row, mask.width);
    for (int x = 0; x < mask.width; x++) {
        border->row[x] = -1;
    }
    for (int y = 0; y < mask.height; y++) {
        for (int x = 0; x < mask.width; x++) {
            if (!border_cell(mask, reach, x, y)) {
                continue;
            }
            int width = 1;
            while (x + width < mask.width && border_cell(mask, reach, x + width, y)) {
                width++;
            }
            int index = border->row[x];
            if (index >= 0 && border->rects[index].width == width &&
                border->rects[index].y + border->rects[index].height == y) {
                border->rects[index].height++;
            } else {
                border->row[x] = acount(border->rects);
                apush(border->rects, ((render_material_border_rect){ x, y, width, 1 }));
            }
            x += width - 1;
        }
    }
}

void render_material_border_free(render_material_border *border)
{
    afree(border->rects);
    afree(border->row);
    *border = (render_material_border){ 0 };
}

typedef struct
{
    uint64_t incarnation;
    uint64_t seen;
    unsigned revision;
    int width, height, reach;
    render_material_border_rect *rects;
    int count, bucket_next, older, newer;
} border_cache_entry;

struct render_material_border_cache
{
    border_cache_entry *entries;
    int *buckets;
    int capacity, bucket_count, count, vacant, oldest, newest;
    int entry_limit;
    size_t bytes, byte_limit;
    uint64_t hits, builds;
    uint64_t frame;
    render_material_border scratch;
};

render_material_border_cache *render_material_border_cache_new(int entries, size_t bytes)
{
    assert(entries >= 0 && entries <= 65536);
    render_material_border_cache *cache = xcalloc(1, sizeof *cache);
    cache->entry_limit = entries;
    cache->byte_limit = bytes;
    cache->vacant = cache->oldest = cache->newest = -1;
    return cache;
}

void render_material_border_cache_begin(render_material_border_cache *cache)
{
    assert(cache);
    if (++cache->frame == 0) {
        render_material_border_cache_clear(cache);
        cache->frame = 1;
    }
}

static unsigned border_bucket(uint64_t incarnation, int buckets)
{
    uint64_t hash = incarnation * UINT64_C(11400714819323198485);
    return (unsigned)(hash ^ (hash >> 32)) & (buckets - 1);
}

static int border_find(const render_material_border_cache *cache, uint64_t incarnation)
{
    if (!cache->bucket_count) {
        return -1;
    }
    int bucket = border_bucket(incarnation, cache->bucket_count);
    for (int i = cache->buckets[bucket]; i >= 0; i = cache->entries[i].bucket_next) {
        if (cache->entries[i].incarnation == incarnation) {
            return i;
        }
    }
    return -1;
}

static void border_unlink(render_material_border_cache *cache, int index)
{
    border_cache_entry *entry = &cache->entries[index];
    if (entry->older >= 0) {
        cache->entries[entry->older].newer = entry->newer;
    } else {
        cache->oldest = entry->newer;
    }
    if (entry->newer >= 0) {
        cache->entries[entry->newer].older = entry->older;
    } else {
        cache->newest = entry->older;
    }
}

static void border_touch(render_material_border_cache *cache, int index)
{
    border_cache_entry *entry = &cache->entries[index];
    entry->older = cache->newest;
    entry->newer = -1;
    if (cache->newest >= 0) {
        cache->entries[cache->newest].newer = index;
    } else {
        cache->oldest = index;
    }
    cache->newest = index;
}

static void border_evict(render_material_border_cache *cache, int index)
{
    border_cache_entry *entry = &cache->entries[index];
    int bucket = border_bucket(entry->incarnation, cache->bucket_count);
    int *link = &cache->buckets[bucket];
    while (*link != index) {
        assert(*link >= 0);
        link = &cache->entries[*link].bucket_next;
    }
    *link = entry->bucket_next;
    border_unlink(cache, index);
    cache->bytes -= (size_t)entry->count * sizeof *entry->rects;
    free(entry->rects);
    *entry = (border_cache_entry){ .bucket_next = cache->vacant };
    cache->vacant = index;
    cache->count--;
}

static int border_bucket_count(int capacity)
{
    int buckets = 2;
    while (buckets < capacity * 2) {
        buckets *= 2;
    }
    return buckets;
}

static size_t border_metadata(int capacity)
{
    return capacity ? (size_t)capacity * sizeof(border_cache_entry) +
                          (size_t)border_bucket_count(capacity) * sizeof(int)
                    : 0;
}

static bool border_grow(render_material_border_cache *cache, size_t rectangles)
{
    int capacity = cache->capacity ? cache->capacity * 2 : 64;
    capacity = capacity < cache->entry_limit ? capacity : cache->entry_limit;
    size_t payload = cache->bytes - border_metadata(cache->capacity);
    while (capacity > cache->capacity &&
           border_metadata(capacity) + payload + rectangles > cache->byte_limit) {
        capacity = cache->capacity + (capacity - cache->capacity) / 2;
    }
    if (capacity <= cache->capacity) {
        return false;
    }
    cache->entries = xrealloc(cache->entries, (size_t)capacity * sizeof *cache->entries);
    for (int i = cache->capacity; i < capacity; i++) {
        cache->entries[i] = (border_cache_entry){ .bucket_next = cache->vacant };
        cache->vacant = i;
    }
    cache->capacity = capacity;
    cache->bucket_count = border_bucket_count(capacity);
    cache->buckets = xrealloc(
        cache->buckets,
        (size_t)cache->bucket_count * sizeof *cache->buckets
    );
    for (int i = 0; i < cache->bucket_count; i++) {
        cache->buckets[i] = -1;
    }
    for (int i = 0; i < capacity; i++) {
        border_cache_entry *entry = &cache->entries[i];
        if (entry->incarnation) {
            int bucket = border_bucket(entry->incarnation, cache->bucket_count);
            entry->bucket_next = cache->buckets[bucket];
            cache->buckets[bucket] = i;
        }
    }
    cache->bytes = payload + border_metadata(capacity);
    return true;
}

render_material_border_view render_material_border_cache_get(
    render_material_border_cache *cache,
    material_occupancy_view mask,
    int reach
)
{
    assert(
        cache && mask.cells && mask.width > 0 && mask.width <= 256 &&
        mask.height > 0 && mask.height <= 256 && isfinite(mask.cell_size) &&
        mask.cell_size > 0 && reach > 0
    );
    int index = mask.incarnation ? border_find(cache, mask.incarnation) : -1;
    if (index >= 0) {
        border_cache_entry *entry = &cache->entries[index];
        if (entry->revision == mask.revision && entry->width == mask.width &&
            entry->height == mask.height && entry->reach == reach) {
            entry->seen = cache->frame;
            border_unlink(cache, index);
            border_touch(cache, index);
            cache->hits++;
            return (render_material_border_view){ entry->rects, entry->count };
        }
        border_evict(cache, index);
    }
    render_material_border_build(&cache->scratch, mask, reach);
    cache->builds++;
    render_material_border_view result = { cache->scratch.rects,
                                           acount(cache->scratch.rects) };
    size_t bytes = (size_t)result.count * sizeof *result.rects;
    if (!mask.incarnation || !cache->entry_limit || bytes > cache->byte_limit ||
        border_metadata(1) > cache->byte_limit - bytes) {
        return result;
    }
    while (cache->bytes > cache->byte_limit - bytes && cache->oldest >= 0) {
        if (cache->frame && cache->entries[cache->oldest].seen == cache->frame) {
            return result;
        }
        border_evict(cache, cache->oldest);
    }
    if (cache->bytes > cache->byte_limit - bytes) {
        return result;
    }
    if (cache->vacant < 0 && !border_grow(cache, bytes)) {
        if (cache->oldest < 0 ||
            (cache->frame && cache->entries[cache->oldest].seen == cache->frame)) {
            return result;
        }
        border_evict(cache, cache->oldest);
    }
    index = cache->vacant;
    border_cache_entry *entry = &cache->entries[index];
    cache->vacant = entry->bucket_next;
    *entry = (border_cache_entry){ .incarnation = mask.incarnation,
                                   .seen = cache->frame,
                                   .revision = mask.revision,
                                   .width = mask.width,
                                   .height = mask.height,
                                   .reach = reach,
                                   .count = result.count };
    if (bytes) {
        entry->rects = xmalloc(bytes);
        memcpy(entry->rects, result.rects, bytes);
    }
    int bucket = border_bucket(entry->incarnation, cache->bucket_count);
    entry->bucket_next = cache->buckets[bucket];
    cache->buckets[bucket] = index;
    border_touch(cache, index);
    cache->count++;
    cache->bytes += bytes;
    assert(cache->count <= cache->entry_limit && cache->bytes <= cache->byte_limit);
    return (render_material_border_view){ entry->rects, entry->count };
}

render_material_border_cache_stats render_material_border_cache_get_stats(
    const render_material_border_cache *cache
)
{
    assert(cache);
    return (
        render_material_border_cache_stats
    ){ cache->count, cache->bytes, cache->hits, cache->builds };
}

void render_material_border_cache_clear(render_material_border_cache *cache)
{
    assert(cache);
    for (int i = 0; i < cache->capacity; i++) {
        free(cache->entries[i].rects);
    }
    free(cache->entries);
    free(cache->buckets);
    render_material_border_free(&cache->scratch);
    int entries = cache->entry_limit;
    size_t bytes = cache->byte_limit;
    *cache = (render_material_border_cache){ .entry_limit = entries,
                                             .byte_limit = bytes,
                                             .vacant = -1,
                                             .oldest = -1,
                                             .newest = -1 };
}

void render_material_border_cache_free(render_material_border_cache *cache)
{
    if (cache) {
        render_material_border_cache_clear(cache);
        free(cache);
    }
}
