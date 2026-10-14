#include "support/arena.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

struct arena_block
{
    arena_block *next;
    size_t cap;
    size_t used;
    _Alignas(max_align_t) unsigned char data[];
};
struct arena_allocation
{
    arena_allocation *next;
    size_t size, capacity;
    arena_tracer trace;
    const void *shape;
    bool live, marked;
    _Alignas(max_align_t) unsigned char data[];
};
static _Thread_local uint64_t block_allocations, collect_marks;
/* Keep bounded reuse across safepoints even when payload sizes keep changing. */
#define ARENA_VALUE_CACHE_BYTES ((size_t)4 * 1024)

uint64_t arena_block_allocations()
{
    return block_allocations;
}
uint64_t arena_collect_marks()
{
    return collect_marks;
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

static void retire(arena *a, arena_allocation *value)
{
    value->live = false;
    value->size = 0;
    value->trace = NULL;
    value->shape = NULL;
    value->next = a->unused;
    a->unused = value;
}
void arena_reset(arena *a)
{
    while (a->allocations) {
        arena_allocation *value = a->allocations;
        a->allocations = value->next;
        retire(a, value);
    }
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
/* Reuses the closest fitting swept value, so allocation never walks the live
 * values. */
static void *allocate_value(arena *a, size_t size)
{
    arena_allocation **best = NULL;
    for (arena_allocation **link = &a->unused; *link; link = &(*link)->next) {
        arena_allocation *value = *link;
        if (value->capacity >= size && value->capacity - size <= size &&
            (!best || value->capacity < (*best)->capacity)) {
            best = link;
        }
    }
    arena_allocation *value;
    if (best) {
        value = *best;
        *best = value->next;
    } else {
        assert(size <= SIZE_MAX - sizeof *value);
        value = xcalloc(1, sizeof *value + size);
        block_allocations++;
        value->capacity = size;
    }
    value->next = a->allocations;
    a->allocations = value;
    a->allocated += size;
    value->size = size;
    value->live = true;
    value->marked = false;
    value->trace = NULL;
    value->shape = NULL;
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

static void free_values(arena_allocation *value)
{
    while (value) {
        arena_allocation *next = value->next;
        free(value);
        value = next;
    }
}
void arena_free(arena *a)
{
    free_values(a->allocations);
    free_values(a->unused);
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
    for (const arena_allocation *allocation = a->allocations; allocation;
         allocation = allocation->next) {
        uintptr_t begin = (uintptr_t)allocation->data;
        if (allocation->live && value >= begin && value - begin < allocation->size) {
            return true;
        }
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
    for (arena_allocation *value = a->allocations; value; value = value->next) {
        if (value->data == data && value->live) {
            value->trace = trace;
            value->shape = shape;
            return;
        }
    }
    assert(false);
}
static int by_address(const void *a, const void *b)
{
    uintptr_t x = (uintptr_t)(*(arena_allocation *const *)a)->data;
    uintptr_t y = (uintptr_t)(*(arena_allocation *const *)b)->data;
    return (x > y) - (x < y);
}
void arena_collect_begin(arena_collection *c, arena **arenas, int count)
{
    *c = (arena_collection){ .arenas = arenas, .count = count };
    for (int i = 0; i < count; i++) {
        assert(arenas[i]->collectable);
        for (arena_allocation *value = arenas[i]->allocations; value;
             value = value->next) {
            value->marked = false;
            apush(c->live, value);
        }
    }
    asort(c->live, by_address);
}
/* The live value whose bytes hold address, if any. */
static arena_allocation *find(const arena_collection *c, uintptr_t address)
{
    int low = 0, high = acount(c->live);
    while (low < high) {
        int middle = low + (high - low) / 2;
        if ((uintptr_t)c->live[middle]->data <= address) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (!low) {
        return NULL;
    }
    arena_allocation *value = c->live[low - 1];
    return address - (uintptr_t)value->data < value->size ? value : NULL;
}
void arena_collect_mark(void *collection, const void *address)
{
    arena_collection *c = collection;
    collect_marks++;
    arena_allocation *value = find(c, (uintptr_t)address);
    if (!value || value->marked) {
        return;
    }
    value->marked = true;
    apush(c->pending, value);
}
static void sweep(arena *a)
{
    a->allocated = 0;
    a->kept = 0;
    for (arena_allocation **link = &a->allocations; *link;) {
        arena_allocation *value = *link;
        if (value->marked) {
            a->kept += value->size;
            link = &value->next;
            continue;
        }
        *link = value->next;
        retire(a, value);
    }
    size_t cached = 0;
    for (arena_allocation *value = a->unused; value; value = value->next) {
        cached += value->capacity;
    }
    while (a->unused && cached > ARENA_VALUE_CACHE_BYTES) {
        arena_allocation *value = a->unused;
        cached -= value->capacity;
        a->unused = value->next;
        free(value);
    }
}
void arena_collect_end(arena_collection *c)
{
    while (acount(c->pending)) {
        arena_allocation *value = apop(c->pending);
        if (value->trace) {
            value->trace(value->data, value->shape, arena_collect_mark, c);
        }
    }
    for (int i = 0; i < c->count; i++) {
        sweep(c->arenas[i]);
    }
    afree(c->live);
    afree(c->pending);
}
void arena_usage(const arena *a, size_t *bytes, size_t *capacity)
{
    *bytes = 0;
    *capacity = 0;
    for (const arena_allocation *value = a->allocations; value; value = value->next) {
        *bytes += value->size;
        *capacity += value->capacity;
    }
    for (const arena_allocation *value = a->unused; value; value = value->next) {
        *capacity += value->capacity;
    }
    for (const arena_block *block = a->first; block; block = block->next) {
        *bytes += block->used;
        *capacity += block->cap;
    }
}
size_t arena_storage_bytes(const arena *a)
{
    size_t bytes = 0;
    for (const arena_allocation *value = a->allocations; value; value = value->next) {
        bytes += sizeof *value + value->capacity;
    }
    for (const arena_allocation *value = a->unused; value; value = value->next) {
        bytes += sizeof *value + value->capacity;
    }
    for (const arena_block *block = a->first; block; block = block->next) {
        bytes += sizeof *block + block->cap;
    }
    return bytes;
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
