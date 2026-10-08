#include "support/arena.h"

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
    bool live, marked, traced;
    _Alignas(max_align_t) unsigned char data[];
};
static _Thread_local uint64_t block_allocations;
/* Keep bounded reuse across safepoints even when payload sizes keep changing. */
#define ARENA_VALUE_CACHE_BYTES ((size_t)4 * 1024)

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

void arena_reset(arena *a)
{
    for (arena_allocation *value = a->allocations; value; value = value->next) {
        value->live = false;
        value->size = 0;
        value->trace = NULL;
        value->shape = NULL;
    }
    for (arena_block *block = a->first; block; block = block->next) {
        block->used = 0;
    }
    a->current = a->first;
}
void arena_init_collectable(arena *a)
{
    *a = (arena){ .collectable = true };
}
static void *allocate_value(arena *a, size_t size)
{
    arena_allocation *best = NULL;
    for (arena_allocation *value = a->allocations; value; value = value->next) {
        if (!value->live && value->capacity >= size && value->capacity - size <= size &&
            (!best || value->capacity < best->capacity)) {
            best = value;
        }
    }
    if (!best) {
        assert(size <= SIZE_MAX - sizeof *best);
        best = xcalloc(1, sizeof *best + size);
        block_allocations++;
        best->capacity = size;
        best->next = a->allocations;
        a->allocations = best;
    }
    best->size = size;
    best->live = true;
    best->marked = false;
    best->traced = false;
    best->trace = NULL;
    best->shape = NULL;
    return best->data;
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
    arena_allocation *value = a->allocations;
    while (value) {
        arena_allocation *next = value->next;
        free(value);
        value = next;
    }
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
void arena_collect_begin(arena *a)
{
    for (arena_allocation *value = a->allocations; value; value = value->next) {
        value->marked = false;
        value->traced = false;
    }
}
bool arena_mark(arena *a, const void *address)
{
    uintptr_t pointer = (uintptr_t)address;
    for (arena_allocation *value = a->allocations; value; value = value->next) {
        uintptr_t begin = (uintptr_t)value->data;
        if (value->live && pointer >= begin && pointer - begin < value->size) {
            if (value->marked) {
                return false;
            }
            value->marked = true;
            return true;
        }
    }
    return false;
}
bool arena_trace_marked(arena *a, arena_marker mark, void *user)
{
    bool traced = false;
    for (arena_allocation *value = a->allocations; value; value = value->next) {
        if (value->live && value->marked && !value->traced) {
            value->traced = true;
            traced = true;
            if (value->trace) {
                value->trace(value->data, value->shape, mark, user);
            }
        }
    }
    return traced;
}
void arena_sweep(arena *a)
{
    size_t cached = 0;
    for (arena_allocation *value = a->allocations; value; value = value->next) {
        if (value->live && !value->marked) {
            value->live = false;
            value->size = 0;
            value->trace = NULL;
            value->shape = NULL;
        }
        if (!value->live) {
            cached += value->capacity;
        }
    }
    arena_allocation **link = &a->allocations;
    while (*link && cached > ARENA_VALUE_CACHE_BYTES) {
        arena_allocation *value = *link;
        if (value->live) {
            link = &value->next;
            continue;
        }
        cached -= value->capacity;
        *link = value->next;
        free(value);
    }
}
void arena_usage(const arena *a, size_t *bytes, size_t *capacity)
{
    *bytes = 0;
    *capacity = 0;
    for (const arena_allocation *value = a->allocations; value; value = value->next) {
        *bytes += value->live ? value->size : 0;
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
