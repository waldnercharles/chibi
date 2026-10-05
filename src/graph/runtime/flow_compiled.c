#include "graph/runtime/flow_compiled_internal.h"
#include "graph/runtime/instance.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void free_view(flow_run *view)
{
    flow_watch_clear(view);
    arena_free(&view->input_arena);
    afree(view->stack_cache);
    afree(view->traversals);
    afree(view->traversal_view);
    free(view);
}

static void retained_free(retained_watch_frame *frame)
{
    free_view(frame->view);
    afree(frame->hosts);
}

static bool retained_observed(flow_run *run, const retained_watch_frame *frame)
{
    for (int i = 0; i < acount(run->host_requests); i++) {
        for (int j = 0; j < acount(frame->hosts); j++) {
            if (run->host_requests[i]->compiled_serial == frame->hosts[j]) {
                return true;
            }
        }
    }
    return false;
}

static void retain_watches(flow_run *run, int reset)
{
    flow_compiled *c = run->compiled;
    for (int i = acount(c->retained) - 1; i >= 0; i--) {
        retained_watch_frame *frame = &c->retained[i];
        if (flow_compiled_descendant(c, frame->frame, reset) &&
            !retained_observed(run, frame)) {
            retained_free(frame);
            adel(c->retained, i);
        }
    }
    for (int frame = 1; frame < acount(c->frames); frame++) {
        if (!c->watch_frames[frame] || !c->watch_order[frame] ||
            !flow_compiled_descendant(c, frame, reset)) {
            continue;
        }
        retained_watch_frame saved = { .view = c->watch_frames[frame],
                                       .frame = frame,
                                       .order = c->watch_order[frame] };
        for (int i = 0; i < acount(run->host_requests); i++) {
            const flow_host_request *r = run->host_requests[i];
            if (r->compiled_order &&
                r->compiled_order == c->watch_order[r->compiled_frame] &&
                flow_compiled_descendant(c, r->compiled_frame, frame)) {
                apush(saved.hosts, r->compiled_serial);
            }
        }
        if (acount(saved.hosts)) {
            saved.view->compiled_owner = NULL;
            saved.view->state = FLOW_COMPLETED;
            saved.view->host_request = NULL;
            apush(c->retained, saved);
            c->watch_frames[frame] = NULL;
        }
    }
}
static flow_run *watch_frame(flow_run *run, int frame, const graph *g)
{
    if (!frame) {
        return run;
    }
    flow_compiled *c = run->compiled;
    if (!c->watch_frames[frame]) {
        c->watch_frames[frame] = xcalloc(1, sizeof *run);
        c->watch_frames[frame]->graph = (graph *)g;
        c->watch_frames[frame]->compiled_owner = run;
        c->watch_frames[frame]->compiled_frame = frame;
        arena_init(&c->watch_frames[frame]->input_arena, 256);
    }
    if (!c->watch_order[frame]) {
        c->watch_order[frame] = ++c->watch_serial;
    }
    if (run->host_request && run->host_request->compiled_frame == frame) {
        run->host_request->compiled_order = c->watch_order[frame];
    }
    return c->watch_frames[frame];
}
flow_run *flow_compiled_frame_view(flow_run *run, int frame)
{
    return watch_frame(
        run,
        frame,
        flow_compiled_frame_source(run->compiled, frame)->graph
    );
}
void flow_compiled_commit_watches(flow_run *run, int frame)
{
    flow_compiled *c = run->compiled;
    flow_run *pending = c->pending_watches ? c->pending_watches[frame] : NULL;
    if (!pending || !acount(pending->watches)) {
        return;
    }
    int parent = c->frames[frame].parent;
    flow_run *owner = watch_frame(run, parent, pending->graph);
    for (int i = 0; i < acount(pending->watches); i++) {
        const flow_watch *w = &pending->watches[i];
        node *n = graph_node_by_id(pending->graph, w->node_id);
        int output, pin;
        bool found = graph_pin_by_id(n, w->pin_id, &output, &pin);
        assert(found);
        (void)found;
        int result = flow_watch_store(owner, pending->graph, n, output, pin, &w->value);
        assert(!result);
        (void)result;
    }
    flow_watch_clear(pending);
}

