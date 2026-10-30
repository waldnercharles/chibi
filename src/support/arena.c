#include "support/arena.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct arena_block
{
    arena_block *next;
    size_t cap;
    size_t used;
    _Alignas(max_align_t) unsigned char data[];
};
struct arena_slot
{
    unsigned char *data;
    size_t size;
    arena_tracer trace;
    const void *shape;
    void (*release)(void *);
    void *resource;
    int next_free; /* Its slab's next free slot, or -1. */
    bool live, marked;
};
/* Equal slots of one size class, or one large value, in one allocation. */
typedef struct arena_slab
{
    unsigned char *data;
    size_t slot, bytes; /* Bytes per slot, and in the whole allocation. */
    int count, used;
    int free;       /* The first free slot, or -1. */
    int size_class; /* ARENA_CLASSES for a large value. */
    arena_slot slots[];
} arena_slab;
struct arena_cleanup
{
    arena_cleanup *next;
    const void *allocation;
    void (*release)(void *);
    void *resource;
};
struct arena_resource
{
    void *resource;
    size_t references, bytes;
};
static _Thread_local uint64_t block_allocations;
/* Slots step by 16 bytes up to 128, then by quarters of each power of two up
 * to ARENA_LARGE_BYTES; a larger value has a slab of its own. */
#define ARENA_SMALL_CLASSES 8
#define ARENA_CLASSES 40
#define ARENA_RECENT_SLABS 256
#define ARENA_LARGE_BYTES ((size_t)32 * 1024)
#define ARENA_SLAB_BYTES ((size_t)64 * 1024)
/* A class's first slab is small, so arenas holding a few values stay small,
 * and each later one doubles up to ARENA_SLAB_BYTES. */
#define ARENA_FIRST_SLAB_BYTES ((size_t)1024)
/* Copies a table holds before it first grows. */
#define ARENA_FIRST_COPIES 16
struct arena_slabs
{
    arena_slab **slabs; /* By address. */
    /* slabs[i]->data, so a search reads one flat array instead of a slab
     * header per step. A collection's marks, cleanups and new slabs search it. */
    uintptr_t *starts;
    arena_slab **open[ARENA_CLASSES]; /* Slabs with a free slot. */
    size_t next_bytes[ARENA_CLASSES]; /* The class's next slab size. */
    arena_slot *last; /* The latest value, which a trace or cleanup names next. */
    /* Slabs that marks found, by the 4 KiB page of the address that named
     * them: a trace names values near the ones it named before. Only marks
     * fill it, and a sweep empties it before it frees any slab. */
    arena_slab *recent[ARENA_RECENT_SLABS];
};

uint64_t arena_block_allocations()
{
    return block_allocations;
}

static arena_block *new_block(size_t cap)
{
    assert(cap <= SIZE_MAX - sizeof(arena_block));
    arena_block *block = xmalloc(sizeof *block + cap);
    block_allocations++;
    block->next = NULL;
    block->cap = cap;
    block->used = 0;
    return block;
}

void arena_init(arena *a, size_t cap)
{
    *a = (arena){ 0 };
    a->first = new_block(cap ? cap : 256);
    a->current = a->first;
}

