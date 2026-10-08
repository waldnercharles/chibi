#ifndef CHIBI_EXECUTION_FUNCTION_H
#define CHIBI_EXECUTION_FUNCTION_H
#include "execution/diagnostic.h"
#include "execution/host.h"
#include "execution/source_debug.h"
#include "execution/state.h"
#include "execution/value_kind.h"
#include "support/arena.h"
#include "types/array.h"
#include "types/type.h"

#include <stdio.h>

typedef struct behavior_function behavior_function;
behavior_value_kind behavior_function_kind(const behavior_function *, behavior_value_bank, int index);
const compile_diagnostic *behavior_function_query_diagnostics(
    const behavior_function *function,
    int *count
);
typedef struct
{
    void *data;            /* Stable even while the current value is null. */
    const char *type_name; /* Owner-interned declared name. */
    const type_desc *type;
    uint64_t layout; /* Declared struct layout, including managed values/arrays. */
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
    node_host_call_fn invoke;
    node_value_call_fn value;
    node_context_call_fn context;
    node_action_call_fn action;
    void *service;
} behavior_data_binding;
typedef struct
{
    const behavior_function *function;
    behavior_data_binding *calls;
    pin_value *values;
    void **buffers;
} behavior_data_storage;
typedef struct
{
    node_command_fn invoke;
    void *service;
    const char *reference;
    node_command_begin_fn begin;
    uint64_t node_id;
    int frame, first, count, result_first;
} behavior_command_binding;
typedef struct
{
    uint64_t graph_id, call_id;
    int parent;
    const char *asset_name;
    int start, end, line, column;
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
    BEHAVIOR_FUNCTION_ARGUMENT,
    BEHAVIOR_FUNCTION_COMMAND,
} behavior_function_failure;
typedef enum
{
    BEHAVIOR_POINT_NODE,
    BEHAVIOR_POINT_FRAME_DONE,
    BEHAVIOR_POINT_CALL_DONE
} behavior_point_kind;
typedef struct
{
    uint64_t graph_id, node_id;
    int frame, stack, depth;
    behavior_point_kind kind;
    bool breakpoint;
} behavior_function_point;
typedef struct
{
    uint64_t graph_id, node_id;
    int frame, pin;
    /* Stage caller values until this child first pauses or returns successfully.
     * With node == NULL, commit that child's staged values. Zero means immediate. */
    int pending_frame;
    bool output;
    pin_value initial;
} behavior_function_watch;
typedef struct
{
    uint64_t from_id, from_pin_id, to_id, to_pin_id;
    int kind;
    int frame;
} behavior_function_edge;
/* Saved scalar state is independent of the executing backend. The caller keeps
 * the program generation, arguments, services and borrowed managed values alive
 * until completion or cancellation. Only values live at the suspension are saved. */
