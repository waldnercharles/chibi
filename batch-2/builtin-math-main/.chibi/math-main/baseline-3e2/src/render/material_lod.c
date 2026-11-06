#include "render/material_lod.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Distinct cell sizes whose level the renderer remembers. */
#define LOD_SCALES 8

typedef struct
{
    uint64_t incarnation, seen;
    unsigned revision;
    /* levels[k - 1] is the mask at level k; level 0 is the caller's. */
    material_mask *levels[RENDER_MATERIAL_LOD_LEVELS - 1];
} lod_entry;

typedef struct
{
    float cell_size;
    int level;
} lod_scale;

struct render_material_lod
{
    lod_entry *entries;
    /* Open addressing on incarnation: entry index + 1, or 0 when vacant. */
    int *slots;
    int slot_count;
    uint64_t frame;
    lod_scale scales[LOD_SCALES];
    int scale_count, next_scale;
};

int render_material_lod_pick(float cell_pixels, int previous)
{
    assert(cell_pixels > 0 && previous >= 0 && previous < RENDER_MATERIAL_LOD_LEVELS);
    float ideal = log2f(RENDER_MATERIAL_LOD_PIXELS / cell_pixels);
    if (ideal > previous - 1 - RENDER_MATERIAL_LOD_HYSTERESIS &&
        ideal <= previous + RENDER_MATERIAL_LOD_HYSTERESIS) {
        return previous;
    }
    float level = ceilf(ideal);
    return level <= 0 ? 0
         : level >= RENDER_MATERIAL_LOD_LEVELS - 1 ? RENDER_MATERIAL_LOD_LEVELS - 1
                                                   : (int)level;
}

render_material_lod *render_material_lod_new()
{
    return xcalloc(1, sizeof(render_material_lod));
}

static void release_levels(lod_entry *entry)
{
    for (int k = 0; k < RENDER_MATERIAL_LOD_LEVELS - 1; k++) {
        if (entry->levels[k]) {
            material_mask_free(entry->levels[k]);
            free(entry->levels[k]);
            entry->levels[k] = NULL;
        }
    }
}

void render_material_lod_free(render_material_lod *lod)
{
    if (!lod) {
        return;
    }
    for (int i = 0; i < acount(lod->entries); i++) {
        release_levels(&lod->entries[i]);
    }
    afree(lod->entries);
    free(lod->slots);
    free(lod);
}

static int slot_of(const render_material_lod *lod, uint64_t incarnation)
{
    unsigned mask = (unsigned)lod->slot_count - 1;
    unsigned slot = (unsigned)(incarnation * UINT64_C(0x9e3779b97f4a7c15) >> 32) & mask;
    while (lod->slots[slot] &&
           lod->entries[lod->slots[slot] - 1].incarnation != incarnation) {
        slot = (slot + 1) & mask;
    }
    return (int)slot;
}

/* Sizes the index for twice the entries and fills it. */
static void index_entries(render_material_lod *lod)
{
    int count = 64;
    while (count < 2 * acount(lod->entries)) {
        count *= 2;
    }
    if (count != lod->slot_count) {
        free(lod->slots);
        lod->slots = xmalloc((size_t)count * sizeof *lod->slots);
        lod->slot_count = count;
    }
    memset(lod->slots, 0, (size_t)count * sizeof *lod->slots);
    for (int i = 0; i < acount(lod->entries); i++) {
        lod->slots[slot_of(lod, lod->entries[i].incarnation)] = i + 1;
    }
}

void render_material_lod_begin(render_material_lod *lod)
{
    lod->frame++;
    int kept = 0;
    for (int i = 0; i < acount(lod->entries); i++) {
        lod_entry *entry = &lod->entries[i];
        if (entry->seen + 1 < lod->frame) {
            release_levels(entry);
            continue;
        }
        lod->entries[kept++] = *entry;
    }
    asetlen(lod->entries, kept);
    index_entries(lod);
}

static lod_scale *scale_for(render_material_lod *lod, float cell_size)
{
    for (int i = 0; i < lod->scale_count; i++) {
        if (lod->scales[i].cell_size == cell_size) {
            return &lod->scales[i];
        }
    }
    /* A new size takes the oldest entry once the table is full. */
    int i = lod->scale_count < LOD_SCALES ? lod->scale_count++ : lod->next_scale;
    lod->next_scale = (i + 1) % LOD_SCALES;
    lod->scales[i] = (lod_scale){ cell_size, 0 };
    return &lod->scales[i];
}

int render_material_lod_level(render_material_lod *lod, float cell_size, float pixels_per_unit)
{
    assert(cell_size > 0 && pixels_per_unit > 0);
    lod_scale *scale = scale_for(lod, cell_size);
    scale->level = render_material_lod_pick(cell_size * pixels_per_unit, scale->level);
    return scale->level;
}

static lod_entry *entry_for(render_material_lod *lod, const material_mask *mask)
{
    if (!lod->slot_count) {
        index_entries(lod);
    }
    int slot = slot_of(lod, mask->incarnation);
    if (!lod->slots[slot]) {
        apush(lod->entries, ((lod_entry){ .incarnation = mask->incarnation, .revision = mask->revision }));
        lod->slots[slot] = acount(lod->entries);
        if (2 * acount(lod->entries) > lod->slot_count) {
            index_entries(lod);
        }
        return &alast(lod->entries);
    }
    lod_entry *entry = &lod->entries[lod->slots[slot] - 1];
    if (entry->revision != mask->revision) {
        release_levels(entry);
        entry->revision = mask->revision;
    }
    return entry;
}

const material_mask *render_material_lod_get(render_material_lod *lod, const material_mask *mask, int level)
{
    assert(level >= 0 && level < RENDER_MATERIAL_LOD_LEVELS);
    /* Terrain neighbors share a limit, including partial edge chunks. Never
     * reduce one side beyond the neighborhood's immutable sampling support. */
    if (mask->halo_radius && level > mask->lod_limit) {
        level = mask->lod_limit;
    }
    int coarsest = 0;
    while (coarsest < level && (mask->width >> coarsest) % 2 == 0 &&
           (mask->height >> coarsest) % 2 == 0) {
        coarsest++;
    }
    if (!coarsest) {
        return mask;
    }
    assert(mask->incarnation);
    lod_entry *entry = entry_for(lod, mask);
    entry->seen = lod->frame;
    const material_mask *finer = mask;
    for (int k = 0; k < coarsest; k++) {
        if (!entry->levels[k]) {
            entry->levels[k] = xmalloc(sizeof *entry->levels[k]);
            *entry->levels[k] = material_mask_reduce(finer);
        }
        finer = entry->levels[k];
    }
    return finer;
}
