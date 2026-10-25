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
struct arena_allocation
{
    arena_allocation *next;
    size_t size, capacity;
    arena_tracer trace;
    const void *shape;
    void (*release)(void *);
    void *resource;
    bool live, marked;
    _Alignas(max_align_t) unsigned char data[];
};
struct arena_page
{
    uintptr_t page;
    arena_allocation *value;
};
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
static _Thread_local uint64_t block_allocations, collect_marks;
/* Keep bounded reuse across safepoints even when payload sizes keep changing. */
#define ARENA_VALUE_CACHE_BYTES ((size_t)4 * 1024)
#define ARENA_PAGE_SHIFT 12
#define ARENA_MIN_PAGES 8

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

static size_t page_slot(const arena *a, uintptr_t page)
{
    uint64_t hash = (uint64_t)page * UINT64_C(0x9E3779B97F4A7C15);
    return (size_t)(hash >> 32) & (a->page_capacity - 1);
}
static uintptr_t first_page(const arena_allocation *value)
{
    return (uintptr_t)value->data >> ARENA_PAGE_SHIFT;
}
static uintptr_t last_page(const arena_allocation *value, size_t size)
{
    uintptr_t begin = (uintptr_t)value->data;
    return (size ? begin + size - 1 : begin) >> ARENA_PAGE_SHIFT;
}
static void insert_page(arena *a, uintptr_t page, arena_allocation *value)
{
    size_t slot = page_slot(a, page);
    while (a->pages[slot].value) {
        slot = (slot + 1) & (a->page_capacity - 1);
    }
    a->pages[slot] = (arena_page){ page, value };
    a->page_count++;
}
static void resize_pages(arena *a, size_t capacity)
{
    arena_page *old = a->pages;
    size_t old_capacity = a->page_capacity;
    assert(capacity >= 2 * a->page_count);
    a->pages = xcalloc(capacity, sizeof *a->pages);
    a->page_capacity = capacity;
    a->page_count = 0;
    for (size_t i = 0; i < old_capacity; i++) {
        if (old[i].value) {
            insert_page(a, old[i].page, old[i].value);
        }
    }
    free(old);
}
static void clear_pages(arena *a)
{
    if (a->pages) {
        memset(a->pages, 0, a->page_capacity * sizeof *a->pages);
    }
    a->page_count = 0;
}
/* Indexes a live value under every page its bytes touch, so finding the value
 * that holds an address never walks the live values. */
static void index_value(arena *a, arena_allocation *value)
{
    for (uintptr_t page = first_page(value); page <= last_page(value, value->size);
         page++) {
        if (2 * (a->page_count + 1) > a->page_capacity) {
            resize_pages(a, a->page_capacity ? 2 * a->page_capacity : ARENA_MIN_PAGES);
        }
        insert_page(a, page, value);
    }
}
static void remove_page(arena *a, uintptr_t page, const arena_allocation *value)
{
    size_t mask = a->page_capacity - 1, slot = page_slot(a, page);
    while (a->pages[slot].page != page || a->pages[slot].value != value) {
        assert(a->pages[slot].value);
        slot = (slot + 1) & mask;
    }
    /* Backward-shift deletion: move each later entry whose home slot does not
     * lie after the hole, so every probe sequence stays unbroken. */
    for (size_t next = (slot + 1) & mask; a->pages[next].value;
         next = (next + 1) & mask) {
        size_t home = page_slot(a, a->pages[next].page);
        if (((next - home) & mask) >= ((next - slot) & mask)) {
            a->pages[slot] = a->pages[next];
            slot = next;
        }
    }
    a->pages[slot] = (arena_page){ 0 };
    a->page_count--;
}
static void unindex_value(arena *a, const arena_allocation *value)
{
    for (uintptr_t page = first_page(value); page <= last_page(value, value->size);
         page++) {
        remove_page(a, page, value);
    }
}
/* After a sweep, keeps room for the live values and the cached ones a later
 * allocation may reuse, so steady reuse never reallocates the index, and
 * releases the rest of a collected peak. */
