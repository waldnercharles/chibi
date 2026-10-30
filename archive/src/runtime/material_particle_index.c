#include "runtime/material_particle_index.h"

#include "support/array.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int64_t x, y;
    int *ids;
    int occupied, next;
} particle_chunk;

typedef struct
{
    int chunk, slot;
} particle_member;

struct material_particle_index
{
    float size;
    particle_chunk *chunks;
    particle_member *members;
    int *occupied;
    int *table;
    int capacity, tombstones, free_chunk;
};

static int64_t coordinate(double value, float size)
{
    double cell = floor(value / size);
    assert(isfinite(cell) && cell >= -0x1p62 && cell <= 0x1p62);
    return (int64_t)cell;
}

static uint64_t hash(int64_t x, int64_t y)
{
    uint64_t h = (uint64_t)x * UINT64_C(0x9e3779b97f4a7c15) ^ (uint64_t)y;
    h ^= h >> 30;
    h *= UINT64_C(0xbf58476d1ce4e5b9);
    h ^= h >> 27;
    h *= UINT64_C(0x94d049bb133111eb);
    return h ^ (h >> 31);
}

static int find(const material_particle_index *index, int64_t x, int64_t y)
{
    if (!index->capacity) {
        return -1;
    }
    int slot = hash(x, y) & (index->capacity - 1);
    while (index->table[slot]) {
        int chunk = index->table[slot] - 1;
        if (chunk >= 0 && index->chunks[chunk].x == x && index->chunks[chunk].y == y) {
            return slot;
        }
        slot = (slot + 1) & (index->capacity - 1);
    }
    return -1;
}

static void insert(material_particle_index *index, int chunk)
{
    particle_chunk *c = &index->chunks[chunk];
    int slot = hash(c->x, c->y) & (index->capacity - 1);
    while (index->table[slot] > 0) {
        slot = (slot + 1) & (index->capacity - 1);
    }
    index->tombstones -= index->table[slot] == -1;
    index->table[slot] = chunk + 1;
}

static void rebuild(material_particle_index *index, int capacity)
{
    free(index->table);
    index->capacity = capacity;
    index->table = xcalloc(capacity, sizeof(*index->table));
    index->tombstones = 0;
    for (int i = 0; i < acount(index->occupied); i++) {
        insert(index, index->occupied[i]);
    }
}

static int chunk_for(material_particle_index *index, fluid_vec position)
{
    int64_t x = coordinate(position.x, index->size);
    int64_t y = coordinate(position.y, index->size);
    int slot = find(index, x, y);
    if (slot >= 0) {
        return index->table[slot] - 1;
    }
    int count = acount(index->occupied);
    if (!index->capacity || (count + index->tombstones + 1) * 2 >= index->capacity) {
        int capacity = index->capacity ? index->capacity : 16;
        if ((count + 1) * 2 >= capacity) {
            capacity *= 2;
        }
        rebuild(index, capacity);
    }
    int chunk = index->free_chunk;
    if (chunk >= 0) {
        index->free_chunk = index->chunks[chunk].next;
    } else {
        chunk = acount(index->chunks);
        apush(index->chunks, (particle_chunk){ 0 });
    }
    particle_chunk *c = &index->chunks[chunk];
    c->x = x;
    c->y = y;
    c->occupied = count;
    apush(index->occupied, chunk);
    insert(index, chunk);
    return chunk;
}

static void remove_member(material_particle_index *index, int id)
{
    particle_member m = index->members[id];
    particle_chunk *c = &index->chunks[m.chunk];
    int moved = alast(c->ids);
    adel(c->ids, m.slot);
    if (moved != id) {
        index->members[moved].slot = m.slot;
    }
    if (acount(c->ids)) {
        return;
    }
    int slot = find(index, c->x, c->y);
    assert(slot >= 0);
    index->table[slot] = -1;
    index->tombstones++;
    int moved_chunk = alast(index->occupied);
    adel(index->occupied, c->occupied);
    index->chunks[moved_chunk].occupied = c->occupied;
    c->next = index->free_chunk;
    index->free_chunk = m.chunk;
}

material_particle_index *material_particle_index_new(float chunk_size)
{
    assert(isfinite(chunk_size) && chunk_size > 0);
    material_particle_index *index = xcalloc(1, sizeof(*index));
    index->size = chunk_size;
    index->free_chunk = -1;
    return index;
}