void flow_compiled_host_output(flow_run *run, int frame, node *n, int pin, const pin_value *value)
{
    flow_compiled *c = run->compiled;
    c->revision++;
    if (!run->instance->trace_enabled) {
        return;
    }
    const graph *g = flow_compiled_frame_source(c, frame)->graph;
    flow_run *view = watch_frame(run, frame, g);
    if (pin < 0) {
        return;
    }
    int result = flow_watch_store(view, g, n, 1, pin, value);
    assert(!result);
    (void)result;
}

static int begin_command(behavior_function_context *context, int index)
{
    flow_compiled_invocation *invocation = context->inspection_user;
    flow_run *run = invocation->run;
    const behavior_command_binding *binding = &context->commands[index];
    flow_host_request *request = flow_host_start(run, binding->node);
    request->compiled_frame = invocation->base + binding->frame;
    request->compiled_serial = ++run->compiled->host_serial;
    request->compiled_order = run->compiled->watch_order
                                ? run->compiled->watch_order[request->compiled_frame]
                                : 0;
    request->compiled_outputs = context->command_results + binding->result_first;
    context->error_message = NULL;
    int result = binding->begin(
        binding->service,
        binding->reference,
        context->command_arguments + binding->first,
        binding->count,
        request,
        &context->error_message
    );
    if (result < 0) {
        flow_host_finish(run, FLOW_HOST_CANCELLED);
        flow_host_release(request);
    }
    context->wake_time = context->activation->wake_times[binding->frame];
    return result;
}

