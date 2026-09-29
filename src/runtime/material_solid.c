#include "runtime/material_solid.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

/* World units per side of an index bucket; a terrain tile spans about two. */
static const float BUCKET = 128;

typedef struct
{
    material_solid solid;
    float c, s, half_width, half_height;
} placed_solid;

typedef struct
{
    uint64_t key;
    int solid;
} bucket_member;

typedef struct
{
    uint64_t key;
    int start, count; /* Zero count marks an empty slot. */
} solid_bucket;

struct material_solids
{
    placed_solid *placed;
    material_solid *items;
    bucket_member *members;
    solid_bucket *table;
    int capacity;
};

material_solids *material_solids_new()
{
    return xcalloc(1, sizeof(material_solids));
}

void material_solids_free(material_solids *solids)
{
    afree(solids->placed);
    afree(solids->items);
    afree(solids->members);
    free(solids->table);
    free(solids);
}

static uint64_t bucket_key(int x, int y)
{
    return (uint64_t)(uint32_t)y << 32 | (uint32_t)x;
}

static unsigned bucket_slot(uint64_t key, int capacity)
{
    return (unsigned)((key * UINT64_C(0x9e3779b97f4a7c15)) >> 40) &
           (unsigned)(capacity - 1);
}

static int compare_members(const void *left, const void *right)
{
    const bucket_member *a = left, *b = right;
    if (a->key != b->key) {
        return a->key < b->key ? -1 : 1;
    }
    return a->solid - b->solid;
}

static void index_solid(material_solids *solids, int index)
{
    placed_solid p = solids->placed[index];
    float ex = fabsf(p.c) * p.half_width + fabsf(p.s) * p.half_height;
    float ey = fabsf(p.s) * p.half_width + fabsf(p.c) * p.half_height;
    fluid_vec center = p.solid.position;
    int x0 = (int)floorf((center.x - ex) / BUCKET),
        x1 = (int)floorf((center.x + ex) / BUCKET);
    int y0 = (int)floorf((center.y - ey) / BUCKET),
        y1 = (int)floorf((center.y + ey) / BUCKET);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            apush(solids->members, ((bucket_member){ bucket_key(x, y), index }));
        }
    }
}

void material_solids_set(material_solids *solids, const material_solid *items, int count)
{
    aclear(solids->placed);
    aclear(solids->items);
    aclear(solids->members);
    for (int i = 0; i < count; i++) {
        material_solid solid = items[i];
        assert(solid.mask && solid.mask->width > 0);
        assert(isfinite(solid.position.x + solid.position.y + solid.angle));
        apush(solids->items, solid);
        apush(
            solids->placed,
            ((placed_solid){ solid,
                             cosf(solid.angle),
                             sinf(solid.angle),
                             solid.mask->width * solid.mask->cell_size * .5f,
                             solid.mask->height * solid.mask->cell_size * .5f })
        );
        index_solid(solids, i);
    }
    int members = acount(solids->members);
    if (members > 1) {
        qsort(solids->members, (size_t)members, sizeof *solids->members, compare_members);
    }
    int capacity = 16;
    while (capacity < members * 2) {
        capacity *= 2;
    }
    if (capacity > solids->capacity) {
        free(solids->table);
        solids->table = xmalloc((size_t)capacity * sizeof *solids->table);
        solids->capacity = capacity;
    }
    for (int i = 0; i < solids->capacity; i++) {
        solids->table[i].count = 0;
    }
    for (int start = 0; start < members;) {
        uint64_t key = solids->members[start].key;
        int end = start + 1;
        while (end < members && solids->members[end].key == key) {
            end++;
        }
        unsigned slot = bucket_slot(key, solids->capacity);
        while (solids->table[slot].count) {
            slot = (slot + 1) & (unsigned)(solids->capacity - 1);
        }
        solids->table[slot] = (solid_bucket){ key, start, end - start };
        start = end;
    }
}

const material_solid *material_solids_items(const material_solids *solids, int *count)
{
    *count = acount(solids->items);
    return solids->items;
}

