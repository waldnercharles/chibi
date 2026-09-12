#ifndef CHIBI_BEHAVIOR_FUNCTION_H
#define CHIBI_BEHAVIOR_FUNCTION_H
#include "graph/model/behavior_asset.h"
#include "graph/runtime/value_array.h"

#include <stdio.h>

typedef struct behavior_function behavior_function;
typedef struct
{
    void *data;            /* Stable even while the current value is null. */
    const char *type_name; /* Owner-interned declared name. */
    const type_desc *type;
    uint64_t layout;
} behavior_variable_storage;
struct service_binding;
typedef struct
{
    node_query_fn read;
    void *service;
    const char *key;
    pin_array snapshot;
} behavior_query_binding;
typedef struct
{
    node_command_fn invoke;
    void *service;
    const char *reference;
} behavior_command_binding;
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
    BEHAVIOR_FUNCTION_STATE,
    BEHAVIOR_FUNCTION_COMMAND,
} behavior_function_failure;
typedef struct
{
    const graph *graph;
    const node *node;
    int pin;
    bool output;
    const char *reason;
} behavior_function_diagnostic;
/* Saved scalar state is independent of the executing backend. The caller keeps
 * the program generation, arguments, services and borrowed managed values alive
 * until completion or cancellation. Only values live at the suspension are saved. */
typedef struct
{
    uint64_t *values;
    /* Optional bytecode scratch cache, allocated on its first execution. */
    struct behavior_function_vm_storage *vm;
    uintptr_t steps;
    int pc, frame, source, stack;
    uint64_t node_id;
} behavior_function_activation;
typedef struct
{
    behavior_function_activation *activation;
    bool owned_host;
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
    int error_stack;
    int error_pin;
    uintptr_t error_steps;
    behavior_function_failure failure;
    const char *error_message;
    const struct service_binding *services;
    behavior_query_binding *queries;
    behavior_command_binding *commands;
    pin_value *command_arguments;
    int error_status;
    uint64_t owner;
    const char *owner_type_name;
    const struct ecs_execution_view *world, *binding_world;
    behavior_variable_storage *variable_storage;
    void *value_owner;
    /* Intern borrowed names before persistent writes; returns owner-lived storage. */
    const char *(*own_type_name)(void *owner, const char *name);
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
/* Raw native/generated-C entry callers prepare/release these once per
 * invocation. run() does this automatically unless the caller supplies them. */
void behavior_function_prepare_host(const behavior_function *function, behavior_function_context *ctx);
void behavior_function_release_host(behavior_function_context *ctx);
void behavior_function_activation_init(
    const behavior_function *function,
    behavior_function_activation *activation
);
void behavior_function_activation_free(behavior_function_activation *activation);
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
const behavior_function_source *behavior_function_stack_at(const behavior_function *function, int stack);
const behavior_function_source *behavior_function_source_at(
    const behavior_function *function,
    int source
);
#endif
