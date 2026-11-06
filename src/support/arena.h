#ifndef CHIBI_ARENA_H
#define CHIBI_ARENA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct arena_block arena_block;
typedef struct arena_allocation arena_allocation;
typedef struct arena_page arena_page;
typedef void (*arena_marker)(void *user, const void *address);
typedef void (*arena_tracer)(const void *value, const void *shape, arena_marker mark, void *user);
typedef void (*arena_finalizer)(void *value);
typedef struct arena
{
    arena_block *first;
    arena_block *current;
    arena_allocation *allocations; /* Live collectable values. */
    arena_allocation *unused;      /* Swept values kept for reuse. */
    arena_page *pages;             /* Live values by each page they touch. */
    size_t page_capacity;          /* Index slots, a power of two. */
    size_t page_count;             /* Occupied index slots. */
    size_t allocated;              /* Collectable bytes since the last sweep. */
    size_t kept;                   /* Collectable bytes the last sweep kept. */
    size_t external; /* Retained external payloads, including ownership overhead. */
    const void *copied_from; /* Source of the last copy remembered. */
    const void *copy;        /* That copy, which this arena holds. */
    bool collectable;
} arena;

/* Optional eager allocation; a zero-initialized arena allocates on first use. */
void arena_init(arena *a, size_t cap);
/* Invalidates allocations and retains blocks for reuse. */
void arena_reset(arena *a);
/* Aligned allocations remain at stable addresses until reset or free. */
void *arena_alloc(arena *a, size_t size);
void arena_free(arena *a);
/* Whether an address is within a live value or a block payload. Collectable
 * arenas answer without walking their values. */
bool arena_contains(const arena *a, const void *address);
/* Thread-local backing-block allocation count; excludes allocations within blocks. */
uint64_t arena_block_allocations();
/* Opt-in nonmoving value storage. Ordinary arenas retain their bump allocator.
 * Values no collection marks become reusable; live addresses stay stable. */
void arena_init_collectable(arena *a);
void arena_set_trace(arena *a, const void *value, arena_tracer trace, const void *shape);
/* Releases external ownership before a collectable allocation is swept or
 * its arena is reset/freed. The callback cannot allocate in this arena. */
void arena_set_finalizer(arena *, const void *value, arena_finalizer);
/* Charge an external retained allocation to a collectable owner. Growth counts
 * toward normal collection admission; release it from that owner's finalizer. */
void arena_external_resize(arena *, size_t previous, size_t bytes);
/* One mark and sweep over collectable arenas whose values may point into one
 * another. A mark finds its value by address in logarithmic time, and each
 * marked value is traced once. The arenas must outlive the collection. */
typedef struct arena_collection
{
    arena **arenas;
    int count;
    arena_allocation **live;    /* Every live value, by address. */
    arena_allocation **pending; /* Marked values not traced yet. */
} arena_collection;
void arena_collect_begin(arena_collection *, arena **arenas, int count);
/* An arena_marker whose user is the collection. */
void arena_collect_mark(void *collection, const void *address);
/* Thread-local count of collection marks, a measure of collection work. */
uint64_t arena_collect_marks();
/* Traces what the marks reach and sweeps the rest. */
void arena_collect_end(arena_collection *);
/* The copy of source that a remembers, or NULL. An arena remembers its last
 * copy until a reset, free or collection. Source's address may since hold
 * another object, so the caller checks that the copy still matches it. */
const void *arena_copy_find(const arena *a, const void *source);
void arena_copy_remember(arena *a, const void *source, const void *copy);
void arena_usage(const arena *a, size_t *bytes, size_t *capacity);
/* Backing allocations, including block and tracked-allocation headers. */
size_t arena_storage_bytes(const arena *);
/* Ordinary arena allocation rejects before growing beyond retained budget. */
void *arena_alloc_bounded(arena *, size_t bytes, size_t budget);

#endif