typedef struct
{
    uint64_t *values;
    double *wake_times;
    struct behavior_function_calls *calls;
    /* Optional bytecode scratch cache, allocated on its first execution. */
    struct behavior_function_vm_storage *vm;
    uintptr_t steps;
    int pc, frame, source, stack;
    uint64_t node_id;
} behavior_function_activation;
typedef struct behavior_function_context
{
    /* Outlined calls retain their parent context while the caller is suspended. */
    struct behavior_function_context *parent;
    const behavior_function *source_function;
    const behavior_source_point *source_points;
    const behavior_source_watch *source_watches;
    void *source_debug_user;
    bool source_debug_suppressed;
    int (*source_pause)(void *, struct behavior_function_context *, const behavior_source_point *);
    void (*source_capture)(
        void *,
        struct behavior_function_context *,
        const behavior_source_watch *,
        const pin_value *
    );
    /* Runtime inspection attaches views after argument retention and detaches
     * them before invocation storage is released. Bodies never call a graph evaluator. */
    void (*call_enter)(const behavior_function *function, struct behavior_function_context *child);
    void (*call_leave)(struct behavior_function_context *child, int result);
    behavior_function_activation *activation;
    /* Updates another activation's wait; source indices belong to this program. */
    int (*retrigger)(struct behavior_function_context *context, int frame, int node, double deadline);
    behavior_data_storage *data;
    double now, wake_time;
    const char *program_name; /* Borrowed immutable registration name. */
    /* Instance simulation clock is distinct from an activation's wait clock. */
    const double *instance_time;
    void *inspection_user;
    int (*inspect)(void *user, const behavior_function_point *point);
    const behavior_function_point *points;
    /* The runtime owns distinct variable, argument and result storage. */
    pin_value *variables;
    pin_value *states; /* Fixed node-state bank for the linked asset generation. */
    const pin_value *arguments;
    int argument_count;
    bool owned_host;
    bool owned_data;
    /* Return values use the canonical Return node's input pin indices. */
    pin_value *results;
    uint64_t returned_node_id;
    bool returned;
    bool exit_requested;
    uint64_t returned_outcome;
    int pending_commands;
    const pin_value *(*action_result)(struct behavior_function_context *, uint64_t action, int ordinal);
    const pin_value *(*event_argument)(struct behavior_function_context *, uint64_t function, int ordinal);
    int (*complete_action)(struct behavior_function_context *, uint64_t action, const pin_value *, int count);
    int (*state_inputs)(struct behavior_function_context *, uint64_t action, int entry, int *demand);
    int (*state_transition)(
        struct behavior_function_context *,
        uint64_t action,
        int entry,
        const pin_value *,
        int count,
        int *selected
    );
    int (*retrigger_action)(
        struct behavior_function_context *,
        uint64_t action,
        double duration,
        bool *found
    );
    int (*request_action_break)(struct behavior_function_context *, uint64_t action);
    int (*take_action_break)(struct behavior_function_context *, uint64_t action, bool *requested);
    int (*initialize_local)(struct behavior_function_context *, pin_value *, bool stored);
    int (*store_local_group)(struct behavior_function_context *, pin_value *, int count, bool stored);
    int (*component_membership)(
        struct behavior_function_context *,
        uint64_t entity,
        uint64_t component,
        bool add,
        bool *changed
    );
    void (*bind_instance)(const struct behavior_function *, struct behavior_function_context *);
    void (*commit_instance)(struct behavior_function_context *);
    int (*store_component)(
        struct behavior_function_context *,
        uint64_t entity,
        uint64_t component,
        uint64_t layout,
        size_t offset,
        pin_value declared,
        pin_value value
    );
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
    void (*failure_observer)(
        const struct behavior_function *,
        const struct behavior_function_context *,
        int status
    );
    const struct service_binding *services;
    behavior_query_binding *queries;
    behavior_command_binding *commands;
    pin_value *command_arguments;
    pin_value *command_results;
    int (*begin_command)(struct behavior_function_context *context, int command);
    int error_status;
    uint64_t owner;
    const char *owner_type_name;
    const struct ecs_execution_view *world, *binding_world;
    /* Optional column table for a synchronous span with stable ECS storage. */
    const struct ecs_execution_column *const *columns;
    behavior_variable_storage *variable_storage;
    void *value_owner;
    /* Invocation-owned copies outlive locals, suspension and returned values.
     * The caller resets this storage only after those values are released. */
    void *value_storage;
    const char *(*copy_string)(void *storage, const char *text);
    pin_array *(*copy_array)(void *storage, const pin_array *array);
    void (*store_value)(void *owner, int variable, const pin_value *value);
    void (*add_set_value)(void *owner, int variable, const pin_value *item);
    /* Intern borrowed names before persistent writes; returns owner-lived storage. */
    const char *(*own_type_name)(void *owner, const char *name);
    void (*watch)(void *user, const behavior_function_watch *source, const pin_value *value);
    void (*trace)(void *user, const behavior_function_edge *edge, uintptr_t steps);
    const behavior_function_watch *watches;
    const behavior_function_edge *edges;
    pin_value watched;
    void *watch_storage;
    /* Keep optional state callbacks after the frequently read context fields. */
    int (*select_state)(void *owner, const behavior_state_selection *selection, int start, int flags);
} behavior_function_context;
/* Deepest retained invocation, including a suspended or failed callee. */
/* Visits the root and retained outlined contexts without changing their frames.
 * The visitor must not release or add call frames. */