static void fit_pages(arena *a)
{
    size_t needed = a->page_count;
    for (const arena_allocation *value = a->unused; value; value = value->next) {
        needed += last_page(value, value->capacity) - first_page(value) + 1;
    }
    size_t capacity = ARENA_MIN_PAGES;
    while (capacity < 4 * needed) {
        capacity *= 2;
    }
    if (capacity < a->page_capacity) {
        resize_pages(a, capacity);
    }
}
/* The live value whose bytes hold address, or that starts at it. */
static arena_allocation *indexed(const arena *a, uintptr_t address, bool start)
{
    if (!a->page_count) {
        return NULL;
    }
    size_t mask = a->page_capacity - 1;
    uintptr_t page = address >> ARENA_PAGE_SHIFT;
    for (size_t slot = page_slot(a, page); a->pages[slot].value;
         slot = (slot + 1) & mask) {
        arena_allocation *value = a->pages[slot].value;
        uintptr_t begin = (uintptr_t)value->data;
        if (a->pages[slot].page == page &&
            (start ? address == begin : address - begin < value->size)) {
            return value;
        }
    }
    return NULL;
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
static void retire(arena *a, arena_allocation *value)
{
    if (value->release) {
        release_resource(a, value->release, value->resource);
        value->release = NULL;
        value->resource = NULL;
    }
    value->live = false;
    value->size = 0;
    value->trace = NULL;
    value->shape = NULL;
    value->next = a->unused;
    a->unused = value;
}
static void forget_copy(arena *a)
{
    memset(a->copies, 0, sizeof a->copies);
    a->next_copy = 0;
}
const void *arena_copy_find(const arena *a, const void *source)
{
    for (int i = 0; source && i < ARENA_COPY_CACHE; i++) {
        if (a->copies[i].source == source) {
            return a->copies[i].copy;
        }
    }
    return NULL;
}
void arena_copy_remember(arena *a, const void *source, const void *copy)
{
    /* A stale copy of source, which its caller rejected, gives way first. */
    for (int i = 0; i < ARENA_COPY_CACHE; i++) {
        if (a->copies[i].source == source) {
            a->copies[i].copy = copy;
            return;
        }
    }
    a->copies[a->next_copy] = (arena_copy){ source, copy };
    a->next_copy = (a->next_copy + 1) % ARENA_COPY_CACHE;
}
void arena_reset(arena *a)
{
    forget_copy(a);
    free_cleanups(a);
    clear_pages(a);
    while (a->allocations) {
        arena_allocation *value = a->allocations;
        a->allocations = value->next;
        retire(a, value);
    }
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
    index_value(a, value);
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

static void free_values(arena *a, arena_allocation *value)
{
    while (value) {
        arena_allocation *next = value->next;
        if (value->release) {
            release_resource(a, value->release, value->resource);
        }
        free(value);
        value = next;
    }
}
void arena_free(arena *a)
{
    free_cleanups(a);
    free_values(a, a->allocations);
    free_values(a, a->unused);
    free(a->resources);
    free(a->pages);
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
    if (indexed(a, value, false)) {
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
    arena_allocation *value = indexed(a, (uintptr_t)data, true);
    assert(value);
    value->trace = trace;
    value->shape = shape;
}
void arena_set_cleanup(arena *a, const void *allocation, void (*release)(void *), void *resource, size_t retained_bytes)
{
    assert(release && resource);
    if (a->collectable) {
        arena_allocation *value = indexed(a, (uintptr_t)allocation, true);
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
        arena_allocation *value = indexed(a, (uintptr_t)allocation, true);
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
        unindex_value(a, value);
        retire(a, value);
    }
    a->kept += a->retained_bytes;
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
    fit_pages(a);
    fit_resources(a);
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
        forget_copy(c->arenas[i]);
        sweep(c->arenas[i]);
    }
    afree(c->live);
    afree(c->pending);
}
void arena_usage(const arena *a, size_t *bytes, size_t *capacity)
{
    *bytes = a->retained_bytes;
    *capacity = a->retained_bytes;
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
size_t arena_cleanup_bytes(const arena *a)
{
    return a->cleanup_count * sizeof(arena_cleanup);
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
    return bytes + a->page_capacity * sizeof *a->pages + a->retained_bytes +
           a->resource_capacity * sizeof *a->resources +
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
