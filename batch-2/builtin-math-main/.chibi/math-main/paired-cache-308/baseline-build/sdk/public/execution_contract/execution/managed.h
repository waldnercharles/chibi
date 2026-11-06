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
    /* Nonzero when its one root call owns every value, so that call may
     * collect it while it runs, after allocating at least this many bytes.
     * Programs share a heap between activations and collect between frames
     * instead. */
    size_t call_collection_bytes;
    /* Calls running on the heap. A nested one runs inside a native, whose C
     * locals no collection traces, so only the outermost collects. */
    int running_calls;
} behavior_managed_heap;
/* What a host that owns its heap's one root call allocates between
 * collections at least: each costs a trace of every live value, so the
 * heap also grows to twice what the last collection kept first. */
#define BEHAVIOR_CALL_COLLECTION_BYTES ((size_t)64 * 1024 * 1024)
void behavior_managed_heap_init(behavior_managed_heap *);
void behavior_managed_heap_free(behavior_managed_heap *);
/* Appends other's family to heap's and makes heap their canonical owner.
 * Cells keep their addresses, so their pointers stay valid. */
void behavior_managed_heap_adopt(behavior_managed_heap *heap, behavior_managed_heap *other);
/* Frees each adopted heap that holds no value and no root. Call after a
 * collection, while execution is quiescent. */
void behavior_managed_heap_release_empty(behavior_managed_heap *);
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
/* Collects a heap with call_collection_bytes once it has allocated that much
 * and as much as its last collection kept. root is the call's root context,
 * every frame of which is suspended at a call or has not started. */
void behavior_managed_heap_safepoint(
    behavior_managed_heap *,
    const struct behavior_function *,
    const struct behavior_function_context *root
);
#endif