void behavior_function_visit_contexts(
    behavior_function_context *,
    void (*visit)(behavior_function_context *)
);
behavior_function_context *behavior_function_active_context(
    behavior_function_context *context,
    const behavior_function **function
);
/* Service collection arguments are NULL or support/array.h vectors, never
 * fixed/sentinel arrays; contexts borrow the collection/name/value storage for
 * their invocation lifetime. See execution/service.h.
 * Bind host storage once per invocation. Custom component readers keep their
 * general compiled ABI until they supply a direct execution view. */
bool behavior_function_bind(
    const behavior_function *function,
    behavior_function_context *ctx,
    const struct service_binding *services,
    uint64_t owner
);
/* Resolve IDs once before a batchable span. The caller owns *columns and must
 * refresh it after any structural boundary or service/view change. Presence
 * and layout diagnostics remain at the original operation, not at binding. */
void behavior_function_bind_columns(
    const behavior_function *function,
    behavior_function_context *ctx,
    const struct ecs_execution_column ***columns
);
/* Rebind services while retaining plain argument/result buffers for this
 * immutable function generation. Each simultaneous invocation needs a cache. */
void behavior_function_prepare_data(
    const behavior_function *function,
    const struct service_binding *services,
    behavior_data_storage **storage
);
void behavior_function_free_data(behavior_data_storage *storage);
void behavior_function_copy_activation(
    const behavior_function *function,
    behavior_function_activation *target,
    const behavior_function_activation *source
);
uint64_t behavior_function_data_allocations();
/* Raw native/generated-C entry callers prepare/release these once per
 * invocation. run() does this automatically unless the caller supplies them. */
void behavior_function_rebind_services(
    const behavior_function *function,
    behavior_function_context *context,
    const struct service_binding *services
);
const char *behavior_function_host_action(behavior_function_context *, int target, uint64_t node_id);
void behavior_function_prepare_host(const behavior_function *function, behavior_function_context *ctx);
void behavior_function_release_host(behavior_function_context *ctx);
size_t behavior_function_watch_size(const behavior_function *function);
/* Checks the live saved values, value banks and outlined call frames. Used by
 * a managed state owner to release overwritten storage after suspension. */
/* Conservative component read footprint across linked definitions. Returns
 * required capacity; zero IDs mean an unbounded host/query read. */
int behavior_function_component_reads(const behavior_function *function, uint64_t *ids, int capacity);
bool behavior_function_retains_storage(
    const behavior_function *function,
    const behavior_function_context *context,
    const struct arena *storage
);
/* Visit current typed value banks and live saved values, including outlined
 * calls. Invoke only when these contexts are suspended or have returned. */
void behavior_function_trace_values(
    const behavior_function *,
    const behavior_function_context *,
    arena_marker,
    void *user
);
bool behavior_function_resumable(const behavior_function *function);
/* Whether its inspected code records the wires and values it carries. */
bool behavior_function_observed(const behavior_function *function);
bool behavior_function_has_invocations(const behavior_function *function);
/* An event whose logic reads no payload, with synchronous, non-reentrant
 * operations only.
 * Its component accesses may cross entities, so execution order still matters. */
bool behavior_function_batchable(const behavior_function *function);
/* Immutable lowering proof. Other-entity reads must not overlap any component
 * written by another body in the same parallel span. */