static void watch(void *user, const behavior_function_watch *original, const pin_value *value)
{
    flow_compiled_invocation *invocation = user;
    flow_run *run = invocation->run;
    behavior_function_watch mapped = *original;
    mapped.frame += invocation->base;
    if (mapped.pending_frame > 0) {
        mapped.pending_frame += invocation->base;
    }
    const behavior_function_watch *source = &mapped;
    flow_compiled *c = run->compiled;
    if (source->node && !run->instance->trace_enabled) {
        return;
    }
    int count = acount(c->frames);
    if (!source->node) {
        if (source->pending_frame) {
            flow_compiled_commit_watches(run, source->pending_frame);
            return;
        }
        retain_watches(run, source->frame);
        for (int i = 1; i < count; i++) {
            for (int at = i; at > 0; at = c->frames[at].parent) {
                if (at != source->frame) {
                    continue;
                }
                if (c->watch_frames[i]) {
                    flow_watch_clear(c->watch_frames[i]);
                    aclear(c->watch_frames[i]->traversals);
                    aclear(c->watch_frames[i]->traversal_view);
                    c->watch_frames[i]->traversal_head = 0;
                    c->watch_frames[i]->traversal_view_dirty = false;
                }
                if (c->pending_watches[i]) {
                    flow_watch_clear(c->pending_watches[i]);
                }
                c->watch_order[i] = 0;
                break;
            }
        }
        c->watch_order[source->frame] = ++c->watch_serial;
        return;
    }
    flow_run *owner;
    if (source->pending_frame) {
        flow_run **pending = source->pending_frame < 0
                               ? &c->outlined_watches[source->frame]
                               : &c->pending_watches[source->pending_frame];
        if (!*pending) {
            *pending = xcalloc(1, sizeof *run);
            (*pending)->graph = (graph *)source->graph;
        }
        owner = *pending;
    } else {
        owner = watch_frame(run, source->frame, source->graph);
    }
    int result = flow_watch_store(
        owner,
        source->graph,
        source->node,
        source->output,
        source->pin,
        value
    );
    assert(!result);
    (void)result;
}
static void trace(void *user, const behavior_function_edge *edge, uintptr_t steps)
{
    flow_compiled_invocation *invocation = user;
    flow_run *run = invocation->run;
    flow_traversal traversal = { edge->from_id, edge->from_pin_id,
                                 edge->to_id,   edge->to_pin_id,
                                 edge->kind,    run->current_time,
                                 steps };
    /* A paused run keeps its traced variant after its instance stops tracing. */
    if (run->instance->trace_enabled) {
        behavior_instance_record_activity(run->instance, &traversal);
    }
    flow_trace_record(run, traversal);
    flow_compiled *c = run->compiled;
    for (int frame = invocation->base + edge->frame; frame > 0;
         frame = c->frames[frame].parent) {
        const behavior_function_source *source = flow_compiled_frame_source(c, frame);
        flow_trace_record(watch_frame(run, frame, source->graph), traversal);
    }
}
static int inspect(void *user, const behavior_function_point *original)
{
    flow_compiled_invocation *invocation = user;
    flow_run *run = invocation->run;
    behavior_function_point mapped = *original;
    mapped.frame += invocation->base;
    const behavior_function_point *point = &mapped;
    flow_compiled *c = run->compiled;
    if (point->frame != c->current_frame) {
        int parent = c->frames[point->frame].parent;
        if (parent == c->current_frame) {
            c->stepping[point->frame] = c->stepping[parent] && !run->step_over;
        }
        c->step_nodes = 0;
        c->current_frame = point->frame;
    }
    if (point->kind != BEHAVIOR_POINT_NODE) {
        if (point->kind == BEHAVIOR_POINT_FRAME_DONE && !original->frame &&
            point->node) {
            invocation->context->returned_node_id = point->node->id;
        }
        return c->stepping[point->frame] ? FLOW_PAUSED_RESULT : FLOW_OK;
    }
    if (c->stepping[point->frame] && c->step_nodes) {
        c->pending_breakpoint = point->node->breakpoint && run->breakpoints_enabled;
        return FLOW_PAUSED_RESULT;
    }
    if (point->node->breakpoint && run->breakpoints_enabled) {
        run->breakpoint_node = (node *)point->node;
        return FLOW_PAUSED_RESULT;
    }
    if (run->pause_requested) {
        run->pause_requested = 0;
        return FLOW_PAUSED_RESULT;
    }
    c->step_nodes++;
    return FLOW_OK;
}
static int retrigger(behavior_function_context *context, int frame, int index, double deadline)
{
    flow_compiled_invocation *invocation = context->inspection_user;
    const behavior_function_source
        *source = behavior_function_frame_source(invocation->function, frame);
    assert(index >= 0 && index < acount(source->graph->nodes));
    return flow_run_retrigger_delay(invocation->run, source->graph->nodes[index], deadline);
}

void flow_compiled_start(flow_run *run, const behavior_function *function)
{
    assert(!run->compiled && run->instance);
    if (run->compiled_cache && run->compiled_cache->function != function) {
        flow_compiled_free(run);
    }
    flow_compiled *c = run->compiled_cache;
    run->compiled_cache = NULL;
    if (!c) {
        c = xcalloc(1, sizeof *c);
        c->function = function;
        behavior_function_activation_init(function, &c->activation);
        c->root = (flow_compiled_invocation){ .run = run,
                                              .function = function,
                                              .context = &c->context,
                                              .parent_frame = -1 };
        flow_compiled_add_frames(c, &c->root);
    }
    run->compiled = c;
    c->revision++;
    node *ret = graph_node_by_id(run->graph, run->graph->return_id);
    c->context = (behavior_function_context){
        .activation = &c->activation,
        .retrigger = retrigger,
        .arguments = run->inputs,
        .argument_count = run->input_count,
        .results = ret ? runtime_node_inputs(runtime_store_node(run->runtime, ret)) : NULL,
        .budget = run->step_budget,
        .call_depth = run->call_depth,
        .inspection_user = &c->root,
        .call_enter = flow_compiled_call_enter,
        .call_leave = flow_compiled_call_leave,
        .begin_command = begin_command
    };
    bool bound = behavior_function_bind(
        function,
        &c->context,
        run->services,
        behavior_instance_owner(run->instance).u64
    );
    assert(bound);
    (void)bound;
    behavior_instance_bind_values(run->instance, &c->context);
    behavior_function_prepare_data(function, run->services, &run->data_storage);
    c->context.data = run->data_storage;
    behavior_function_bind_storage(&c->context, &run->scratch);
    behavior_function_prepare_results(function, c->context.results, &run->scratch);
    run->whole_function = function;
    run->whole_function_executed = true;
    aclear(run->frames);
    if (run->step_mode) {
        flow_compiled_step(run, false);
    }
}
void flow_compiled_clear_frame(flow_compiled *c, int frame)
{
    flow_run *view = c->watch_frames[frame];
    if (view) {
        flow_watch_clear(view);
        arena_reset(&view->input_arena);
        view->inputs = NULL;
        view->input_count = 0;
        view->state = FLOW_IDLE;
        view->host_request = NULL;
        view->compiled_revision = 0;
        aclear(view->stack_cache);
        aclear(view->traversals);
        aclear(view->traversal_view);
        view->traversal_head = 0;
        view->traversal_view_dirty = false;
    }
    if (c->pending_watches[frame]) {
        flow_watch_clear(c->pending_watches[frame]);
    }
    if (c->outlined_watches[frame]) {
        flow_watch_clear(c->outlined_watches[frame]);
    }
    c->watch_order[frame] = 0;
}