static int size_class(size_t size)
{
    if (size <= 16 * ARENA_SMALL_CLASSES) {
        return size ? (int)((size + 15) / 16) - 1 : 0;
    }
    int power = 7;
    while ((size - 1) >> (power + 1)) {
        power++;
    }
    int quarter = (int)((size - 1) >> (power - 2)) & 3;
    return ARENA_SMALL_CLASSES + (power - 7) * 4 + quarter;
}
static size_t class_bytes(int size_class)
{
    if (size_class < ARENA_SMALL_CLASSES) {
        return (size_t)(size_class + 1) * 16;
    }
    int power = 7 + (size_class - ARENA_SMALL_CLASSES) / 4;
    int quarter = (size_class - ARENA_SMALL_CLASSES) % 4;
    return (size_t)(5 + quarter) << (power - 2);
}
/* The number of starts at or below address. */
static int upper_bound(const uintptr_t *starts, int count, uintptr_t address)
{
    int low = 0, high = count;
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (starts[middle] <= address) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}
static arena_slab *new_slab(arena_slabs *s, int size_class, size_t slot, int count)
{
    size_t align = _Alignof(max_align_t);
    size_t header = sizeof(arena_slab) + (size_t)count * sizeof(arena_slot);
    header = (header + align - 1) / align * align;
    assert(slot <= (SIZE_MAX - header) / (size_t)count);
    arena_slab *slab = xmalloc(header + slot * (size_t)count);
    block_allocations++;
    *slab = (arena_slab){ .data = (unsigned char *)slab + header,
                          .slot = slot,
                          .bytes = header + slot * (size_t)count,
                          .count = count,
                          .size_class = size_class };
    for (int i = 0; i < count; i++) {
        slab->slots[i] = (arena_slot){ .data = slab->data + slot * (size_t)i,
                                       .next_free = i + 1 < count ? i + 1 : -1 };
    }
    int slabs = acount(s->starts),
        at = upper_bound(s->starts, slabs, (uintptr_t)slab->data);
    apush(s->slabs, slab);
    apush(s->starts, (uintptr_t)slab->data);
    memmove(&s->slabs[at + 1], &s->slabs[at], (size_t)(slabs - at) * sizeof *s->slabs);
    memmove(&s->starts[at + 1], &s->starts[at], (size_t)(slabs - at) * sizeof *s->starts);
    s->slabs[at] = slab;
    s->starts[at] = (uintptr_t)slab->data;
    return slab;
}
static bool slab_holds(const arena_slab *slab, uintptr_t address)
{
    return address - (uintptr_t)slab->data < slab->slot * (size_t)slab->count;
}
static int recent_index(uintptr_t address)
{
    return (int)((address >> 12) & (ARENA_RECENT_SLABS - 1));
}
/* The slab whose slots hold address. Addresses outside every slab, such as
 * static names, miss without a search. */
