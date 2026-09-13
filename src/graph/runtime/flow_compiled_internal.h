#ifndef CHIBI_FLOW_COMPILED_INTERNAL_H
#define CHIBI_FLOW_COMPILED_INTERNAL_H

#include "graph/compiler/behavior_function.h"
#include "graph/runtime/flow_internal.h"

typedef struct flow_compiled_invocation
{
    flow_run *run;
    const behavior_function *function;
    behavior_function_context *context;
    struct flow_compiled_invocation *parent;
    const node *call;
    int base, parent_frame;
    bool completed;
} flow_compiled_invocation;

typedef struct
{
    flow_compiled_invocation *invocation;
    int local, parent;
} flow_compiled_frame;

typedef struct
{
    flow_run *view;
    uint64_t *hosts;
    unsigned order;
    int frame;
} retained_watch_frame;

typedef struct flow_compiled
{
    const behavior_function *function;
    behavior_function_context context;
    behavior_function_activation activation;
    flow_compiled_invocation root;
    flow_compiled_invocation **invocations;
    flow_compiled_invocation *failed_return;
    flow_compiled_frame *frames;
    int step_nodes, current_frame;
    bool pending_breakpoint;
    bool *stepping;
    bool finished, stopping;
    void *watch_storage;
    flow_run **watch_frames;
    flow_run **pending_watches, **outlined_watches;
    unsigned *watch_order, watch_serial;
    uint64_t revision, host_serial;
    retained_watch_frame *retained;
} flow_compiled;

void flow_compiled_add_frames(flow_compiled *compiled, flow_compiled_invocation *invocation);
bool flow_compiled_descendant(const flow_compiled *compiled, int frame, int ancestor);
const behavior_function_source *flow_compiled_frame_source(const flow_compiled *compiled, int frame);
flow_compiled_invocation *flow_compiled_active(const flow_compiled *compiled);
int flow_compiled_active_frame(const flow_run *run);
void flow_compiled_call_enter(const behavior_function *function, behavior_function_context *child);
void flow_compiled_call_leave(behavior_function_context *child, int result);
void flow_compiled_commit_watches(flow_run *run, int frame);
void flow_compiled_clear_frame(flow_compiled *compiled, int frame);
flow_run *flow_compiled_frame_view(flow_run *run, int frame);

#endif
