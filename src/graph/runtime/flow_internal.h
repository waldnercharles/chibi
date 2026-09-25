#ifndef CHIBI_FLOW_INTERNAL_H
#define CHIBI_FLOW_INTERNAL_H

#include "graph/runtime/activation.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/flow.h"
#include "graph/runtime/runtime.h"
#include "graph/runtime/value_array.h"
#include "support/arena.h"

/* Private activation state shared only by execution and its diagnostics. */

typedef struct
{
    uint64_t node_id;
    uint64_t pin_id;
    int output;
    pin_value value;
    void *owned;
    size_t owned_size;
    arena managed;
    char *type_name;
} flow_watch;

struct flow_host_request
{
    int references;
    flow_host_state state;
    flow_run *run;
    node *node;
    pin_value *compiled_outputs;
    int compiled_frame;
    unsigned compiled_order;
    uint64_t compiled_serial;
};

struct flow_run
{
    const struct behavior_graph_program *program;
    int backend;
    graph *graph;
    behavior_instance *instance; /* owning instance, or NULL standalone */
    int graph_guard_owned; /* 1 when this run holds graph_flow_pause itself */
    flow_state state;
    unsigned long step_budget;
    unsigned long steps;
    bool profile_started;
    bool whole_function_executed;
    const struct behavior_function *whole_function;
    int whole_error_stack;
    struct flow_compiled *compiled, *compiled_cache;
    behavior_data_storage *data_storage;
    /* Read-only logical frame views never own an execution runtime. */
    flow_run *compiled_owner;
    int compiled_frame;
    uint64_t compiled_revision;
    const node *inspection_node;
    unsigned long long topology_revision;
    flow_clock_fn now;
    void *now_user;
    double current_time;
    double wake_time;
    int waiting_output;
    node *waiting_node;
    flow_host_request *host_request;
    flow_host_request **host_requests;
    int graph_paused;
    int pause_requested;
    int step_mode;
    unsigned long activation_budget;
    unsigned call_depth;
    int breakpoints_enabled;
    node *breakpoint_node;
    int breakpoint_skipped;
    service_binding *services;
    flow_error error;
    eval_context eval;
    runtime_store *runtime;
    flow_run **children;
    flow_run **call_pool; /* idle frames, at most one per call site */
    flow_run *parent;
    flow_run *active_child;
    node *call_node;
    node *invocation;
    node *returned;
    int entry_output;
    int return_input;
    int step_over;
    activation_frame *frames;
    flow_call_frame *stack_cache;
    flow_call_frame *data_error_stack;
    pin_value *inputs;
    int input_count;
    int input_capacity;
    const void *input_user_data;
    flow_sink_fn sink;
    void *sink_user;
    char *event_name;
    arena input_arena;
    arena scratch;
    flow_watch *watches;
    int *watch_index;
    int watch_capacity;
    flow_traversal *traversals;
    flow_traversal *traversal_view;
    int traversal_head;
    bool traversal_view_dirty;
};

struct flow_context
{
    flow_run *run;
    node *node;
    int input_pin;
    int pause_requested;
    bool inputs_unchanged;
    double pause_time;
    int pause_output;
    int failed;
};

void flow_fail(flow_run *run, int code, node *n, const char *message);
void flow_run_finish_compiled(
    flow_run *run,
    const behavior_function *function,
    const behavior_function_context *context,
    int result
);
void flow_compiled_start(flow_run *run, const struct behavior_function *function);
void flow_compiled_capture_failure(
    flow_run *run,
    const struct behavior_function *function,
    const behavior_function_context *context
);
void flow_compiled_host_output(flow_run *run, int frame, node *n, int pin, const pin_value *value);
int flow_compiled_execute(flow_run *run);
void flow_compiled_stop(flow_run *run);
void flow_compiled_free(flow_run *run);
void flow_compiled_resume(flow_run *run);
void flow_compiled_step(flow_run *run);
void flow_compiled_pause(flow_run *run);
const node *flow_compiled_current(const flow_run *run);
double flow_compiled_wake_time(const flow_run *run);
void flow_compiled_retime(flow_run *run, double deadline);
bool flow_run_retrigger_delay(flow_run *current, const node *waiting, double deadline);
void flow_compiled_build_stack(flow_run *run);
const flow_run *flow_compiled_leaf(const flow_run *run);
const flow_run *flow_compiled_sync_view(const flow_run *run);
static inline const flow_run *flow_run_view(const flow_run *run)
{
    return run && run->compiled_owner ? flow_compiled_sync_view(run) : run;
}
const flow_run *flow_compiled_watch_owner(const flow_run *run, const node *n, int output, int pin);
const pin_value *flow_watch_value(const flow_run *run, const node *n, int output, int pin);
void flow_trace_record(flow_run *run, flow_traversal traversal);
const pin_value *flow_watch_input_value(const flow_run *run, const node *n, int pin);
int flow_watch_store(flow_run *run, const graph *g, const node *n, int output, int pin, const pin_value *value);
void flow_watch_capture(flow_run *run, node *n);
void flow_watch_capture_outputs(flow_run *run, node *n);
void flow_watch_clear(flow_run *run);

void flow_host_finish(flow_run *run, flow_host_state state);
flow_host_request *flow_host_start(flow_run *run, node *n);
void flow_host_detach(flow_run *run);
int flow_run_continue(flow_run *run, int clear_step_mode);
void flow_run_data_call(
    flow_run *run,
    const graph *caller,
    const node *call,
    const graph *callee,
    const node *failed
);

#endif
