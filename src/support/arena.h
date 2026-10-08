#ifndef CHIBI_ARENA_H
#define CHIBI_ARENA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct arena_block arena_block;
typedef struct arena_allocation arena_allocation;
typedef void (*arena_marker)(void *user, const void *address);
typedef void (*arena_tracer)(const void *value, const void *shape, arena_marker mark, void *user);
typedef struct arena
{
    arena_block *first;
    arena_block *current;
    arena_allocation *allocations;
    bool collectable;
} arena;

/* Optional eager allocation; a zero-initialized arena allocates on first use. */
void arena_init(arena *a, size_t cap);
/* Invalidates allocations and retains blocks for reuse. */
void arena_reset(arena *a);
/* Aligned allocations remain at stable addresses until reset or free. */
void *arena_alloc(arena *a, size_t size);
void arena_free(arena *a);
/* Whether an address is within a currently allocated block payload. */
bool arena_contains(const arena *a, const void *address);
/* Thread-local backing-block allocation count; excludes allocations within blocks. */
uint64_t arena_block_allocations();
/* Opt-in nonmoving value storage. Ordinary arenas retain their bump allocator.
 * Unmarked allocations become reusable after sweep; live addresses stay stable. */
void arena_init_collectable(arena *a);
void arena_set_trace(arena *a, const void *value, arena_tracer trace, const void *shape);
void arena_collect_begin(arena *a);
bool arena_mark(arena *a, const void *address);
bool arena_trace_marked(arena *a, arena_marker mark, void *user);
void arena_sweep(arena *a);
void arena_usage(const arena *a, size_t *bytes, size_t *capacity);
/* Backing allocations, including block and tracked-allocation headers. */
size_t arena_storage_bytes(const arena *);
/* Ordinary arena allocation rejects before growing beyond retained budget. */
void *arena_alloc_bounded(arena *, size_t bytes, size_t budget);

#endif
