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
    bool live, marked, traced;
    _Alignas(max_align_t) unsigned char data[];
};
static _Thread_local uint64_t block_allocations;
static _Thread_local uint64_t mark_search_steps;
/* Keep bounded reuse across safepoints even when payload sizes keep changing. */
#define ARENA_VALUE_CACHE_BYTES ((size_t)4 * 1024)

uint64_t arena_block_allocations()
{
    return block_allocations;
}
uint64_t arena_mark_search_steps()
{
    return mark_search_steps;
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

static void include_address(arena *a, const arena_allocation *value)
{
    uintptr_t begin = (uintptr_t)value->data;
    uintptr_t end = (uintptr_t)(value->data + value->size);
    if (begin < a->address_begin) {
        a->address_begin = begin;
    }
    if (end > a->address_end) {
        a->address_end = end;
    }
}
static void retire(arena *a, arena_allocation *value)
{
    a->index_valid = false;
    value->live = false;
    value->size = 0;
    value->trace = NULL;
    value->shape = NULL;
    value->next = a->unused;
    a->unused = value;
}
void arena_reset(arena *a)
{
    a->address_begin = UINTPTR_MAX;
    a->address_end = 0;
    while (a->allocations) {
        arena_allocation *value = a->allocations;
        a->allocations = value->next;
        retire(a, value);
    }
    for (arena_block *block = a->first; block; block = block->next) {
        block->used = 0;
    }
    a->current = a->first;
}
void arena_init_collectable(arena *a)
{
    *a = (arena){ .address_begin = UINTPTR_MAX, .collectable = true };
}
/* Reuses the closest fitting swept value, so allocation never walks the live
 * values. */
static void *allocate_value(arena *a, size_t size)
{
    a->index_valid = false;
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
    value->size = size;
    value->live = true;
    value->marked = false;
    value->traced = false;
    value->trace = NULL;
    value->shape = NULL;
    include_address(a, value);
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
    afree(a->index);
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
void arena_collect_begin(arena *a)
{
    for (arena_allocation *value = a->allocations; value; value = value->next) {
        value->marked = false;
        value->traced = false;
    }
}
static int allocation_address_compare(const void *left, const void *right)
{
    mark_search_steps++;
    const arena_allocation *a = *(arena_allocation *const *)left;
    const arena_allocation *b = *(arena_allocation *const *)right;
    uintptr_t x = (uintptr_t)a->data, y = (uintptr_t)b->data;
    return (x > y) - (x < y);
}
static void index_allocations(arena *a)
{
    if (a->index_valid) {
        return;
    }
    aclear(a->index);
    for (arena_allocation *value = a->allocations; value; value = value->next) {
        apush(a->index, value);
    }
    asort(a->index, allocation_address_compare);
    a->index_valid = true;
}
bool arena_mark(arena *a, const void *address)
{
    if (!address || !a->allocations) {
        return false;
    }
    uintptr_t pointer = (uintptr_t)address;
    mark_search_steps += 2;
    if (pointer < a->address_begin || pointer >= a->address_end) {
        return false;
    }
    index_allocations(a);
    int low = 0, high = acount(a->index);
    while (low < high) {
        int middle = low + (high - low) / 2;
        mark_search_steps++;
        if ((uintptr_t)a->index[middle]->data <= pointer) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    arena_allocation *value = a->index[low - 1];
    mark_search_steps++;
    if (pointer - (uintptr_t)value->data >= value->size || value->marked) {
        return false;
    }
    value->marked = true;
    return true;
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
    a->address_begin = UINTPTR_MAX;
    a->address_end = 0;
    for (arena_allocation **link = &a->allocations; *link;) {
        arena_allocation *value = *link;
        if (value->marked) {
            include_address(a, value);
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
    size_t bytes = a->index
                     ? sizeof(dyna_hdr) + (size_t)acap(a->index) * sizeof *a->index
                     : 0;
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