bool behavior_function_parallel(const behavior_function *function);
/* Static work estimate used to keep cheap loops below scheduling overhead. */
int behavior_function_parallel_work(const behavior_function *function);
/* Union of a host call's zero-based U32 input across the unobserved body.
 * No matching calls returns zero; an unproven value/invocation returns UINT_MAX.
 * Used once when preparing a synchronous execution plan, not per entity. */
unsigned behavior_function_host_mask(
    const behavior_function *function,
    const node_host_call *call,
    int input
);
bool behavior_function_parallel_with(const behavior_function *a, const behavior_function *b);
/* Cached conservative proof for synchronous broadcasts. Unknown host effects
 * and structural operations cannot preserve a component-dependent query index. */
bool behavior_function_preserves_components(
    const behavior_function *function,
    const uint64_t *components,
    int count
);
bool behavior_function_breakpoints(const behavior_function *function);
int behavior_function_frame_count(const behavior_function *function);
uintptr_t behavior_function_frame_steps(
    const behavior_function *function,
    const behavior_function_activation *activation,
    int frame
);
int behavior_function_frame_parent(const behavior_function *function, int frame);
const behavior_function_source *behavior_function_frame_source(
    const behavior_function *function,
    int frame
);
int behavior_function_frame_stack(const behavior_function *function, int frame);
const behavior_function_point *behavior_function_pause_point(
    const behavior_function *function,
    const behavior_function_activation *activation
);
void behavior_function_activation_init(
    const behavior_function *function,
    behavior_function_activation *activation
);
void behavior_function_activation_free(behavior_function_activation *activation);
void behavior_function_clear_calls(behavior_function_activation *activation);
void behavior_function_free(behavior_function *function);
bool behavior_function_compile(behavior_function *function);
int behavior_function_run(const behavior_function *function, behavior_function_context *ctx, bool native);
/* Thread-local count of VM scratch initializations, for allocation profiling. */
uint64_t behavior_function_vm_allocations();
/* The exported C function has the same ABI as the SLJIT entry. */
/* Batchable Tick body embedded in an ordered native row loop. */
bool behavior_function_compile_system(behavior_function *function);
void *behavior_function_system_entry(const behavior_function *function);
/* Caller owns the sequence kernel; all immutable functions must outlive its
 * invocation. Frees with behavior_jit_free. Executes exactly the given order. */
void *behavior_function_compile_sequence(const behavior_function *const *functions, int count);
void behavior_function_write_sequence_c(
    const behavior_function *const *functions,
    int count,
    FILE *out,
    const char *name
);
void behavior_function_write_system_c(const behavior_function *function, FILE *out, const char *name);
void behavior_function_write_c(const behavior_function *function, FILE *out, const char *name);
void *behavior_function_entry(const behavior_function *function);
/* Caller-owned output storage outlives the invocation. Only aggregate results
 * allocate; scalar result slots are already initialized by the runtime. */
void behavior_function_prepare_results(
    const behavior_function *function,
    pin_value *results,
    arena *storage
);
void behavior_function_bind_storage(behavior_function_context *context, arena *storage);
/* True when emitted operations require invocation-owned temporary values. */
bool behavior_function_uses_storage(const behavior_function *function);
/* Pure calls share the evaluator's depth counter. If an inlined call could
 * exceed its limit, use the checked compiled path for precise diagnostics. */
bool behavior_function_pure_depth(const behavior_function *function, unsigned depth);
const behavior_function_source *behavior_function_stack_at(const behavior_function *function, int stack);
int behavior_function_source_count(const behavior_function *function);
pin_type behavior_function_slot_type(const behavior_function *, int slot);
const behavior_function_source *behavior_function_instruction_source(
    const behavior_function *,
    int pc
);
const behavior_function_source *behavior_function_source_at(
    const behavior_function *function,
    int source
);
/* Reconstruct frame arguments from owned parameter layouts. */
const pin_value *behavior_function_frame_inputs(
    const behavior_function *function,
    const behavior_function_activation *activation,
    int frame,
    arena *storage,
    int *count
);
#endif
