#ifndef CHIBI_BEHAVIOR_UPDATE_H
#define CHIBI_BEHAVIOR_UPDATE_H

#include "ecs/execution.h"
#include "graph/runtime/instance.h"

/* Prepared synchronous entries. Bindings own shared host/VM scratch and a
 * reusable temporary-value arena. Failures transfer that arena to inspection.
 * Rows own
 * no instance state. A binding must never execute concurrently or reentrantly.
 * Proven parallel spans use private bindings and disjoint entity chunks.
 * The caller keeps instances and their immutable programs alive. */
typedef struct behavior_update_binding behavior_update_binding;
typedef struct
{
    behavior_instance *instance;
    behavior_update_binding *binding;
    uint64_t services_revision;
    uint64_t entity;
    int attachment;
} behavior_update_row;
typedef behavior_update_row *(*behavior_update_fn)(
    behavior_update_row *row,
    behavior_update_row *end,
    const ecs_execution_view *world,
    int *result
);

behavior_update_binding *behavior_update_prepare(
    behavior_update_binding ***bindings,
    behavior_instance *instance,
    const behavior_function *function,
    node *entry
);
void behavior_update_free_bindings(behavior_update_binding **bindings);
/* Refresh borrowed columns before a span after any possible storage mutation. */
void behavior_update_refresh(behavior_update_binding **bindings);

/* Stops BEFORE a row requiring the ordinary scheduler, or AT a failed row.
 * Successful prior rows have already executed. Never retry a failed body.
 * The C loop is also the portable/instrumented implementation and the native
 * compiler comparison. A NULL profile callback disables all timing work. */
behavior_update_row *behavior_update_run_c(
    behavior_update_row *row,
    behavior_update_row *end,
    const ecs_execution_view *world,
    int *result,
    behavior_profile_fn profile
);
flow_run *behavior_update_failure(behavior_update_row *row, int result);

/* Native ABI: row *(row *, end *, world *, int *result). Both system kernels
 * and the prepared-body loop retain liveness/observation/binding guards. */
typedef struct
{
    int begin, end, period;
    void *code; /* Borrowed from a function or the sequence cache. */
} behavior_update_span;
typedef struct behavior_update_kernel behavior_update_kernel;
/* Cache repeated ordered sequences; the prepared native loop covers gaps.
 * Rebuilding reuses matching generations and frees unused sequence kernels. */
void behavior_update_system_spans(
    const behavior_update_row *rows,
    int count,
    behavior_update_span **spans,
    behavior_update_kernel ***kernels
);
/* Retains spans before the affected row and repairs only nearby boundaries. */
int behavior_update_system_spans_incremental(
    const behavior_update_row *rows,
    int count,
    int dirty_row,
    behavior_update_span **spans,
    behavior_update_kernel ***kernels
);
void behavior_update_free_kernels(behavior_update_kernel **kernels);
void *behavior_update_compile();
void behavior_update_free_code(void *code);

#endif