static arena_slab *find_slab(const arena_slabs *s, uintptr_t address)
{
    arena_slab *recent = s->recent[recent_index(address)];
    if (recent && slab_holds(recent, address)) {
        return recent;
    }
    int count = acount(s->starts);
    if (!count || address < s->starts[0]) {
        return NULL;
    }
    int at = address >= s->starts[count - 1]
               ? count
               : upper_bound(s->starts, count, address);
    arena_slab *found = s->slabs[at - 1];
    return slab_holds(found, address) ? found : NULL;
}
/* The live value whose bytes hold address, or that starts at it. */
static arena_slot *slot_in(arena_slab *slab, uintptr_t address, bool start)
{
    arena_slot *value = &slab->slots[(address - (uintptr_t)slab->data) / slab->slot];
    uintptr_t begin = (uintptr_t)value->data;
    if (!value->live || (start ? address != begin : address - begin >= value->size)) {
        return NULL;
    }
    return value;
}
static arena_slot *find_slot(const arena *a, uintptr_t address, bool start)
{
    arena_slab *slab = a->slabs ? find_slab(a->slabs, address) : NULL;
    return slab ? slot_in(slab, address, start) : NULL;
}
static arena_slot *started(const arena *a, const void *address)
{
    arena_slot *last = a->slabs ? a->slabs->last : NULL;
    if (last && last->live && last->data == address) {
        return last;
    }
    return find_slot(a, (uintptr_t)address, true);
}
static size_t resource_slot(const arena *a, const void *resource)
{
    uint64_t hash = (uint64_t)(uintptr_t)resource * UINT64_C(0x9E3779B97F4A7C15);
    return (size_t)(hash >> 32) & (a->resource_capacity - 1);
}
static void resize_resources(arena *a, size_t capacity)
{
    arena_resource *old = a->resources;
    size_t old_capacity = a->resource_capacity;
    a->resources = xcalloc(capacity, sizeof *a->resources);
    a->resource_capacity = capacity;
    for (size_t i = 0; i < old_capacity; i++) {
        if (!old[i].resource) {
            continue;
        }
        size_t slot = resource_slot(a, old[i].resource);
        while (a->resources[slot].resource) {
            slot = (slot + 1) & (capacity - 1);
        }
        a->resources[slot] = old[i];
    }
    free(old);
}
static void retain_resource(arena *a, void *resource, size_t bytes)
{
    if (2 * (a->resource_count + 1) > a->resource_capacity) {
        resize_resources(a, a->resource_capacity ? a->resource_capacity * 2 : 8);
    }
    size_t slot = resource_slot(a, resource);
    while (a->resources[slot].resource && a->resources[slot].resource != resource) {
        slot = (slot + 1) & (a->resource_capacity - 1);
    }
    arena_resource *entry = &a->resources[slot];
    if (entry->resource) {
        assert(entry->bytes == bytes);
        entry->references++;
        return;
    }
    assert(bytes <= SIZE_MAX - a->retained_bytes);
    *entry = (arena_resource){ resource, 1, bytes };
    a->resource_count++;
    a->retained_bytes += bytes;
    if (a->collectable) {
        assert(bytes <= SIZE_MAX - a->allocated);
        a->allocated += bytes;
    }
}
static void release_resource(arena *a, void (*release)(void *), void *resource)
{
    size_t mask = a->resource_capacity - 1, slot = resource_slot(a, resource);
    while (a->resources[slot].resource != resource) {
        assert(a->resources[slot].resource);
        slot = (slot + 1) & mask;
    }
    if (!--a->resources[slot].references) {
        a->retained_bytes -= a->resources[slot].bytes;
        a->resource_count--;
        /* Remove the key without leaving tombstones across collections. */
        for (size_t next = (slot + 1) & mask; a->resources[next].resource;
             next = (next + 1) & mask) {
            size_t home = resource_slot(a, a->resources[next].resource);
            if (((next - home) & mask) >= ((next - slot) & mask)) {
                a->resources[slot] = a->resources[next];
                slot = next;
            }
        }
        a->resources[slot] = (arena_resource){ 0 };
    }
    release(resource);
}
static void resize_resource(arena *a, void *resource, size_t bytes)
{
    size_t slot = resource_slot(a, resource);
    while (a->resources[slot].resource != resource) {
        assert(a->resources[slot].resource);
        slot = (slot + 1) & (a->resource_capacity - 1);
    }
    arena_resource *entry = &a->resources[slot];
    if (entry->bytes == bytes) {
        return;
    }
    assert(entry->references == 1);
    assert(bytes <= SIZE_MAX - (a->retained_bytes - entry->bytes));
    a->retained_bytes = a->retained_bytes - entry->bytes + bytes;
    if (a->collectable && bytes > entry->bytes) {
        size_t added = bytes - entry->bytes;
        assert(added <= SIZE_MAX - a->allocated);
        a->allocated += added;
    }
    entry->bytes = bytes;
}
static void free_cleanups(arena *a)
{
    while (a->cleanups) {
        arena_cleanup *cleanup = a->cleanups;
        a->cleanups = cleanup->next;
        release_resource(a, cleanup->release, cleanup->resource);
        free(cleanup);
    }
    a->cleanup_count = 0;
}
static void fit_resources(arena *a)
{
    if (!a->resource_count) {
        free(a->resources);
        a->resources = NULL;
        a->resource_capacity = 0;
        return;
    }
    size_t capacity = 8;
    while (capacity < 4 * a->resource_count) {
        capacity *= 2;
    }
    if (capacity < a->resource_capacity) {
        resize_resources(a, capacity);
    }
}
static void retire(arena *a, arena_slab *slab, int index)
{
    arena_slot *value = &slab->slots[index];
    if (value->release) {
        release_resource(a, value->release, value->resource);
    }
    *value = (arena_slot){ .data = value->data, .next_free = slab->free };
    slab->free = index;
    slab->used--;
}
/* Retires the values sweep_slab finds unmarked, or every value, and frees an
 * empty slab unless it is the one its class keeps for the next allocation. */
