#ifndef CHIBI_CATALOG_INDEX_H
#define CHIBI_CATALOG_INDEX_H

#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

/* Construction-only identity lookup. Catalog arrays retain declaration order;
 * indices, not pointers into their growing storage, identify accepted rows. */
typedef struct
{
    uint64_t id;
    int ordinal; /* Original array index + 1; zero means an unused bucket. */
} catalog_index_entry;
typedef struct
{
    catalog_index_entry *entries;
    int count, capacity;
} catalog_index;

static inline int catalog_index_slot(const catalog_index *index, uint64_t id)
{
    uint64_t hash = id;
    hash = (hash ^ (hash >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    hash = (hash ^ (hash >> 27)) * UINT64_C(0x94d049bb133111eb);
    hash ^= hash >> 31;
    int slot = (int)(hash & (uint64_t)(index->capacity - 1));
    while (index->entries[slot].ordinal && index->entries[slot].id != id) {
        slot = (slot + 1) & (index->capacity - 1);
    }
    return slot;
}
static inline int catalog_index_find(const catalog_index *index, uint64_t id)
{
    return index->capacity
             ? index->entries[catalog_index_slot(index, id)].ordinal - 1
             : -1;
}
/* Returns the existing row, or -1 after recording a previously unseen ID.
 * An explicit occupied bit in ordinal permits ID zero where callers allow it. */
static inline int catalog_index_add(catalog_index *index, uint64_t id, int row)
{
    assert(row >= 0 && row < INT_MAX);
    int found = catalog_index_find(index, id);
    if (found >= 0) {
        return found;
    }
    if (index->count >= index->capacity - index->capacity / 4) {
        assert(index->capacity <= INT_MAX / 2);
        int capacity = index->capacity ? 2 * index->capacity : 16;
        catalog_index grown = {
            .entries = xcalloc((size_t)capacity, sizeof *grown.entries),
            .count = index->count,
            .capacity = capacity,
        };
        for (int i = 0; i < index->capacity; i++) {
            catalog_index_entry entry = index->entries[i];
            if (entry.ordinal) {
                grown.entries[catalog_index_slot(&grown, entry.id)] = entry;
            }
        }
        free(index->entries);
        *index = grown;
    }
    index->entries[catalog_index_slot(index, id)] = (catalog_index_entry){ id, row + 1 };
    index->count++;
    return -1;
}
static inline void catalog_index_free(catalog_index *index)
{
    free(index->entries);
    *index = (catalog_index){ 0 };
}

#endif