void flow_compiled_stop(flow_run *run)
{
    if (!run->compiled) {
        return;
    }
    flow_compiled *c = run->compiled;
    assert(!run->compiled_cache);
    run->wake_time = c->activation.wake_times ? c->activation.wake_times[0] : 0;
    c->stopping = true;
    behavior_function_clear_calls(&c->activation);
    behavior_function_release_host(&c->context);
    for (int i = 0; i < acount(c->retained); i++) {
        retained_free(&c->retained[i]);
    }
    aclear(c->retained);
    for (int i = 0; i < acount(c->frames); i++) {
        flow_compiled_clear_frame(c, i);
    }
    c->watch_serial = 0;
    c->context = (behavior_function_context){ 0 };
    c->activation = (behavior_function_activation){ .values = c->activation.values,
                                                    .wake_times = c->activation.wake_times,
                                                    .vm = c->activation.vm };
    memset(c->stepping, 0, (size_t)acount(c->frames) * sizeof *c->stepping);
    c->finished = c->stopping = false;
    c->failed_return = NULL;
    c->step_nodes = c->current_frame = 0;
    c->pending_breakpoint = false;
    run->compiled_cache = c;
    run->compiled = NULL;
}
void flow_compiled_capture_failure(flow_run *run, const behavior_function *f, const behavior_function_context *context)
{
    flow_compiled_start(run, f);
    flow_compiled *c = run->compiled;
    behavior_function_copy_activation(f, &c->activation, context->activation);
    c->context = *context;
    c->context.activation = &c->activation;
    c->context.data = run->data_storage;
    c->context.inspection_user = &c->root;
}
void flow_compiled_free(flow_run *run)
{
    flow_compiled_stop(run);
    flow_compiled *c = run->compiled_cache;
    if (!c) {
        return;
    }
    behavior_function_activation_free(&c->activation);
    afree(c->stepping);
    free(c->watch_storage);
    if (c->watch_frames) {
        for (int i = 0; i < acount(c->frames); i++) {
            if (c->watch_frames[i]) {
                free_view(c->watch_frames[i]);
            }
            free(c->pending_watches[i]);
            free(c->outlined_watches[i]);
        }
    }
    afree(c->watch_frames);
    afree(c->pending_watches);
    afree(c->outlined_watches);
    afree(c->watch_order);
    for (int i = 0; i < acount(c->invocations); i++) {
        free(c->invocations[i]);
    }
    afree(c->invocations);
    afree(c->frames);
    afree(c->retained);
    free(c);
    run->compiled_cache = NULL;
}
int flow_compiled_execute(flow_run *run)
{
    flow_compiled *c = run->compiled;
    c->revision++;
    if (c->finished) {
        run->state = FLOW_COMPLETED;
        return FLOW_OK;
    }
    const behavior_function *f = run->whole_function;
    flow_compiled_invocation *active = flow_compiled_active(c);
    const behavior_function_point
        *point = behavior_function_pause_point(active->function, active->context->activation);
    /* A step stops before the next activation. If that node also has a
     * breakpoint, the next advance must observe it before executing the node. */
    if (c->pending_breakpoint) {
        c->pending_breakpoint = false;
        assert(point && point->node);
        if (point->node->breakpoint && run->breakpoints_enabled) {
            run->breakpoint_node = (node *)point->node;
            run->state = FLOW_PAUSED;
            return FLOW_PAUSED_RESULT;
        }
    }
    c->step_nodes = point && point->kind == BEHAVIOR_POINT_NODE ? 1 : 0;
    c->current_frame = active->base + active->context->activation->frame;
    run->breakpoint_node = NULL;
    c->context.now = run->current_time;
    c->context.delta_seconds = run->eval.delta_seconds;
    bool stepping = false;
    for (int i = 0; i < acount(c->frames); i++) {
        stepping |= c->stepping[i];
    }
    /* Retained watches still need frame resets when recording is disabled.
     * Activations that have never been inspected keep the ordinary variant. */
    c->context.inspect = stepping || run->pause_requested || c->watch_serial ||
                                 run->instance->trace_enabled ||
                                 (run->breakpoints_enabled &&
                                  behavior_function_breakpoints(f))
                           ? inspect
                           : NULL;
    c->context.watch = run->instance->trace_enabled || c->watch_serial ? watch : NULL;
    c->context.trace = run->instance->trace_enabled ? trace : NULL;
    if (c->context.watch && !c->watch_storage && behavior_function_watch_size(f)) {
        c->watch_storage = xmalloc(behavior_function_watch_size(f));
    }
    c->context.watch_storage = c->watch_storage;
    behavior_instance_bind_values(run->instance, &c->context);
    int result = behavior_function_run(f, &c->context, run->backend == BEHAVIOR_SLJIT);
    active = flow_compiled_active(c);
    behavior_function_context *context = active->context;
    run->steps = c->context.steps;
    if (result == FLOW_SUSPENDED_RESULT || result == FLOW_PAUSED_RESULT) {
        for (int i = 1; c->pending_watches && i < acount(c->frames); i++) {
            flow_compiled_commit_watches(run, i);
        }
    }
    if (result == FLOW_SUSPENDED_RESULT) {
        const behavior_function_source *source = behavior_function_source_at(
            active->function,
            context->activation->source
        );
        run->waiting_node = graph_node_by_id(source->graph, context->activation->node_id);
        run->waiting_output = -1;
        run->wake_time = c->context.wake_time;
        run->state = FLOW_SUSPENDED;
        return result;
    }
    if (result == FLOW_PAUSED_RESULT) {
        if (c->context.returned_node_id) {
            run->returned = graph_node_by_id(run->graph, c->context.returned_node_id);
        }
        run->state = FLOW_PAUSED;
        return result;
    }
    if (result) {
        run->whole_error_stack = context->error_stack;
        const behavior_function_source
            *source = behavior_function_source_at(active->function, context->error_source);
        node *failed = graph_node_by_id(source->graph, context->node_id);
        const char *message = context->error_message ? context->error_message
                            : context->failure == BEHAVIOR_FUNCTION_DEPTH
                                ? "function call depth limit exceeded"
                            : context->failure == BEHAVIOR_FUNCTION_NO_RETURN
                                ? "function completed without Return"
                            : context->failure == BEHAVIOR_FUNCTION_DATA
                                ? "data function produced an invalid result"
                                : "flow step budget exceeded";
        flow_fail(run, result, failed, message);
        run->error.steps = context->error_steps;
        run->error.pin = context->error_pin;
        return result;
    }
    c->finished = true;
    run->returned = c->context.returned_node_id
                      ? graph_node_by_id(run->graph, c->context.returned_node_id)
                      : NULL;
    run->state = c->stepping[0] && c->step_nodes ? FLOW_PAUSED : FLOW_COMPLETED;
    return run->state == FLOW_PAUSED ? FLOW_PAUSED_RESULT : FLOW_OK;
}
static bool missing_return(const flow_run *root)
{
    return root->state == FLOW_FAILED &&
           flow_compiled_active(root->compiled)->context->failure ==
               BEHAVIOR_FUNCTION_NO_RETURN;
}
const flow_run *flow_compiled_sync_view(const flow_run *run)
{
    if (!run || !run->compiled_owner) {
        return run;
    }
    const flow_run *root = run->compiled_owner;
    flow_compiled *c = root->compiled;
    if (!c || run->compiled_revision == c->revision) {
        return run;
    }
    flow_run *view = (flow_run *)run;
    int frame = run->compiled_frame;
    const flow_compiled_frame *logical = &c->frames[frame];
    const flow_compiled_invocation *invocation = logical->invocation;
    if (invocation->completed) {
        return view;
    }
    const behavior_function *f = invocation->function;
    const behavior_function_context *context = invocation->context;
    const behavior_function_activation *activation = context->activation;
    int active = flow_compiled_active_frame(root);
    bool running = !c->finished && flow_compiled_descendant(c, active, frame);
    bool failed_return = missing_return(root) && active == frame;
    view->compiled_revision = c->revision;
    view->backend = root->backend;
    view->instance = root->instance;
    view->whole_function = f;
    view->whole_function_executed = true;
    view->whole_error_stack = root->whole_error_stack;
    view->state = running && !failed_return ? root->state : FLOW_COMPLETED;
    view->inspection_node = running ? flow_compiled_current(root) : NULL;
    view->steps = !running || active != frame || failed_return
                    ? behavior_function_frame_steps(f, activation, logical->local)
                : root->state == FLOW_FAILED ? context->error_steps
                                             : activation->steps;
    view->step_budget = root->step_budget;
    view->call_depth = root->call_depth;
    for (int at = frame; at > 0; at = c->frames[at].parent) {
        view->call_depth++;
    }
    view->wake_time = running ? flow_compiled_wake_time(root)
                    : activation->wake_times ? activation->wake_times[logical->local]
                                             : 0;
    view->waiting_node = running ? root->waiting_node : NULL;
    view->host_request = running ? root->host_request : NULL;
    view->error = running && !failed_return ? root->error : (flow_error){ 0 };
    arena_reset(&view->input_arena);
    view->input_count = context->argument_count;
    const pin_value *inputs = logical->local ? behavior_function_frame_inputs(
                                                   f,
                                                   activation,
                                                   logical->local,
                                                   &view->input_arena,
                                                   &view->input_count
                                               )
                                             : context->arguments;
    view->inputs = arena_alloc(
        &view->input_arena,
        (size_t)(view->input_count + 1) * sizeof *view->inputs
    );
    for (int i = 0; i < view->input_count; i++) {
        int result = pin_value_snapshot_to_arena(&view->input_arena, &view->inputs[i], &inputs[i]);
        assert(!result);
        (void)result;
    }
    return view;
}
const flow_run *flow_compiled_leaf(const flow_run *run)
{
    const flow_run *root = run->compiled_owner ? run->compiled_owner : run;
    flow_compiled *c = root->compiled;
    if (!c || c->finished) {
        return flow_compiled_sync_view(run);
    }
    int frame = flow_compiled_active_frame(root);
    if (!frame || (run->compiled_owner &&
                   !flow_compiled_descendant(c, frame, run->compiled_frame))) {
        return flow_compiled_sync_view(run);
    }
    const behavior_function_source *source = flow_compiled_frame_source(c, frame);
    flow_run *view = watch_frame((flow_run *)root, frame, source->graph);
    return flow_compiled_sync_view(view);
}
const node *flow_compiled_current(const flow_run *run)
{
    const flow_compiled *c = run->compiled;
    if (run->state == FLOW_FAILED) {
        return missing_return(run) ? NULL : run->error.node;
    }
    const flow_compiled_invocation *invocation = flow_compiled_active(c);
    const behavior_function_activation *activation = invocation->context->activation;
    const behavior_function *f = invocation->function;
    if (c->finished || !activation->pc) {
        return NULL;
    }
    const behavior_function_source
        *source = behavior_function_source_at(f, activation->source);
    const behavior_function_point *point = behavior_function_pause_point(f, activation);
    if (point && point->kind == BEHAVIOR_POINT_NODE) {
        return point->node;
    }
    if (point && point->kind == BEHAVIOR_POINT_FRAME_DONE) {
        return NULL;
    }
    if (activation->stack < 0) {
        return NULL;
    }
    const behavior_function_source
        *stack = behavior_function_stack_at(f, activation->stack);
    return stack->graph == source->graph ? stack->call : NULL;
}

