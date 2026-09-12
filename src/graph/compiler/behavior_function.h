#ifndef CHIBI_BEHAVIOR_FUNCTION_H
#define CHIBI_BEHAVIOR_FUNCTION_H
#include "graph/model/behavior_asset.h"

#include <stdio.h>

typedef struct behavior_function behavior_function;
struct service_binding;
typedef struct
{
    graph *graph;
    node *call;
    int parent;
} behavior_function_source;
typedef enum
{
    BEHAVIOR_FUNCTION_BUDGET,
    BEHAVIOR_FUNCTION_DEPTH,
    BEHAVIOR_FUNCTION_NO_RETURN,
    BEHAVIOR_FUNCTION_DATA,
    BEHAVIOR_FUNCTION_HOST_DATA,
    BEHAVIOR_FUNCTION_HOST_FLOW,
} behavior_function_failure;
typedef struct
{
    const graph *graph;
    const node *node;
    int pin;
    bool output;
    const char *reason;
} behavior_function_diagnostic;
typedef struct
{
    /* The runtime owns distinct variable, argument and result storage. */
    pin_value *variables;
    const pin_value *arguments;
    int argument_count;
    /* Return values use the canonical Return node's input pin indices. */
    pin_value *results;
    uint64_t returned_node_id;
    double delta_seconds;
    uintptr_t steps;
    uintptr_t budget;
    uint64_t node_id;
    unsigned call_depth;
    int error_frame;
    int error_source;
    int error_pin;
    uintptr_t error_steps;
    behavior_function_failure failure;
    const char *error_message;
    uint64_t owner;
    const struct ecs_execution_view *world, *binding_world;
} behavior_function_context;
/* Complete synchronous entry with scalar or fixed-layout values, or NULL if an operation requires the
 * general compiled runtime. No partial effects occur during eligibility checks. */
behavior_function *behavior_function_lower(
    const behavior_asset *asset,
    graph *g,
    node *entry,
    behavior_function_diagnostic *diagnostic
);
/* Bind host storage once per invocation. Custom component readers keep their
 * general compiled ABI until they supply a direct execution view. */
bool behavior_function_bind(
    const behavior_function *function,
    behavior_function_context *ctx,
    const struct service_binding *services,
    uint64_t owner
);
void behavior_function_free(behavior_function *function);
bool behavior_function_compile(behavior_function *function);
int behavior_function_run(const behavior_function *function, behavior_function_context *ctx, bool native);
/* The exported C function has the same ABI as the SLJIT entry. */
void behavior_function_write_c(const behavior_function *function, FILE *out, const char *name);
void *behavior_function_entry(const behavior_function *function);
/* Caller-owned output storage outlives the invocation. Only aggregate results
 * allocate; scalar result slots are already initialized by the runtime. */
void behavior_function_prepare_results(
    const behavior_function *function,
    pin_value *results,
    arena *storage
);
/* Pure calls share the evaluator's depth counter. If an inlined call could
 * exceed its limit, use the checked compiled path for precise diagnostics. */
bool behavior_function_pure_depth(const behavior_function *function, unsigned depth);
const behavior_function_source *behavior_function_source_at(
    const behavior_function *function,
    int source
);
#endif