void material_particle_index_free(material_particle_index *index)
{
    for (int i = 0; i < acount(index->chunks); i++) {
        afree(index->chunks[i].ids);
    }
    afree(index->chunks);
    afree(index->members);
    afree(index->occupied);
    free(index->table);
    free(index);
}

void material_particle_index_clear(material_particle_index *index)
{
    aclear(index->members);
    aclear(index->occupied);
    index->free_chunk = -1;
    for (int i = acount(index->chunks) - 1; i >= 0; i--) {
        aclear(index->chunks[i].ids);
        index->chunks[i].next = index->free_chunk;
        index->free_chunk = i;
    }
    if (index->capacity) {
        memset(index->table, 0, index->capacity * sizeof(*index->table));
    }
    index->tombstones = 0;
}

void material_particle_index_add(material_particle_index *index, int id, fluid_vec position)
{
    assert(id == acount(index->members));
    int chunk = chunk_for(index, position);
    int slot = acount(index->chunks[chunk].ids);
    apush(index->chunks[chunk].ids, id);
    apush(index->members, ((particle_member){ chunk, slot }));
}

void material_particle_index_move(material_particle_index *index, int id, fluid_vec position)
{
    assert(id >= 0 && id < acount(index->members));
    particle_chunk *c = &index->chunks[index->members[id].chunk];
    if (c->x == coordinate(position.x, index->size) &&
        c->y == coordinate(position.y, index->size)) {
        return;
    }
    remove_member(index, id);
    int chunk = chunk_for(index, position);
    index->members[id] = (particle_member){ chunk, acount(index->chunks[chunk].ids) };
    apush(index->chunks[chunk].ids, id);
}

void material_particle_index_swap_remove(material_particle_index *index, int id, int last_id)
{
    assert(last_id == acount(index->members) - 1 && id >= 0 && id <= last_id);
    remove_member(index, id);
    if (id != last_id) {
        particle_member m = index->members[last_id];
        index->chunks[m.chunk].ids[m.slot] = id;
        index->members[id] = m;
    }
    apop(index->members);
}

static int compare_ids(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

static void append_chunk(const material_particle_index *index, int chunk, int **ids)
{
    const int *members = index->chunks[chunk].ids;
    for (int i = 0; i < acount(members); i++) {
        apush(*ids, members[i]);
    }
}

void material_particle_index_query(
    const material_particle_index *index,
    const fluid_region *regions,
    int region_count,
    int **ids
)
{
    assert(region_count >= 0);
    aclear(*ids);
    for (int r = 0; r < region_count; r++) {
        fluid_region region = regions[r];
        assert(
            isfinite(region.width) && isfinite(region.height) &&
            region.width >= 0 && region.height >= 0
        );
        int64_t x0 = coordinate(region.x, index->size);
        int64_t y0 = coordinate(region.y, index->size);
        int64_t x1 = coordinate((double)region.x + region.width, index->size);
        int64_t y1 = coordinate((double)region.y + region.height, index->size);
        double cells = ((double)x1 - x0 + 1) * ((double)y1 - y0 + 1);
        if (cells > (double)acount(index->occupied) * 4) {
            for (int i = 0; i < acount(index->occupied); i++) {
                int chunk = index->occupied[i];
                particle_chunk *c = &index->chunks[chunk];
                if (c->x >= x0 && c->x <= x1 && c->y >= y0 && c->y <= y1) {
                    append_chunk(index, chunk, ids);
                }
            }
            continue;
        }
        for (int64_t y = y0; y <= y1; y++) {
            for (int64_t x = x0; x <= x1; x++) {
                int slot = find(index, x, y);
                if (slot >= 0) {
                    append_chunk(index, index->table[slot] - 1, ids);
                }
            }
        }
    }
    int count = acount(*ids);
    if (!count) {
        return;
    }
    qsort(*ids, count, sizeof(**ids), compare_ids);
    int unique = 1;
    for (int i = 1; i < count; i++) {
        if ((*ids)[i] != (*ids)[unique - 1]) {
            (*ids)[unique++] = (*ids)[i];
        }
    }
    asetlen(*ids, unique);
}
