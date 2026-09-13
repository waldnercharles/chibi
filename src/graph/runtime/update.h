#ifndef CHIBI_BEHAVIOR_UPDATE_H
#define CHIBI_BEHAVIOR_UPDATE_H

#include "ecs/execution.h"
#include "graph/runtime/instance.h"

/* Prepared synchronous entries. Bindings own shared host/VM scratch and a
 * reusable temporary-value arena. Failures transfer that arena to inspection.
 * Rows own
 * no instance state. Execute rows in order, never concurrently or reentrantly.
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

/* Native ABI: row *(row *, end *, world *, int *result). Generated code checks
 * liveness/observation/bindings and calls prepared whole-function entries. */
void *behavior_update_compile();
void behavior_update_free_code(void *code);

#endif
