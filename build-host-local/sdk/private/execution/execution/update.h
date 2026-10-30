#ifndef CHIBI_EXECUTION_UPDATE_H
#define CHIBI_EXECUTION_UPDATE_H

#include "ecs/execution.h"
#include "execution/backend.h"
#include "execution/function.h"

typedef struct
{
    double current_time;
    double delta_seconds;
} behavior_clock;
/* Disjoint setup/execution intervals. Execution includes nested functions,
 * excluding other synchronous root events and their setup. No waiting wall time. */
typedef enum
{
    BEHAVIOR_PROFILE_SETUP,
    BEHAVIOR_PROFILE_START,
    BEHAVIOR_PROFILE_RESUME
} behavior_profile_phase;
typedef void (*behavior_profile_fn)(
    const char *event,
    uint64_t nanoseconds,
    uint64_t inclusive_nanoseconds,
    behavior_profile_phase phase,
    void *user
);
/* Canonical live state shared by the producer and synchronous backends.
 * The producer owns its value banks and clock and updates pending/generation
 * at queue and program replacement boundaries. The backend borrows them only
 * for the duration of an ordered span. value_owner is the receiver of the
 * context's value-storage callbacks; backends never inspect that receiver. */
typedef struct
{
    pin_value *variables;
    behavior_variable_storage *variable_storage;
    pin_value *node_states;
    behavior_profile_fn profile;
    uint64_t services_revision;
    pin_value owner;
    const behavior_clock *clock;
    behavior_backend backend;
    int alive, trace_enabled;
    int pending;
    uint64_t generation;
    void *value_owner;
} execution_update_state;
/* Prepared scratch is never concurrent or reentrant. The producer owns its
 * host storage and keeps this function generation alive until all rows retire. */
typedef struct
{
    behavior_function_context context;
    const behavior_function *function;
    void *code, *system_code;
    uint64_t generation;
    int index;
} execution_update_binding;
typedef struct
{
    execution_update_state *state;
    execution_update_binding *binding;
    uint64_t services_revision, entity;
    int attachment;
} execution_update_row;
typedef execution_update_row *(*execution_update_fn)(
    execution_update_row *,
    execution_update_row *,
    const ecs_execution_view *,
    int *
);
void *behavior_update_compile();
void behavior_update_free_code(void *code);

#endif
