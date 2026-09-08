#ifndef CBLUEPRINT_FLOW_INTERNAL_H
#define CBLUEPRINT_FLOW_INTERNAL_H

#include "graph/runtime/eval.h"
#include "graph/runtime/flow.h"
#include "graph/runtime/runtime.h"
#include "graph/runtime/value_array.h"
#include "support/arena.h"

/* Private activation state shared only by execution and its diagnostics. */
typedef enum
{
    FLOW_FRAME_NODE,
    FLOW_FRAME_LOOP,
    FLOW_FRAME_FOREACH,
    FLOW_FRAME_WHILE,
} flow_frame_kind;

typedef struct
{
    node *node;
    flow_frame_kind kind;
    int current;
    int last;
    int input_pin;
    int body_output;
    int value_output;
    int completed_output;
    int index_output;
    int break_requested;
    pin_array *array;
} flow_frame;

typedef struct
{
    uint64_t node_id;
    uint64_t pin_id;
    int output;
    pin_value value;
    void *owned;
    size_t owned_size;
} flow_watch;

struct flow_host_request
{
    int references;
    flow_host_state state;
    flow_run *run;
    node *node;
};

struct flow_run
{
    graph *graph;
    behavior_instance *instance; /* owning instance, or NULL standalone */
    int graph_guard_owned; /* 1 when this run holds graph_flow_pause itself */
    flow_state state;
    unsigned long step_budget;
    unsigned long steps;
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
    flow_run *parent;
    flow_run *active_child;
    node *call_node;
    node *invocation;
    node *returned;
    int entry_output;
    int return_input;
    int step_over;
    flow_frame *frames;
    flow_call_frame *stack_cache;
    pin_value *inputs;
    int input_count;
    int input_capacity;
    const void *input_user_data;
    flow_sink_fn sink;
    void *sink_user;
    char *event_name;
    flow_parallel_for_fn parallel_for;
    void *parallel_user;
    arena input_arena;
    arena scratch;
    flow_watch *watches;
    flow_traversal *traversals;
};

struct flow_context
{
    flow_run *run;
    node *node;
    int input_pin;
    int pause_requested;
    double pause_time;
    int pause_output;
    int failed;
};

int flow_watch_store(flow_run *run, const node *n, int output, int pin, const pin_value *value);
void flow_watch_capture(flow_run *run, node *n);
void flow_watch_clear(flow_run *run);

void flow_host_finish(flow_run *run, flow_host_state state);
void flow_host_detach(flow_run *run);
int flow_copy_input(arena *a, pin_value *dst, const pin_value *src);
int flow_run_continue(flow_run *run, int clear_step_mode);

#endif