void flow_compiled_retime(flow_run *run, double deadline)
{
    flow_compiled *c = run->compiled;
    flow_compiled_invocation *invocation = flow_compiled_active(c);
    behavior_function_context *context = invocation->context;
    context->activation->wake_times[context->activation->frame] = deadline;
    context->wake_time = deadline;
    c->context.wake_time = deadline;
    c->revision++;
}

double flow_compiled_wake_time(const flow_run *run)
{
    const flow_compiled *c = run->compiled;
    int frame = flow_compiled_active_frame(run);
    const flow_compiled_frame *logical = &c->frames[frame];
    const behavior_function_context *context = logical->invocation->context;
    if (!context) {
        return c->watch_frames[frame] ? c->watch_frames[frame]->wake_time : 0;
    }
    return context->activation->wake_times
             ? context->activation->wake_times[logical->local]
             : 0;
}

void flow_compiled_resume(flow_run *run)
{
    flow_compiled *c = run->compiled;
    memset(c->stepping, 0, (size_t)acount(c->frames) * sizeof *c->stepping);
}
void flow_compiled_step(flow_run *run, bool out)
{
    flow_compiled *c = run->compiled;
    flow_compiled_resume(run);
    int active = flow_compiled_active_frame(run);
    for (int frame = active; frame >= 0; frame = c->frames[frame].parent) {
        c->stepping[frame] = true;
    }
    c->stepping[active] = !out;
}
void flow_compiled_pause(flow_run *run)
{
    flow_compiled *c = run->compiled;
    c->stepping[flow_compiled_active_frame(run)] = true;
    run->step_mode = c->stepping[0];
}