static const solid_bucket *find_bucket(const material_solids *solids, fluid_vec point)
{
    if (!acount(solids->members)) {
        return NULL;
    }
    uint64_t key = bucket_key((int)floorf(point.x / BUCKET), (int)floorf(point.y / BUCKET));
    unsigned slot = bucket_slot(key, solids->capacity);
    while (solids->table[slot].count) {
        if (solids->table[slot].key == key) {
            return &solids->table[slot];
        }
        slot = (slot + 1) & (unsigned)(solids->capacity - 1);
    }
    return NULL;
}

static fluid_vec to_local(const placed_solid *p, fluid_vec point)
{
    float x = point.x - p->solid.position.x, y = point.y - p->solid.position.y;
    return (fluid_vec){ p->c * x + p->s * y, -p->s * x + p->c * y };
}

static bool covers(const placed_solid *p, fluid_vec local)
{
    return fabsf(local.x) <= p->half_width && fabsf(local.y) <= p->half_height;
}

material_solid_sample material_solids_sample(const material_solids *solids, fluid_vec point)
{
    material_solid_sample result = { MATERIAL_SOLID_FAR, { 0 }, -1 };
    const solid_bucket *bucket = find_bucket(solids, point);
    if (!bucket) {
        return result;
    }
    for (int i = bucket->start; i < bucket->start + bucket->count; i++) {
        const placed_solid *p = &solids->placed[solids->members[i].solid];
        fluid_vec local = to_local(p, point);
        if (!covers(p, local)) {
            continue;
        }
        float gx, gy;
        float distance = material_mask_sample(p->solid.mask, local.x, local.y, &gx, &gy);
        if (distance >= result.distance) {
            continue;
        }
        float length = hypotf(gx, gy);
        result.distance = distance;
        result.solid = solids->members[i].solid;
        result.normal = length > 1e-8f
                          ? (fluid_vec){ (p->c * gx - p->s * gy) / length,
                                         (p->s * gx + p->c * gy) / length }
                          : (fluid_vec){ 0 };
    }
    return result;
}

/* Distance along the ray from point to the far side of its bucket. */
static float bucket_exit(fluid_vec point, fluid_vec direction)
{
    float bx = floorf(point.x / BUCKET), by = floorf(point.y / BUCKET);
    float tx = direction.x > 0 ? ((bx + 1) * BUCKET - point.x) / direction.x
             : direction.x < 0 ? (bx * BUCKET - point.x) / direction.x
                               : INFINITY;
    float ty = direction.y > 0 ? ((by + 1) * BUCKET - point.y) / direction.y
             : direction.y < 0 ? (by * BUCKET - point.y) / direction.y
                               : INFINITY;
    return fminf(tx, ty);
}

bool material_solids_blocked(const material_solids *solids, fluid_vec a, fluid_vec b)
{
    fluid_vec d = { b.x - a.x, b.y - a.y };
    float length = hypotf(d.x, d.y);
    if (length < 1e-6f) {
        return false;
    }
    fluid_vec direction = { d.x / length, d.y / length };
    /* Steps stay below half a cell near surfaces, so a one-cell wall, whose
     * negative band is a cell wide, is always sampled. */
    for (float t = 1e-3f; t < length;) {
        fluid_vec point = { a.x + direction.x * t, a.y + direction.y * t };
        material_solid_sample s = material_solids_sample(solids, point);
        if (s.distance < 0) {
            return true;
        }
        if (s.solid < 0) {
            t += find_bucket(solids, point) ? 1 : bucket_exit(point, direction) + 1e-3f;
            continue;
        }
        float cell = solids->placed[s.solid].solid.mask->cell_size;
        t += fminf(fmaxf(s.distance * .8f, cell * .4f), cell * 2);
    }
    return false;
}

int material_solids_find(const material_solids *solids, fluid_vec point, fluid_vec *local)
{
    const solid_bucket *bucket = find_bucket(solids, point);
    for (int i = 0; bucket && i < bucket->count; i++) {
        int index = solids->members[bucket->start + i].solid;
        const placed_solid *p = &solids->placed[index];
        fluid_vec at = to_local(p, point);
        if (covers(p, at) && material_mask_contains(p->solid.mask, at.x, at.y)) {
            *local = at;
            return index;
        }
    }
    return -1;
}