static bool sweep_slab(arena *a, arena_slab *slab, bool everything, bool *spare)
{
    for (int i = 0; slab->used && i < slab->count; i++) {
        arena_slot *value = &slab->slots[i];
        if (!value->live) {
            continue;
        }
        if (value->marked && !everything) {
            value->marked = false;
            a->kept += value->size;
            continue;
        }
        retire(a, slab, i);
    }
    if (slab->used) {
        return true;
    }
    if (slab->size_class == ARENA_CLASSES || spare[slab->size_class]) {
        free(slab);
        return false;
    }
    spare[slab->size_class] = true;
    return true;
}
static void sweep_slabs(arena *a, bool everything)
{
    arena_slabs *s = a->slabs;
    if (!s) {
        return;
    }
    s->last = NULL;
    memset(s->recent, 0, sizeof s->recent);
    bool spare[ARENA_CLASSES] = { 0 };
    int kept = 0;
    for (int i = 0; i < acount(s->slabs); i++) {
        if (sweep_slab(a, s->slabs[i], everything, spare)) {
            s->starts[kept] = s->starts[i];
            s->slabs[kept++] = s->slabs[i];
        }
    }
    asetlen(s->slabs, kept);
    asetlen(s->starts, kept);
    for (int c = 0; c < ARENA_CLASSES; c++) {
        asetlen(s->open[c], 0);
    }
    for (int i = 0; i < kept; i++) {
        arena_slab *slab = s->slabs[i];
        if (slab->size_class < ARENA_CLASSES && slab->used < slab->count) {
            apush(s->open[slab->size_class], slab);
        }
    }
}
static void forget_copy(arena *a)
{
    if (a->copy_count) {
        memset(a->copies, 0, a->copy_capacity * sizeof *a->copies);
        a->copy_count = 0;
    }
}
/* The slot that holds source, or the empty slot where it goes. */
static arena_copy *copy_slot(const arena *a, const void *source)
{
    uint64_t hash = (uint64_t)(uintptr_t)source * UINT64_C(0x9e3779b97f4a7c15);
    size_t i = (size_t)(hash >> 32) & (a->copy_capacity - 1);
    while (a->copies[i].source && a->copies[i].source != source) {
        i = (i + 1) & (a->copy_capacity - 1);
    }
    return &a->copies[i];
}
const void *arena_copy_find(const arena *a, const void *source)
{
    return source && a->copy_count ? copy_slot(a, source)->copy : NULL;
}
void arena_copy_remember(arena *a, const void *source, const void *copy)
{
    if ((a->copy_count + 1) * 2 > a->copy_capacity) {
        arena_copy *old = a->copies;
        size_t capacity = a->copy_capacity;
        a->copy_capacity = capacity ? capacity * 2 : ARENA_FIRST_COPIES;
        a->copies = xcalloc(a->copy_capacity, sizeof *a->copies);
        for (size_t i = 0; i < capacity; i++) {
            if (old[i].source) {
                *copy_slot(a, old[i].source) = old[i];
            }
        }
        free(old);
    }
    /* A stale copy of source, which its caller rejected, gives way. */
    arena_copy *slot = copy_slot(a, source);
    a->copy_count += !slot->source;
    *slot = (arena_copy){ source, copy };
}
void arena_reset(arena *a)
{
    forget_copy(a);
    free_cleanups(a);
    sweep_slabs(a, true);
    fit_resources(a);
    a->allocated = 0;
    a->kept = 0;
    for (arena_block *block = a->first; block; block = block->next) {
        block->used = 0;
    }
    a->current = a->first;
}
void arena_init_collectable(arena *a)
{
    *a = (arena){ .collectable = true };
}
/* Takes a free slot of the size's class, or gives a large value a slab. */
static void *allocate_value(arena *a, size_t size)
{
    if (!a->slabs) {
        a->slabs = xcalloc(1, sizeof *a->slabs);
    }
    arena_slabs *s = a->slabs;
    arena_slab *slab;
    if (size > ARENA_LARGE_BYTES) {
        size_t align = _Alignof(max_align_t);
        slab = new_slab(s, ARENA_CLASSES, (size + align - 1) / align * align, 1);
    } else {
        int c = size_class(size);
        if (!acount(s->open[c])) {
            size_t slot = class_bytes(c), bytes = s->next_bytes[c];
            bytes = bytes ? bytes : ARENA_FIRST_SLAB_BYTES;
            s->next_bytes[c] = bytes < ARENA_SLAB_BYTES ? bytes * 2 : bytes;
            apush(s->open[c], new_slab(s, c, slot, bytes > slot ? (int)(bytes / slot) : 1));
        }
        slab = s->open[c][acount(s->open[c]) - 1];
    }
    arena_slot *value = &slab->slots[slab->free];
    slab->free = value->next_free;
    if (++slab->used == slab->count && slab->size_class < ARENA_CLASSES) {
        (void)apop(s->open[slab->size_class]);
    }
    value->size = size;
    value->live = true;
    memset(value->data, 0, size);
    a->allocated += size;
    s->last = value;
    return value->data;
}
void *arena_alloc(arena *a, size_t size)
{
    if (a->collectable) {
        return allocate_value(a, size);
    }
    if (!a->first) {
        arena_init(a, size > 256 ? size : 256);
    }
    for (;;) {
        arena_block *block = a->current;
        size_t align = _Alignof(max_align_t);
        size_t padding = (align - block->used % align) % align;
        size_t remaining = block->cap - block->used;
        if (padding <= remaining && size <= remaining - padding) {
            void *result = block->data + block->used + padding;
            block->used += padding + size;
            return result;
        }
        if (!block->next) {
            size_t cap = block->cap <= SIZE_MAX / 2 ? block->cap * 2 : size;
            block->next = new_block(cap > size ? cap : size);
        }
        a->current = block->next;
    }
}