const flow_run *flow_compiled_watch_owner(const flow_run *run, const node *n, int output, int pin)
{
    const flow_compiled *c = run->compiled_owner ? run->compiled_owner->compiled
                                                 : run->compiled;
    int scope = run->compiled_owner ? run->compiled_frame : 0;
    if (!c || !c->watch_frames) {
        return NULL;
    }
    const flow_run *owner = NULL;
    unsigned newest = 0;
    int owner_frame = -1;
    for (int i = 0; i < acount(c->retained); i++) {
        const retained_watch_frame *frame = &c->retained[i];
        if (flow_compiled_descendant(c, frame->frame, scope) &&
            frame->order >= newest && flow_watch_value(frame->view, n, output, pin)) {
            newest = frame->order;
            owner = frame->view;
            owner_frame = frame->frame;
        }
    }
    for (int i = 1; i < acount(c->frames); i++) {
        const flow_run *view = c->watch_frames[i];
        if (!view || !flow_compiled_descendant(c, i, scope) ||
            !flow_watch_value(view, n, output, pin)) {
            continue;
        }
        /* A recursive ancestor's own watch takes precedence over its children,
         * just as querying an ordinary activation walks itself before children. */
        if (owner_frame >= 0 && newest == c->watch_order[owner_frame] &&
            flow_compiled_descendant(c, i, owner_frame)) {
            continue;
        }
        if ((owner_frame >= 0 && flow_compiled_descendant(c, owner_frame, i)) ||
            c->watch_order[i] >= newest) {
            newest = c->watch_order[i];
            owner = view;
            owner_frame = i;
        }
    }
    return owner;
}
