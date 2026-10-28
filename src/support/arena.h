#ifndef CHIBI_ARENA_H
#define CHIBI_ARENA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct arena_block arena_block;
typedef struct arena_slot arena_slot;
typedef struct arena_slabs arena_slabs;
typedef struct arena_cleanup arena_cleanup;
typedef struct arena_resource arena_resource;
typedef void (*arena_marker)(void *user, const void *address);
typedef void (*arena_tracer)(const void *value, const void *shape, arena_marker mark, void *user);
/* Recent copies an arena remembers; values often alternate a few layouts. */
#define ARENA_COPY_CACHE 8
typedef struct arena_copy
{
    const void *source;
    const void *copy; /* Held by the arena. */
} arena_copy;
typedef struct arena
{
    arena_block *first;
    arena_block *current;
    arena_slabs *slabs; /* Collectable values, by size class. */
    size_t allocated;   /* Value and new resource bytes since the last sweep. */
    size_t kept;        /* Value and resource bytes the last sweep kept. */
    arena_cleanup *cleanups; /* Ordinary allocations with external ownership. */
    arena_resource *resources; /* Unique resources retained by live allocations. */
    size_t resource_capacity, resource_count, retained_bytes;
    size_t cleanup_count;
    size_t external; /* Retained external payloads, including ownership overhead. */
    arena_copy copies[ARENA_COPY_CACHE];
    int next_copy; /* The entry the next remembered copy replaces. */
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
/* Transfers one retained reference to an allocation. Attach at most once, using
 * its starting address and non-null resource/release. Release runs exactly once
 * when that allocation retires, before its storage is reused or freed. It must
 * not mutate this arena or depend on other arena allocations remaining live.
 * Shared resource identities must report the same retained_bytes; accounting
 * charges their bytes once per arena until the last attachment retires. */
void arena_set_cleanup(
    arena *a,
    const void *allocation,
    void (*release)(void *),
    void *resource,
    size_t retained_bytes
);
/* Replace an allocation's attachment, releasing the previous reference. The
 * caller transfers one already-retained reference to the replacement, even
 * when its resource identity is unchanged. A null release/resource pair removes
 * the attachment. An unchanged resource keeps its accounting entry; changing
 * its byte size requires this to be its only attachment. Release callbacks
 * obey arena_set_cleanup's restrictions. */
void arena_replace_cleanup(
    arena *a,
    const void *allocation,
    void (*release)(void *),
    void *resource,
    size_t retained_bytes
);
/* Charge an external retained allocation to a collectable owner. Growth counts
 * toward normal collection admission; release it from that owner's cleanup. */
void arena_external_resize(arena *, size_t previous, size_t bytes);
/* One mark and sweep over collectable arenas whose values may point into one
 * another. A mark finds its value by address in time logarithmic in the
 * arenas' slabs, and each marked value is traced once. The arenas must outlive
 * the collection. */
typedef struct arena_collection
{
    arena **arenas;
    int count;
    arena_slot **pending; /* Marked values not traced yet. */
} arena_collection;
void arena_collect_begin(arena_collection *, arena **arenas, int count);
/* An arena_marker whose user is the collection. */
void arena_collect_mark(void *collection, const void *address);
/* Thread-local count of collection marks, a measure of collection work. */
uint64_t arena_collect_marks();
/* Traces what the marks reach and sweeps the rest. */
void arena_collect_end(arena_collection *);
/* The copy of source that a remembers, or NULL. An arena remembers its last
 * ARENA_COPY_CACHE copies until a reset, free or collection. Source's address may since hold
 * another object, so the caller checks that the copy still matches it. */
const void *arena_copy_find(const arena *a, const void *source);
void arena_copy_remember(arena *a, const void *source, const void *copy);
void arena_usage(const arena *a, size_t *bytes, size_t *capacity);
/* Backing allocations, including block and tracked-allocation headers. */
size_t arena_storage_bytes(const arena *);
/* Ordinary arena allocation rejects before growing beyond retained budget. */
void *arena_alloc_bounded(arena *, size_t bytes, size_t budget);

#endif