void arena_free(arena *a)
{
    free_cleanups(a);
    if (a->slabs) {
        sweep_slabs(a, true);
        for (int i = 0; i < acount(a->slabs->slabs); i++) {
            free(a->slabs->slabs[i]);
        }
        afree(a->slabs->slabs);
        afree(a->slabs->starts);
        for (int c = 0; c < ARENA_CLASSES; c++) {
            afree(a->slabs->open[c]);
        }
        free(a->slabs);
    }
    free(a->resources);
    free(a->copies);
    arena_block *block = a->first;
    while (block) {
        arena_block *next = block->next;
        free(block);
        block = next;
    }
    *a = (arena){ 0 };
}

bool arena_contains(const arena *a, const void *address)
{
    uintptr_t value = (uintptr_t)address;
    if (find_slot(a, value, false)) {
        return true;
    }
    for (const arena_block *block = a->first; block; block = block->next) {
        uintptr_t begin = (uintptr_t)block->data;
        if (value >= begin && value - begin < block->used) {
            return true;
        }
    }
    return false;
}
void arena_set_trace(arena *a, const void *data, arena_tracer trace, const void *shape)
{
    if (!a->collectable) {
        return;
    }
    arena_slot *value = started(a, data);
    assert(value);
    value->trace = trace;
    value->shape = shape;
}
void arena_external_resize(arena *a, size_t previous, size_t bytes)
{
    assert(
        a->collectable && a->external >= previous &&
        bytes <= SIZE_MAX - (a->external - previous)
    );
    a->external = a->external - previous + bytes;
    if (bytes > previous) {
        size_t growth = bytes - previous;
        assert(growth <= SIZE_MAX - a->allocated);
        a->allocated += growth;
    }
}
void arena_set_cleanup(arena *a, const void *allocation, void (*release)(void *), void *resource, size_t retained_bytes)
{
    assert(release && resource);
    if (a->collectable) {
        arena_slot *value = started(a, allocation);
        assert(value && !value->release);
        value->release = release;
        value->resource = resource;
    } else {
        assert(arena_contains(a, allocation));
        arena_cleanup *cleanup = xmalloc(sizeof *cleanup);
        *cleanup = (arena_cleanup){ a->cleanups, allocation, release, resource };
        a->cleanups = cleanup;
        a->cleanup_count++;
    }
    retain_resource(a, resource, retained_bytes);
}
void arena_replace_cleanup(
    arena *a,
    const void *allocation,
    void (*release)(void *),
    void *resource,
    size_t retained_bytes
)
{
    assert((release != NULL) == (resource != NULL));
    assert(resource || !retained_bytes);
    if (a->collectable) {
        arena_slot *value = started(a, allocation);
        assert(value);
        void (*old_release)(void *) = value->release;
        void *old_resource = value->resource;
        if (old_release && old_resource == resource) {
            resize_resource(a, resource, retained_bytes);
            value->release = release;
            old_release(old_resource);
            return;
        }
        value->release = NULL;
        value->resource = NULL;
        if (old_release) {
            release_resource(a, old_release, old_resource);
        }
    } else {
        assert(arena_contains(a, allocation));
        arena_cleanup **link = &a->cleanups;
        while (*link && (*link)->allocation != allocation) {
            link = &(*link)->next;
        }
        if (*link) {
            arena_cleanup *cleanup = *link;
            if (cleanup->resource == resource) {
                resize_resource(a, resource, retained_bytes);
                void (*old_release)(void *) = cleanup->release;
                cleanup->release = release;
                old_release(resource);
                return;
            }
            *link = cleanup->next;
            a->cleanup_count--;
            release_resource(a, cleanup->release, cleanup->resource);
            free(cleanup);
        }
    }
    if (resource) {
        arena_set_cleanup(a, allocation, release, resource, retained_bytes);
    }
}
void arena_collect_begin(arena_collection *c, arena **arenas, int count)
{
    *c = (arena_collection){ .arenas = arenas, .count = count };
    for (int i = 0; i < count; i++) {
        assert(arenas[i]->collectable);
    }
}
bool arena_collect_mark_held(arena_collection *c, const void *address)
{
    for (int i = 0; i < c->count; i++) {
        arena_slabs *s = c->arenas[i]->slabs;
        arena_slab *slab = s ? find_slab(s, (uintptr_t)address) : NULL;
        if (!slab) {
            continue;
        }
        s->recent[recent_index((uintptr_t)address)] = slab;
        arena_slot *value = slot_in(slab, (uintptr_t)address, false);
        if (!value) {
            return false;
        }
        if (!value->marked) {
            value->marked = true;
            apush(c->pending, value);
        }
        return true;
    }
    return false;
}
void arena_collect_mark(void *collection, const void *address)
{
    (void)arena_collect_mark_held(collection, address);
}
void arena_collect_end(arena_collection *c)
{
    while (acount(c->pending)) {
        arena_slot *value = apop(c->pending);
        if (value->trace) {
            value->trace(value->data, value->shape, arena_collect_mark, c);
        }
    }
    for (int i = 0; i < c->count; i++) {
        arena *a = c->arenas[i];
        forget_copy(a);
        a->allocated = 0;
        a->kept = 0;
        sweep_slabs(a, false);
        a->kept += a->retained_bytes + a->external;
        fit_resources(a);
    }
    afree(c->pending);
}
void arena_usage(const arena *a, size_t *bytes, size_t *capacity)
{
    *bytes = a->retained_bytes + a->external;
    *capacity = a->retained_bytes + a->external;
    for (int i = 0; a->slabs && i < acount(a->slabs->slabs); i++) {
        const arena_slab *slab = a->slabs->slabs[i];
        *capacity += slab->slot * (size_t)slab->count;
        for (int j = 0; j < slab->count; j++) {
            *bytes += slab->slots[j].size;
        }
    }
    for (const arena_block *block = a->first; block; block = block->next) {
        *bytes += block->used;
        *capacity += block->cap;
    }
}
size_t arena_storage_bytes(const arena *a)
{
    size_t bytes = 0;
    if (a->slabs) {
        bytes += sizeof *a->slabs +
                 (size_t)acap(a->slabs->slabs) * sizeof *a->slabs->slabs;
        for (int i = 0; i < acount(a->slabs->slabs); i++) {
            bytes += a->slabs->slabs[i]->bytes;
        }
    }
    for (const arena_block *block = a->first; block; block = block->next) {
        bytes += sizeof *block + block->cap;
    }
    return bytes + a->retained_bytes + a->external +
           a->resource_capacity * sizeof *a->resources +
           a->copy_capacity * sizeof *a->copies +
           a->cleanup_count * sizeof(arena_cleanup);
}
void *arena_alloc_bounded(arena *a, size_t size, size_t budget)
{
    assert(!a->collectable);
    size_t retained = arena_storage_bytes(a);
    if (retained > budget) {
        return NULL;
    }
    if (!a->first) {
        size_t capacity = size > 256 ? size : 256;
        if (budget - retained < sizeof(arena_block) ||
            capacity > budget - retained - sizeof(arena_block)) {
            return NULL;
        }
        return arena_alloc(a, size);
    }
    for (const arena_block *block = a->current; block; block = block->next) {
        size_t align = _Alignof(max_align_t);
        size_t padding = (align - block->used % align) % align;
        size_t remaining = block->cap - block->used;
        if (padding <= remaining && size <= remaining - padding) {
            return arena_alloc(a, size);
        }
        if (block->next) {
            continue;
        }
        size_t capacity = block->cap <= SIZE_MAX / 2 ? block->cap * 2 : size;
        if (capacity < size) {
            capacity = size;
        }
        if (budget - retained < sizeof(arena_block) ||
            capacity > budget - retained - sizeof(arena_block)) {
            return NULL;
        }
    }
    return arena_alloc(a, size);
}
