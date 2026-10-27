#ifndef CHIBI_EXECUTION_MANAGED_H
#define CHIBI_EXECUTION_MANAGED_H
#include "support/arena.h"
#include "types/value.h"

struct behavior_function;
struct behavior_function_context;
/* Caller-owned heap. Root slots must retain stable addresses until unrooted or heap destruction.
 * Registering a root snapshots its value header and owned payload into the
 * heap; managed pointer identities remain unchanged.
 * Collection is permitted only while execution is quiescent. The heap must
 * outlive every executing context that uses its pointers. Destroying the heap
 * clears registered host slots; roots never extend the heap lifetime. */
typedef struct behavior_managed_heap
{
    arena storage;
    pin_value **roots;
    struct behavior_managed_heap *owner; /* Canonical family after heap adoption. */
    struct behavior_managed_heap *previous; /* Heaps adopted by hot replacement. */
} behavior_managed_heap;
void behavior_managed_heap_init(behavior_managed_heap *);
void behavior_managed_heap_free(behavior_managed_heap *);
/* Returns an error without changing the slot or registering it on failure. */
const char *behavior_managed_heap_root(behavior_managed_heap *, pin_value *);
const char *behavior_managed_value_error(const pin_value *);
/* storage must be the first arena member of an initialized managed heap. */
const char *behavior_managed_storage_value_error(arena *storage, const pin_value *);
const char *behavior_managed_storage_owner_error(arena *storage, const pin_value *);
/* Clears the slot: its retained copy is left to the next collection. */
void behavior_managed_heap_unroot(behavior_managed_heap *, pin_value *);
void behavior_managed_heap_trace_value(const pin_value *, arena_marker, void *);
void behavior_managed_heap_trace_roots(const behavior_managed_heap *, arena_marker, void *);
void behavior_managed_heap_collect(
    behavior_managed_heap *,
    const struct behavior_function *,
    const struct behavior_function_context *
);
#endif
