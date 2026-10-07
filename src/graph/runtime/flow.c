#include "graph/runtime/flow.h"

#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/profile.h"
#include "graph/runtime/runtime.h"
#include "graph/runtime/service.h"
#include "graph/runtime/value_array.h"
#include "support/arena.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FLOW_TRAVERSAL_MAX 256

#include "graph/runtime/flow_compiled_internal.h"

static node *frame_node(const flow_run *run, const activation_frame *frame)
{
    return run->program ? behavior_program_block_node(run->program, frame->block)
                        : frame->node;
}
static const flow_run *active_run(const flow_run *run)
{
    while (run && run->active_child) {
        run = run->active_child;
    }
    return run;
}

static void push_frame(flow_run *run, activation_frame frame)
{
    frame.block = run->program ? behavior_program_block(run->program, frame.node)
                               : -1;
    if (run->program) {
        frame.node = NULL;
    }
    apush(run->frames, frame);
}

static void set_event_name(flow_run *run, const char *name)
{
    if (run->event_name == name ||
        (run->event_name && name && strcmp(run->event_name, name) == 0)) {
        return;
    }
    char *copy = name ? xstrdup(name) : NULL;
    free(run->event_name);
    run->event_name = copy;
}

static runtime_node *flow_runtime_node(flow_run *run, const node *n)
{
    return runtime_store_node(run->runtime, n);
}

static void set_error(flow_error *error, int code, const node *n, const char *message)
{
    if (error) {
        *error = (flow_error){ code, n, -1, message, 0 };
    }
}

static int graph_has_node(const graph *g, const node *n)
{
    return g && n && graph_node_by_id(g, n->id) == n;
}

static bool flow_is_delta_output(const pin_spec *pin)
{
    return pin && pin->kind == PIN_DATA &&
           (pin->type == PIN_F32 || pin->type == PIN_F64) && pin->name &&
           (!strcmp(pin->name, "DeltaSeconds") || !strcmp(pin->name, "Delta Seconds"));
}

static bool flow_event_delta(const node *entry, const pin_value *inputs, int input_count, double *delta)
{
    if (!entry || entry->desc->flow_kind != NODE_FLOW_EVENT || !delta) {
        return false;
    }
    int parameter = 0;
    for (int i = 0; i < entry->output_count; i++) {
        const pin_spec *pin = &entry->output_pins[i];
        if (pin->kind != PIN_DATA || pin->field_source) {
            continue;
        }
        if (flow_is_delta_output(pin)) {
            pin_value value = pin->default_value;
            if (parameter < input_count && inputs &&
                pin_compatible(
                    pin,
                    &(pin_spec){ .type = inputs[parameter].type,
                                 .type_name = inputs[parameter].type_name,
                                 .container = inputs[parameter].container }
                )) {
                value = inputs[parameter];
                pin_coerce(&value, pin);
            }
            if (value.type == PIN_F32) {
                *delta = value.f;
                return true;
            }
            if (value.type == PIN_F64) {
                *delta = value.d;
                return true;
            }
        }
        parameter++;
    }
    return false;
}

void flow_fail(flow_run *run, int code, node *n, const char *message)
{
    if (run->host_request) {
        flow_host_finish(run, FLOW_HOST_CANCELLED);
    }
    run->state = FLOW_FAILED;
    run->error = (flow_error){ code, n, -1, message, run->steps };
}

void flow_run_finish_compiled(
    flow_run *run,
    const behavior_function *function,
    const behavior_function_context *ctx,
    int result
)
{
    if (result && ctx->activation && behavior_function_resumable(function) &&
        !run->compiled) {
        flow_compiled_capture_failure(run, function, ctx);
    }
    run->whole_function_executed = true;
    run->whole_function = function;
    run->steps = ctx->steps;
    aclear(run->frames);
    if (result) {
        run->whole_error_stack = ctx->error_stack;
        const behavior_function_source *source = behavior_function_source_at(function, ctx->error_source);
        node *failed = graph_node_by_id(source->graph, ctx->node_id);
        const char *message = ctx->error_message ? ctx->error_message
                            : ctx->failure == BEHAVIOR_FUNCTION_DEPTH
                                ? "function call depth limit exceeded"
                            : ctx->failure == BEHAVIOR_FUNCTION_NO_RETURN
                                ? "function completed without Return"
                            : ctx->failure == BEHAVIOR_FUNCTION_DATA
                                ? "data function produced an invalid result"
                                : "flow step budget exceeded";
        flow_fail(run, result, failed, message);
        run->error.steps = ctx->error_steps;
        run->error.pin = ctx->error_pin;
        return;
    }
    run->returned = ctx->returned_node_id
                      ? graph_node_by_id(run->graph, run->graph->return_id)
                      : NULL;
    run->state = FLOW_COMPLETED;
}

static void graph_pause(flow_run *run)
{
    if (run->graph_paused) {
        return;
    }
    graph_flow_pause(run->graph);
    run->graph_paused = 1;
}

static void graph_resume(flow_run *run)
{
    if (!run->graph_paused) {
        return;
    }
    graph_flow_resume(run->graph);
    run->graph_paused = 0;
}

static void record_traversal(
    flow_run *run,
    uint64_t from_id,
    uint64_t from_pin_id,
    uint64_t to_id,
    uint64_t to_pin_id,
    graph_edge_kind kind
)
{
    if (run->instance && !run->instance->trace_enabled) {
        return;
    }
    flow_traversal traversal = {
        from_id, from_pin_id,       to_id,      to_pin_id,
        kind,    run->current_time, run->steps,
    };
    if (run->instance) {
        behavior_instance_record_activity(run->instance, &traversal);
    }
    flow_trace_record(run, traversal);
}

void flow_trace_record(flow_run *run, flow_traversal traversal)
{
    /* Call ancestors retain each traversal as it happens, including while a
     * child waits and after completed children are pruned or recycled. */
    for (; run; run = run->parent) {
        if (acount(run->traversals) >= FLOW_TRAVERSAL_MAX) {
            run->traversals[run->traversal_head] = traversal;
            run->traversal_head = (run->traversal_head + 1) % FLOW_TRAVERSAL_MAX;
        } else {
            apush(run->traversals, traversal);
        }
        run->traversal_view_dirty = true;
        run->traversal_revision++;
    }
}

/* One output pin has at most one EXEC target. The edge's explicit order is
 * retained by the graph and JSON; node output pins provide Sequence order. */
static int schedule_output(flow_context *ctx, int output_pin)
{
    flow_run *run = ctx->run;
    if (output_pin < 0 || output_pin >= ctx->node->output_count) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_NODE, "flow output pin out of range");
    }
    if (ctx->node->output_pins[output_pin].kind != PIN_EXEC) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_NODE, "continuation is not an EXEC output");
    }

    node *target = NULL;
    int target_pin = -1;
    if (run->program) {
        if (!behavior_program_target(run->program, ctx->node, output_pin, &target, &target_pin)) {
            return FLOW_OK;
        }
        record_traversal(
            run,
            ctx->node->id,
            ctx->node->output_pins[output_pin].id,
            target->id,
            target->input_pins[target_pin].id,
            GRAPH_EDGE_EXEC
        );
        push_frame(run, ((activation_frame){ .node = target, .kind = ACTIVATION_BLOCK, .input_pin = target_pin }));
        return FLOW_OK;
    }
    const edge *e = NULL;
    int found = graph_exec_output_edges(run->graph, ctx->node, output_pin, &e);
    if (found > 1) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "EXEC output has ambiguous fan-out");
    }
    if (!found) {
        return FLOW_OK;
    }
    target = graph_node_by_id(run->graph, e->to_id);
    int is_out = 0;
    if (!target || !graph_pin_by_id(target, e->to_pin_id, &is_out, &target_pin) ||
        is_out || target->input_pins[target_pin].kind != PIN_EXEC) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "invalid EXEC target");
    }
    record_traversal(run, ctx->node->id, ctx->node->output_pins[output_pin].id, e->to_id, e->to_pin_id, GRAPH_EDGE_EXEC);
    push_frame(
        run,
        (activation_frame){
            .node = target,
            .kind = ACTIVATION_BLOCK,
            .input_pin = target_pin,
        }
    );
    return FLOW_OK;
}

static int schedule_output_for_node(flow_run *run, node *n, int output_pin)
{
    flow_context ctx = { .run = run, .node = n, .input_pin = -1 };
    return schedule_output(&ctx, output_pin);
}

static void record_data_traversal(flow_run *run, const node *src, int src_pin, const node *dst, int dst_pin)
{
    record_traversal(
        run,
        src->id,
        src->output_pins[src_pin].id,
        dst->id,
        dst->input_pins[dst_pin].id,
        GRAPH_EDGE_DATA
    );
}

static void record_evaluated_data_edge(const node *src, int src_pin, const node *dst, int dst_pin, void *user)
{
    record_data_traversal((flow_run *)user, src, src_pin, dst, dst_pin);
}

static int flow_run_check_topology(flow_run *run)
{
    if (run->topology_revision == graph_topology_revision(run->graph)) {
        return 0;
    }
    flow_fail(run, FLOW_ERR_TOPOLOGY, NULL, "graph topology changed during flow run");
    return FLOW_ERR_TOPOLOGY;
}

static int flow_run_advance_child(flow_run *run)
{
    flow_run *child = run->active_child;
    child->current_time = run->current_time;
    flow_run_continue(child, 0);
    if (child->state == FLOW_FAILED) {
        run->state = FLOW_FAILED;
        run->error = child->error;
        return run->error.code;
    }
    if (child->state != FLOW_COMPLETED) {
        run->state = child->state;
        return child->state == FLOW_PAUSED ? FLOW_PAUSED_RESULT : FLOW_SUSPENDED_RESULT;
    }
    node *ret = child->returned;
    node *call = run->call_node;
    if (!ret && call->desc == &NODE_CallMacro) {
        run->active_child = NULL;
        run->call_node = NULL;
        return FLOW_OK;
    }
    /* A function without results may end anywhere, as a void function does;
       one with results must reach Return to produce them. */
    bool results = false;
    for (int i = 0; i < call->output_count; i++) {
        results |= call->output_pins[i].kind == PIN_DATA &&
                   !call->output_pins[i].field_source;
    }
    if (!ret && results) {
        flow_fail(run, FLOW_ERR_INVALID_GRAPH, call, "function completed without Return");
        return FLOW_ERR_INVALID_GRAPH;
    }
    pin_value *outputs = runtime_node_outputs(flow_runtime_node(run, call));
    int at = 0;
    for (int i = 0; ret && i < ret->input_count; i++) {
        if (ret->input_pins[i].kind != PIN_DATA) {
            continue;
        }
        while (at < call->output_count && (call->output_pins[at].kind != PIN_DATA ||
                                           call->output_pins[at].field_source)) {
            at++;
        }
        if (at >= call->output_count) {
            flow_fail(run, FLOW_ERR_INVALID_GRAPH, ret, "function return signature does not match call");
            return FLOW_ERR_INVALID_GRAPH;
        }
        if (!pin_compatible(&ret->input_pins[i], &call->output_pins[at])) {
            flow_fail(run, FLOW_ERR_INVALID_GRAPH, ret, "function return type does not match call");
            return FLOW_ERR_INVALID_GRAPH;
        }
        const pin_value *value = flow_run_input_value(child, ret, i);
        if (pin_value_snapshot_to_arena(&run->scratch, &outputs[at], value)) {
            flow_fail(run, FLOW_ERR_NODE, ret, "function return copy failed");
            return FLOW_ERR_NODE;
        }
        pin_coerce(&outputs[at], &call->output_pins[at]);
        at++;
    }
    run->active_child = NULL;
    run->call_node = NULL;
    flow_watch_capture_outputs(run, call);
    int output = 0;
    if (call->desc == &NODE_CallMacro) {
        int ordinal = 0;
        for (int i = 0; i < child->return_input; i++) {
            ordinal += ret->input_pins[i].kind == PIN_EXEC;
        }
        for (output = 0; output < call->output_count; output++) {
            if (call->output_pins[output].kind == PIN_EXEC && ordinal-- == 0) {
                break;
            }
        }
        if (output == call->output_count) {
            flow_fail(run, FLOW_ERR_INVALID_GRAPH, call, "macro exit is missing");
            return FLOW_ERR_INVALID_GRAPH;
        }
    }
    return schedule_output_for_node(run, call, output);
}

static int flow_run_wait(flow_run *run)
{
    if (run->active_child) {
        int result = flow_run_advance_child(run);
        if (result) {
            return result;
        }
        if (run->step_mode) {
            run->activation_budget = 0;
            run->state = FLOW_PAUSED;
            return FLOW_PAUSED_RESULT;
        }
    }
    if (run->host_request) {
        if (run->host_request->state == FLOW_HOST_PENDING) {
            run->state = FLOW_SUSPENDED;
            return FLOW_SUSPENDED_RESULT;
        }
        flow_host_finish(run, FLOW_HOST_FINISHED);
    }
    if (!run->waiting_node) {
        return FLOW_OK;
    }
    if (run->current_time < run->wake_time) {
        run->state = FLOW_SUSPENDED;
        return FLOW_SUSPENDED_RESULT;
    }

    node *node = run->waiting_node;
    int output_pin = run->waiting_output;
    run->waiting_node = NULL;
    run->waiting_output = -1;
    if (run->compiled) {
        return FLOW_OK;
    }
    if (schedule_output_for_node(run, node, output_pin)) {
        return run->error.code;
    }
    return FLOW_OK;
}

static void frame_arrays_free(flow_run *run)
{
    for (int i = 0; i < acount(run->frames); i++) {
        if (run->frames[i].kind == ACTIVATION_FOREACH) {
            arena_free(&run->frames[i].array_storage);
        }
    }
}

static bool flow_run_observed(const flow_run *run);

static void flow_run_release_call(flow_run *run, flow_run *child)
{
    if (!run->instance || !run->instance->pool_enabled || flow_run_observed(child)) {
        flow_run_free(child);
        return;
    }
    for (int i = 0; i < acount(run->call_pool); i++) {
        if (run->call_pool[i]->invocation == child->invocation) {
            flow_run_free(child);
            return;
        }
    }
    flow_run_stop(child);
    flow_run_recycle(child);
    apush(run->call_pool, child);
}

static void flow_run_release_children(flow_run *run)
{
    for (int i = 0; i < acount(run->children); i++) {
        flow_run_release_call(run, run->children[i]);
    }
    aclear(run->children);
    run->active_child = NULL;
    run->call_node = NULL;
    run->returned = NULL;
}

static _Thread_local uint64_t oracle_calls;

uint64_t flow_oracle_calls()
{
    return oracle_calls;
}

behavior_backend flow_run_backend(const flow_run *run)
{
    return run ? run->backend : BEHAVIOR_UNAVAILABLE;
}

static int flow_run_activate(flow_run *run)
{
    if (run->steps >= run->step_budget) {
        node *n = acount(run->frames)
                    ? frame_node(run, &run->frames[acount(run->frames) - 1])
                    : NULL;
        flow_fail(run, FLOW_ERR_STEP_BUDGET, n, "flow step budget exceeded");
        return FLOW_ERR_STEP_BUDGET;
    }

    /* Each synchronous node activation gets a fresh pull-eval memo. Runtime
     * values and the arena remain live for the run so flow data outputs and
     * pointer-bearing values can cross activation boundaries safely. */
    runtime_store_reset_memo(run->runtime);
    eval_context_reset(&run->eval);
    run->eval.services = run->services;
    /* Allowing authored breakpoints is not an inspection subscription. Node
     * break checks stay active independently of data-edge observation. */
    run->eval.on_data_edge = !run->instance || run->instance->trace_enabled
                               ? record_evaluated_data_edge
                               : NULL;

    activation_frame frame = apop(run->frames);
    frame.node = frame_node(run, &frame);
    run->steps++;
    if (frame.kind == ACTIVATION_WHILE) {
        flow_context ctx = { .run = run, .node = frame.node, .input_pin = -1 };
        return frame.break_requested
                 ? flow_context_continue(&ctx, frame.completed_output)
                 : flow_context_while(
                       &ctx,
                       frame.index_output,
                       frame.body_output,
                       frame.completed_output
                   );
    }
    if (frame.kind == ACTIVATION_LOOP || frame.kind == ACTIVATION_FOREACH) {
        if (frame.kind == ACTIVATION_FOREACH) {
            if (frame.break_requested || !frame.array || frame.current > frame.last) {
                flow_watch_capture_outputs(run, frame.node);
                arena_free(&frame.array_storage);
                return schedule_output_for_node(run, frame.node, frame.completed_output);
            }
            int index = frame.current;
            runtime_node *state = flow_runtime_node(run, frame.node);
            pin_value *outputs = runtime_node_outputs(state);
            runtime_node_set_computed(state, 0);
            outputs[frame.index_output] = (pin_value){
                .type = PIN_I32,
                .type_name = "int",
                .i = index,
            };
            outputs[frame.index_output].container = PIN_CONTAINER_SINGLE;
            if (pin_value_snapshot_to_arena(
                    &run->scratch,
                    &outputs[frame.value_output],
                    &frame.array->items[index]
                )) {
                arena_free(&frame.array_storage);
                flow_fail(run, FLOW_ERR_NODE, frame.node, "ForEach element copy failed");
                return FLOW_ERR_NODE;
            }
            if (frame.current < frame.last) {
                frame.current++;
            } else {
                frame.current = frame.last + 1;
            }
            push_frame(run, frame);
            flow_watch_capture_outputs(run, frame.node);
            return schedule_output_for_node(run, frame.node, frame.body_output);
        }
        if (frame.break_requested || frame.current == frame.last) {
            flow_watch_capture_outputs(run, frame.node);
            return schedule_output_for_node(run, frame.node, frame.completed_output);
        }
        frame.current += frame.current < frame.last ? 1 : -1;
        runtime_node *state = flow_runtime_node(run, frame.node);
        pin_value *outputs = runtime_node_outputs(state);
        runtime_node_set_computed(state, 0);
        outputs[frame.index_output].type = PIN_I32;
        outputs[frame.index_output].type_name = "int";
        outputs[frame.index_output].i = frame.current;
        push_frame(run, frame);
        flow_watch_capture_outputs(run, frame.node);
        return schedule_output_for_node(run, frame.node, frame.body_output);
    }

    node *n = frame.node;
    if (!n || !n->desc || !n->desc->flow) {
        flow_fail(run, FLOW_ERR_INVALID_NODE, n, "node has no flow evaluator");
        return FLOW_ERR_INVALID_NODE;
    }

    if (n->disabled) {
        for (int i = 0; i < n->output_count; i++) {
            if (n->output_pins[i].kind != PIN_DATA) {
                continue;
            }
            runtime_node *state = flow_runtime_node(run, n);
            runtime_node_outputs(state)[i] = (pin_value){
                .type = n->output_pins[i].type,
                .type_name = n->output_pins[i].type_name
            };
        }
        flow_watch_capture(run, n);
        for (int i = 0; i < n->output_count; i++) {
            if (n->output_pins[i].kind == PIN_EXEC) {
                return schedule_output_for_node(run, n, i);
            }
        }
        return FLOW_OK;
    }

    flow_context ctx = {
        .run = run,
        .node = n,
        .input_pin = frame.input_pin,
    };
    int result;
    if (run->program) {
        result = behavior_program_flow(run->program, n, &ctx, run->backend);
    } else {
        oracle_calls++;
        result = n->desc->flow(n, &ctx);
    }
    if (ctx.failed || result < 0) {
        if (run->state != FLOW_FAILED) {
            flow_fail(run, result < 0 ? result : FLOW_ERR_NODE, n, "flow node failed");
        }
        return run->error.code;
    }
    if (ctx.inputs_unchanged) {
        flow_watch_capture_outputs(run, n);
    } else {
        flow_watch_capture(run, n);
    }
    if (run->active_child) {
        return run->state == FLOW_PAUSED ? FLOW_PAUSED_RESULT : FLOW_SUSPENDED_RESULT;
    }
    if (run->host_request) {
        run->state = FLOW_SUSPENDED;
        return FLOW_SUSPENDED_RESULT;
    }
    if (ctx.pause_requested) {
        run->waiting_node = n;
        run->waiting_output = ctx.pause_output;
        run->wake_time = ctx.pause_time;
        run->state = FLOW_SUSPENDED;
        return FLOW_SUSPENDED_RESULT;
    }
    return FLOW_OK;
}

static int execute(flow_run *run)
{
    if (flow_run_check_topology(run)) {
        return FLOW_ERR_TOPOLOGY;
    }
    int waiting = flow_run_wait(run);
    if (waiting) {
        return waiting;
    }

    if (run->compiled) {
        return flow_compiled_execute(run);
    }

    if (run->program && run->instance && run->steps == 0 && acount(run->frames) == 1) {
        node *entry = frame_node(run, &run->frames[0]);
        const behavior_function *function = behavior_program_function(run->program, entry);
        behavior_function_context host = { 0 };
        bool uninspected = !run->instance->trace_enabled && !run->step_mode &&
                           !run->pause_requested && !acount(run->watches);
        /* The debugger's target records every wire and value; code compiled
           without observers records none, so the target interprets it. */
        bool interpret = function && run->instance->debug_target &&
                         run->instance->trace_enabled &&
                         !behavior_function_observed(function);
        if (function && !interpret &&
            (uninspected || behavior_function_resumable(function)) &&
            behavior_function_bind(
                function,
                &host,
                run->services,
                behavior_instance_owner(run->instance).u64
            )) {
            if (behavior_function_resumable(function)) {
                flow_compiled_start(run, function);
                return flow_compiled_execute(run);
            }
            node *ret = graph_node_by_id(run->graph, run->graph->return_id);
            behavior_function_context ctx = {
                .arguments = run->inputs,
                .argument_count = run->input_count,
                .results = ret ? runtime_node_inputs(flow_runtime_node(run, ret)) : NULL,
                .delta_seconds = run->eval.delta_seconds,
                .budget = run->step_budget,
                .call_depth = run->call_depth,
                .owner = host.owner,
                .world = host.world,
                .binding_world = host.binding_world,
                .services = host.services,
            };
            behavior_instance_bind_values(run->instance, &ctx);
            behavior_function_prepare_data(function, run->services, &run->data_storage);
            ctx.data = run->data_storage;
            behavior_function_bind_storage(&ctx, &run->scratch);
            behavior_function_prepare_results(function, ctx.results, &run->scratch);
            int result = behavior_function_run(function, &ctx, run->backend == BEHAVIOR_SLJIT);
            flow_run_finish_compiled(run, function, &ctx, result);
            return result;
        }
    }

    while (run->state == FLOW_RUNNING && acount(run->frames) > 0) {
        node *next = frame_node(run, &run->frames[acount(run->frames) - 1]);
        if (next && next->breakpoint && run->breakpoints_enabled &&
            !run->breakpoint_skipped) {
            run->breakpoint_node = next;
            run->state = FLOW_PAUSED;
            return FLOW_PAUSED_RESULT;
        }
        run->breakpoint_skipped = 0;
        run->breakpoint_node = NULL;
        if (run->pause_requested) {
            run->pause_requested = 0;
            run->state = FLOW_PAUSED;
            return FLOW_PAUSED_RESULT;
        }
        int result = flow_run_activate(run);
        if (result) {
            return result;
        }
        if (run->step_mode && run->activation_budget && --run->activation_budget == 0) {
            run->state = FLOW_PAUSED;
            return FLOW_PAUSED_RESULT;
        }
    }
    if (run->state == FLOW_RUNNING && acount(run->frames) == 0) {
        run->state = FLOW_COMPLETED;
    }
    return run->state == FLOW_FAILED ? run->error.code : FLOW_OK;
}

static _Thread_local behavior_profile_scope *active_profile;

static uint64_t profile_now()
{
    struct timespec now;
    int result = clock_gettime(CLOCK_MONOTONIC, &now);
    assert(result == 0);
    (void)result;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

void behavior_profile_enter(behavior_profile_scope *scope)
{
    *scope = (behavior_profile_scope){ .parent = active_profile,
                                       .start = profile_now() };
    active_profile = scope;
}

uint64_t behavior_profile_leave(behavior_profile_scope *scope)
{
    uint64_t elapsed = profile_now() - scope->start;
    active_profile = scope->parent;
    if (scope->parent) {
        scope->parent->children += elapsed;
    }
    assert(elapsed >= scope->children);
    return elapsed - scope->children;
}

static int flow_run_execute(flow_run *run)
{
    behavior_instance *instance = run->instance;
    const flow_run *leaf = active_run(run);
    bool waiting = (leaf->host_request && leaf->host_request->state == FLOW_HOST_PENDING) ||
                   (leaf->waiting_node && run->current_time < leaf->wake_time);
    if (run->parent || !instance || !instance->profile || waiting) {
        return execute(run);
    }
    behavior_profile_scope scope;
    behavior_profile_enter(&scope);
    bool first = !run->profile_started;
    run->profile_started = true;
    int result = execute(run);
    uint64_t elapsed = behavior_profile_leave(&scope);
    instance->profile(
        run->event_name,
        elapsed,
        elapsed + scope.children,
        first ? BEHAVIOR_PROFILE_START : BEHAVIOR_PROFILE_RESUME,
        instance->profile_user
    );
    return result;
}

flow_run *flow_run_start(graph *g, node *entry, flow_inputs inputs, const flow_options *options, flow_error *error)
{
    set_error(error, FLOW_OK, NULL, NULL);
    if (!graph_has_node(g, entry) || !entry->desc || !entry->desc->flow) {
        set_error(error, FLOW_ERR_INVALID_NODE, entry, "invalid flow entry node");
        return NULL;
    }
    if (inputs.count < 0 || (inputs.count > 0 && !inputs.values)) {
        set_error(error, FLOW_ERR_ARGUMENT, entry, "invalid flow inputs");
        return NULL;
    }

    flow_run *run = xcalloc(1, sizeof *run);
    run->graph = g;
    run->state = FLOW_IDLE;
    eval_context_init(&run->eval);
    /* Arenas allocate on first use and retain only the blocks this activation
     * needed. Most compiled events have no copied payload or scratch values. */
    if (!flow_run_start_reuse(run, g, entry, inputs, options, error)) {
        flow_run_free(run);
        return NULL;
    }
    return run;
}

flow_run *flow_run_start_reuse(
    flow_run *run,
    graph *g,
    node *entry,
    flow_inputs inputs,
    const flow_options *options,
    flow_error *error
)
{
    set_error(error, FLOW_OK, NULL, NULL);
    if (!run || run->state != FLOW_IDLE || run->graph != g ||
        !graph_has_node(g, entry) || !entry->desc || !entry->desc->flow) {
        set_error(error, FLOW_ERR_INVALID_NODE, entry, "invalid reusable flow entry");
        return NULL;
    }
    if (inputs.count < 0 || (inputs.count > 0 && !inputs.values)) {
        set_error(error, FLOW_ERR_ARGUMENT, entry, "invalid flow inputs");
        return NULL;
    }
    run->instance = options ? options->instance : NULL;
    run->backend = run->instance ? run->instance->backend : BEHAVIOR_ORACLE;
    run->program = NULL;
    if (run->instance && run->backend != BEHAVIOR_ORACLE) {
        behavior_program *program = behavior_program_publish(run->instance->asset);
        const compile_diagnostic *d = behavior_program_diagnostic(program);
        if (d) {
            set_error(error, FLOW_ERR_COMPILE, entry, d->message);
            return NULL;
        }
        run->program = behavior_program_graph(program, g);
        if (!run->program) {
            set_error(error, FLOW_ERR_COMPILE, entry, "compiled activation graph is missing");
            return NULL;
        }
    }
    run->graph_guard_owned = run->instance == NULL;
    run->state = FLOW_RUNNING;
    run->step_budget = options && options->step_budget ? options->step_budget
                                                       : FLOW_DEFAULT_STEP_BUDGET;
    run->now = options ? options->now : NULL;
    run->now_user = options ? options->now_user : NULL;
    run->current_time = options ? options->initial_time : 0.0;
    run->wake_time = 0.0;
    run->entry_output = 0;
    run->return_input = 0;
    run->waiting_node = NULL;
    run->waiting_output = -1;
    run->graph_paused = 0;
    run->pause_requested = 0;
    run->step_mode = 0;
    run->activation_budget = 0;
    run->call_depth = options ? options->call_depth : 0;
    run->step_over = 0;
    run->returned = NULL;
    run->breakpoints_enabled = options && options->breakpoints_override
                                 ? options->breakpoints_enabled
                                 : g->breakpoints_enabled != 0;
    run->breakpoint_node = NULL;
    run->breakpoint_skipped = 0;
    run->sink = options ? options->sink : NULL;
    run->sink_user = options ? options->sink_user : NULL;
    set_event_name(run, options ? options->event_name : NULL);
    run->steps = 0;
    run->whole_function_executed = false;
    run->whole_function = NULL;
    run->whole_error_stack = -1;
    run->profile_started = false;
    run->topology_revision = graph_topology_revision(g);
    run->input_count = inputs.count;
    run->input_user_data = inputs.user_data;
    run->error = (flow_error){ 0, NULL, -1, NULL, 0 };
    aclear(run->frames);
    aclear(run->stack_cache);
    aclear(run->data_error_stack);
    aclear(run->services);
    if (!run->runtime) {
        run->runtime = runtime_store_create(g);
        eval_context_attach_runtime(&run->eval, run->runtime);
    }
    runtime_store_reset(run->runtime);
    eval_context_reset(&run->eval);
    run->eval.delta_seconds = options ? options->delta_seconds : run->eval.delta_seconds;
    run->eval.event_entry = entry;
    run->eval.instance = run->instance;
    run->eval.program = run->program;
    run->eval.observer = run;
    run->eval.backend = run->backend;
    run->eval.on_data_edge = record_evaluated_data_edge;
    run->eval.on_data_edge_user = run;
    arena_reset(&run->input_arena);
    arena_reset(&run->scratch);
    if (inputs.count > run->input_capacity) {
        run->inputs = (pin_value *)
            xrealloc(run->inputs, (size_t)inputs.count * sizeof *run->inputs);
        run->input_capacity = inputs.count;
    }
    if (inputs.count) {
        memset(run->inputs, 0, (size_t)inputs.count * sizeof *run->inputs);
        for (int i = 0; i < inputs.count; i++) {
            if (pin_value_snapshot_to_arena(
                    &run->input_arena,
                    &run->inputs[i],
                    &inputs.values[i]
                )) {
                set_error(error, FLOW_ERR_NODE, entry, "flow input copy failed");
                flow_run_stop(run);
                return NULL;
            }
        }
    }
    /* The event payload is authoritative when the event declares the
     * conventional DeltaSeconds output. */
    (void)flow_event_delta(entry, run->inputs, run->input_count, &run->eval.delta_seconds);
    push_frame(run, (activation_frame){ .node = entry, .kind = ACTIVATION_BLOCK, .input_pin = -1 });
    if (run->graph_guard_owned) {
        graph_pause(run);
    }
    if (run->instance) {
        for (int i = 0; i < acount(run->instance->services); i++) {
            apush(run->services, run->instance->services[i]);
        }
    }
    return run;
}

int flow_run_recycle(flow_run *run)
{
    if (!run) {
        return FLOW_ERR_ARGUMENT;
    }
    if (run->state != FLOW_IDLE && run->state != FLOW_COMPLETED &&
        run->state != FLOW_FAILED) {
        return FLOW_ERR_STATE;
    }
    flow_run_stop(run);
    aclear(run->traversals);
    run->traversal_head = 0;
    run->traversal_view_dirty = false;
    run->traversal_revision++;
    arena_reset(&run->input_arena);
    arena_reset(&run->scratch);
    run->input_count = 0;
    run->input_user_data = NULL;
    run->steps = 0;
    run->whole_function_executed = false;
    run->whole_function = NULL;
    run->whole_error_stack = -1;
    return FLOW_OK;
}

flow_run *graph_run_event(graph *g, const char *name, flow_inputs inputs, flow_error *error)
{
    set_error(error, FLOW_OK, NULL, NULL);
    if (!g || !name || !name[0]) {
        set_error(error, FLOW_ERR_ARGUMENT, NULL, "event name is required");
        return NULL;
    }
    node *found = NULL;
    for (int i = 0; i < acount(g->nodes); i++) {
        node *n = g->nodes[i];
        if (n->desc->flow_kind != NODE_FLOW_EVENT || !n->reference_name ||
            strcmp(n->reference_name, name) != 0) {
            continue;
        }
        if (found) {
            set_error(error, FLOW_ERR_DUPLICATE_EVENT, n, "duplicate event name");
            return NULL;
        }
        found = n;
    }
    if (!found) {
        set_error(error, FLOW_ERR_UNKNOWN_EVENT, NULL, "unknown event name");
        return NULL;
    }
    flow_run *run = flow_run_start(g, found, inputs, NULL, error);
    if (!run) {
        return NULL;
    }
    flow_run_resume(run);
    return run;
}

bool flow_run_owner_valid(const flow_run *run)
{
    return !run->instance || !run->instance->activation_valid ||
           run->instance->activation_valid(run->instance);
}

int flow_run_continue(flow_run *run, int clear_step_mode)
{
    if (!run) {
        return FLOW_ERR_ARGUMENT;
    }
    if (run->state == FLOW_IDLE) {
        return FLOW_ERR_STATE;
    }
    if (run->state == FLOW_COMPLETED || run->state == FLOW_FAILED) {
        return run->error.code;
    }
    if (run->breakpoint_node && acount(run->frames) &&
        frame_node(run, &run->frames[acount(run->frames) - 1]) == run->breakpoint_node) {
        run->breakpoint_skipped = 1;
    }
    run->state = FLOW_RUNNING;
    if (clear_step_mode) {
        if (run->compiled) {
            flow_compiled_resume(run);
        }
        run->step_mode = 0;
        run->activation_budget = 0;
        run->step_over = 0;
        for (flow_run *child = run->active_child; child; child = child->active_child) {
            child->step_mode = 0;
            child->activation_budget = 0;
            child->step_over = 0;
        }
    } else if (run->step_mode) {
        if (run->activation_budget == 0) {
            run->activation_budget = 1;
        }
    } else {
        run->activation_budget = 0;
    }
    behavior_instance *instance = run->instance;
    bool synchronize = instance && instance->activation_enter;
    if (synchronize && instance->activation_depth++ == 0 &&
        !instance->activation_enter(instance)) {
        instance->activation_depth--;
        flow_run_stop(run);
        return FLOW_OK;
    }
    int result = flow_run_execute(run);
    if (synchronize && --instance->activation_depth == 0) {
        instance->activation_leave(instance);
    }
    return result;
}

int flow_run_resume(flow_run *run)
{
    if (run && run->instance) {
        run->instance->schedule_revision++;
    }
    return flow_run_continue(run, 1);
}

int flow_run_tick_at(flow_run *run, double monotonic_time)
{
    if (!run) {
        return FLOW_ERR_ARGUMENT;
    }
    run->current_time = monotonic_time;
    return flow_run_continue(run, 0);
}

int flow_run_pause(flow_run *run)
{
    if (!run) {
        return FLOW_ERR_ARGUMENT;
    }
    if (run->active_child) {
        return flow_run_pause(run->active_child);
    }
    if (run->state == FLOW_RUNNING) {
        run->pause_requested = 1;
        return FLOW_OK;
    }
    if (run->state == FLOW_SUSPENDED) {
        if (run->compiled) {
            flow_compiled_pause(run);
            return FLOW_OK;
        }
        run->step_mode = 1;
        run->activation_budget = 1;
        return FLOW_OK;
    }
    if (run->state == FLOW_PAUSED) {
        return FLOW_OK;
    }
    return FLOW_ERR_STATE;
}

typedef enum
{
    STEP_INTO,
    STEP_OVER,
    STEP_OUT,
} step_kind;

static bool inside_call(const flow_run *run)
{
    return run->active_child ||
           (run->compiled && flow_compiled_active_frame(run) > 0);
}

/* Every run on the active call chain advances one activation. Over keeps the
   calls the innermost run makes from stepping; Out lets the innermost run
   finish, so its caller pauses once the call returns. */
static int step(flow_run *run, step_kind kind)
{
    if (!run) {
        return FLOW_ERR_ARGUMENT;
    }
    if (run->state == FLOW_IDLE) {
        return FLOW_ERR_STATE;
    }
    if (run->state == FLOW_COMPLETED || run->state == FLOW_FAILED) {
        return run->error.code;
    }
    if (kind == STEP_OUT && !inside_call(run)) {
        return flow_run_resume(run);
    }
    if (run->breakpoint_node && acount(run->frames) &&
        frame_node(run, &run->frames[acount(run->frames) - 1]) == run->breakpoint_node) {
        run->breakpoint_skipped = 1;
    }
    run->state = FLOW_RUNNING;
    run->pause_requested = 0;
    flow_run *leaf = run;
    for (flow_run *r = run; r; r = r->active_child) {
        r->step_mode = 1;
        r->activation_budget = 1;
        r->step_over = kind == STEP_OVER;
        leaf = r;
    }
    if (kind == STEP_OUT) {
        leaf->step_mode = 0;
        leaf->activation_budget = 0;
    }
    if (run->compiled) {
        flow_compiled_step(run, kind == STEP_OUT);
    }
    int result = flow_run_execute(run);
    for (flow_run *r = run; r; r = r->active_child) {
        r->step_over = 0;
    }
    return result;
}

int flow_run_step(flow_run *run)
{
    return step(run, STEP_INTO);
}

int flow_run_step_over(flow_run *run)
{
    return step(run, STEP_OVER);
}

int flow_run_step_out(flow_run *run)
{
    return step(run, STEP_OUT);
}

int flow_run_stop(flow_run *run)
{
    if (!run) {
        return FLOW_ERR_ARGUMENT;
    }
    if (run->state == FLOW_IDLE) {
        return FLOW_OK;
    }
    flow_host_detach(run);
    flow_compiled_stop(run);
    run->state = FLOW_IDLE;
    frame_arrays_free(run);
    aclear(run->frames);
    aclear(run->data_error_stack);
    run->waiting_node = NULL;
    run->waiting_output = -1;
    run->pause_requested = 0;
    run->step_mode = 0;
    run->activation_budget = 0;
    run->breakpoint_node = NULL;
    run->breakpoint_skipped = 0;
    flow_run_release_children(run);
    runtime_store_reset(run->runtime);
    eval_context_reset(&run->eval);
    flow_watch_clear(run);
    run->error = (flow_error){ 0, NULL, -1, NULL, run->steps };
    if (run->graph_guard_owned) {
        graph_resume(run);
    }
    return FLOW_OK;
}

int flow_run_reset(flow_run *run)
{
    if (!run) {
        return FLOW_ERR_ARGUMENT;
    }
    flow_run_stop(run);
    flow_compiled_free(run);
    behavior_function_free_data(run->data_storage);
    run->data_storage = NULL;
    for (int i = 0; i < acount(run->call_pool); i++) {
        flow_run_free(run->call_pool[i]);
    }
    afree(run->call_pool);
    afree(run->children);
    runtime_store_reset(run->runtime);
    eval_context_reset(&run->eval);
    frame_arrays_free(run);
    afree(run->frames);
    afree(run->stack_cache);
    afree(run->data_error_stack);
    afree(run->services);
    free(run->inputs);
    run->inputs = NULL;
    run->input_count = 0;
    run->input_capacity = 0;
    run->input_user_data = NULL;
    arena_free(&run->input_arena);
    arena_free(&run->scratch);
    eval_context_detach_runtime(&run->eval);
    runtime_store_free(run->runtime);
    run->runtime = NULL;
    flow_watch_clear(run);
    afree(run->traversals);
    afree(run->traversal_view);
    run->traversal_head = 0;
    run->traversal_view_dirty = false;
    run->traversal_revision++;
    run->steps = 0;
    run->whole_function_executed = false;
    run->whole_function = NULL;
    run->whole_error_stack = -1;
    run->topology_revision = graph_topology_revision(run->graph);
    run->error = (flow_error){ 0, NULL, -1, NULL, 0 };
    return FLOW_OK;
}

int flow_run_ready(const flow_run *run, double now)
{
    run = flow_run_view(run);
    if (!run) {
        return 0;
    }
    if (run->state == FLOW_FAILED || run->state == FLOW_IDLE ||
        run->state == FLOW_COMPLETED) {
        return 0;
    }
    if (run->active_child) {
        return flow_run_ready(run->active_child, now);
    }
    if (run->state == FLOW_RUNNING) {
        return 1;
    }
    if (run->state == FLOW_SUSPENDED) {
        if (run->host_request) {
            return run->host_request->state == FLOW_HOST_READY;
        }
        return now >= run->wake_time;
    }
    return 0;
}

bool flow_run_waiting_for_host(const flow_run *run)
{
    run = flow_run_view(run);
    return run && (run->host_request != NULL ||
                   flow_run_waiting_for_host(run->active_child));
}

const flow_run *flow_run_active_leaf(const flow_run *run)
{
    run = active_run(run);
    return run && (run->compiled || run->compiled_owner) ? flow_compiled_leaf(run)
                                                         : run;
}

flow_state flow_run_status(const flow_run *run)
{
    run = flow_run_view(run);
    if (run && (run->state == FLOW_FAILED || run->state == FLOW_IDLE)) {
        return run->state;
    }
    run = active_run(run);
    return run ? run->state : FLOW_IDLE;
}

double flow_run_wake_time(const flow_run *run)
{
    run = flow_run_view(active_run(run));
    return run && run->compiled ? flow_compiled_wake_time(run)
         : run                  ? run->wake_time
                                : 0.0;
}

const flow_error *flow_run_error(const flow_run *run)
{
    run = flow_run_view(run);
    return run ? &run->error : NULL;
}

unsigned long flow_run_steps(const flow_run *run)
{
    run = flow_run_view(run);
    return run ? run->steps : 0;
}

flow_inputs flow_run_inputs(const flow_run *run)
{
    run = flow_run_view(run);
    return run ? (flow_inputs){ run->inputs, run->input_count, run->input_user_data }
               : (flow_inputs){ NULL, 0, NULL };
}

const pin_value *flow_run_input_value(const flow_run *run, const node *n, int pin)
{
    run = flow_run_view(run);
    if (run && (run->compiled || run->compiled_owner) && n && pin >= 0 &&
        pin < n->input_count) {
        const pin_value *value = flow_watch_input_value(run, n, pin);
        if (value) {
            return value;
        }
    }
    if (run && run->compiled_owner) {
        return NULL;
    }
    if (!run || !n || pin < 0 || pin >= n->input_count ||
        (run->whole_function_executed && n != run->returned)) {
        return NULL;
    }
    const runtime_node *state = flow_runtime_node((flow_run *)run, n);
    return &runtime_node_inputs_const(state)[pin];
}

uint64_t flow_run_traversal_revision(const flow_run *run)
{
    return run ? run->traversal_revision : 0;
}

const flow_traversal *flow_run_traversals(const flow_run *run, int *count)
{
    if (count) {
        *count = run ? acount(run->traversals) : 0;
    }
    if (!run || !run->traversal_head) {
        return run ? run->traversals : NULL;
    }
    flow_run *mutable_run = (flow_run *)run;
    if (run->traversal_view_dirty) {
        int tail = FLOW_TRAVERSAL_MAX - run->traversal_head;
        asetlen(mutable_run->traversal_view, FLOW_TRAVERSAL_MAX);
        memcpy(
            mutable_run->traversal_view,
            run->traversals + run->traversal_head,
            (size_t)tail * sizeof *run->traversals
        );
        memcpy(
            mutable_run->traversal_view + tail,
            run->traversals,
            (size_t)run->traversal_head * sizeof *run->traversals
        );
        mutable_run->traversal_view_dirty = false;
    }
    return run->traversal_view;
}

const node *flow_run_current_node(const flow_run *run)
{
    run = flow_run_view(active_run(run));
    if (run && run->compiled) {
        return flow_compiled_current(run);
    }
    if (run && run->state == FLOW_FAILED && run->error.node) {
        return run->error.node;
    }
    if (run && run->compiled_owner) {
        return run->inspection_node;
    }
    if (!run || !acount(run->frames)) {
        return NULL;
    }
    return frame_node(run, &run->frames[acount(run->frames) - 1]);
}

const char *flow_run_event_name(const flow_run *run)
{
    return run ? run->event_name : NULL;
}

void flow_run_data_call(
    flow_run *run,
    const graph *caller,
    const node *call,
    const graph *callee,
    const node *failed
)
{
    if (!run) {
        return;
    }
    if (!acount(run->data_error_stack)) {
        apush(
            run->data_error_stack,
            ((flow_call_frame){ callee, callee->graph_id, failed->id })
        );
    }
    apush(run->data_error_stack, ((flow_call_frame){ caller, caller->graph_id, call->id }));
}
const flow_call_frame *flow_run_call_stack(const flow_run *run, int *count)
{
    run = flow_run_view(run);
    if (count) {
        *count = 0;
    }
    if (!run) {
        return NULL;
    }
    flow_run *mutable_run = (flow_run *)run;
    aclear(mutable_run->stack_cache);
    if (run->compiled || run->compiled_owner) {
        flow_compiled_build_stack(mutable_run);
        if (count) {
            *count = acount(mutable_run->stack_cache);
        }
        return mutable_run->stack_cache;
    }
    if (run->whole_function_executed && run->state == FLOW_FAILED) {
        for (int frame = run->whole_error_stack; frame >= 0;) {
            const behavior_function_source
                *source = behavior_function_stack_at(run->whole_function, frame);
            apush(
                mutable_run->stack_cache,
                ((flow_call_frame){ source->graph,
                                    source->graph->graph_id,
                                    source->call->id })
            );
            frame = source->parent;
        }
        int length = acount(mutable_run->stack_cache);
        for (int i = 0; i < length / 2; i++) {
            flow_call_frame swap = mutable_run->stack_cache[i];
            mutable_run->stack_cache[i] = mutable_run->stack_cache[length - i - 1];
            mutable_run->stack_cache[length - i - 1] = swap;
        }
        if (count) {
            *count = length;
        }
        return mutable_run->stack_cache;
    }
    for (int i = 0; i < acount(run->frames); i++) {
        node *n = frame_node(run, &run->frames[i]);
        apush(
            mutable_run->stack_cache,
            (flow_call_frame){ run->graph, run->graph->graph_id, n ? n->id : 0 }
        );
    }
    if (run->active_child) {
        apush(
            mutable_run->stack_cache,
            (flow_call_frame){ run->graph, run->graph->graph_id, run->call_node->id }
        );
        int child_count = 0;
        const flow_call_frame *children = flow_run_call_stack(run->active_child, &child_count);
        for (int i = 0; i < child_count; i++) {
            apush(mutable_run->stack_cache, children[i]);
        }
    }
    for (int i = acount(run->data_error_stack) - 1; i >= 0; i--) {
        apush(mutable_run->stack_cache, run->data_error_stack[i]);
    }
    if (count) {
        *count = acount(mutable_run->stack_cache);
    }
    return mutable_run->stack_cache;
}

unsigned flow_run_call_depth(const flow_run *run)
{
    run = flow_run_view(run);
    return run ? run->call_depth : 0;
}

unsigned flow_context_call_depth(const struct flow_context *ctx)
{
    return ctx && ctx->run ? ctx->run->call_depth : 0;
}

int flow_context_input_pin(const struct flow_context *ctx)
{
    return ctx ? ctx->input_pin : -1;
}

pin_value *flow_context_input_value(struct flow_context *ctx, int pin)
{
    if (!ctx || !ctx->run || !ctx->node || pin < 0 || pin >= ctx->node->input_count) {
        return NULL;
    }
    return &runtime_node_inputs(flow_runtime_node(ctx->run, ctx->node))[pin];
}

pin_value *flow_context_output_value(struct flow_context *ctx, int pin)
{
    if (!ctx || !ctx->run || !ctx->node || pin < 0 || pin >= ctx->node->output_count) {
        return NULL;
    }
    return &runtime_node_outputs(flow_runtime_node(ctx->run, ctx->node))[pin];
}

void *flow_context_service(const struct flow_context *ctx, const char *type_name)
{
    if (!ctx || !ctx->run || !type_name) {
        return NULL;
    }
    for (int i = 0; i < acount(ctx->run->services); i++) {
        if (strcmp(ctx->run->services[i].type_name, type_name) == 0) {
            return ctx->run->services[i].value;
        }
    }
    return NULL;
}

void flow_run_free(flow_run *run)
{
    if (!run) {
        return;
    }
    if (run->instance && !run->parent) {
        behavior_instance_remove_activation(run->instance, run);
    }
    flow_run_reset(run);
    free(run->event_name);
    free(run);
}

double flow_context_now(const struct flow_context *ctx)
{
    return ctx && ctx->run ? ctx->run->current_time : 0.0;
}

int flow_context_eval_data(struct flow_context *ctx, int input_pin, pin_value *out)
{
    if (!ctx || !ctx->run || !out || input_pin < 0 ||
        input_pin >= ctx->node->input_count) {
        return flow_context_fail(ctx, FLOW_ERR_ARGUMENT, "invalid data input");
    }
    const pin_spec *spec = &ctx->node->input_pins[input_pin];
    if (spec->kind != PIN_DATA) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_NODE, "EXEC input used as data");
    }

    if (ctx->run->program) {
        int result = behavior_program_input(
            ctx->run->program,
            ctx->node,
            input_pin,
            &ctx->run->scratch,
            out,
            &ctx->run->eval,
            ctx->run->backend
        );
        if (result) {
            const eval_status *status = &ctx->run->eval.status;
            /* A binding on the executing node fails at the flow boundary.
             * Preserve the oracle's code and pin when inspection selects the
             * general compiled path. Errors in upstream expressions retain
             * their data-node source. */
            bool binding = spec->binding && status->node == ctx->node &&
                           status->pin == input_pin;
            flow_context_fail(
                ctx,
                binding ? FLOW_ERR_NODE : FLOW_ERR_INVALID_GRAPH,
                status->message
            );
            ctx->run->error.node = status->node;
            ctx->run->error.pin = binding ? -1 : status->pin;
            return ctx->run->error.code;
        }
        return FLOW_OK;
    }
    node *src = NULL;
    int src_pin = 0;
    if (graph_input_source(ctx->run->graph, ctx->node, input_pin, &src, &src_pin)) {
        record_data_traversal(ctx->run, src, src_pin, ctx->node, input_pin);
        int result = graph_eval_output(
            ctx->run->graph,
            src,
            src_pin,
            &ctx->run->scratch,
            out,
            &ctx->run->eval
        );
        if (result) {
            const eval_status *status = eval_context_status(&ctx->run->eval);
            flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, status->message);
            ctx->run->error.node = status->node;
            ctx->run->error.pin = status->pin;
            return ctx->run->error.code;
        }
        const node_desc *conversion = graph_input_conversion(ctx->run->graph, ctx->node, input_pin);
        if (conversion) {
            if (!conversion->fn) {
                return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "edge conversion has no function");
            }
            *out = conversion->fn(out, 1, &ctx->run->scratch);
            pin_coerce(out, spec);
            if (out->type != spec->type || (out->type == PIN_STRUCT && !out->ptr)) {
                int result = flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "edge conversion failed");
                ctx->run->error.pin = input_pin;
                return result;
            }
        }
        pin_coerce(out, spec);
        *flow_context_input_value(ctx, input_pin) = *out;
        if (src->output_pins[src_pin].flags & PIN_FLAG_WATCH) {
            (void)flow_watch_store(ctx->run, ctx->run->graph, src, 1, src_pin, out);
        }
        return FLOW_OK;
    }
    const char *error = eval_context_resolve_input(
        &ctx->run->eval,
        spec,
        spec->binding,
        &ctx->run->scratch,
        out
    );
    if (error) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, error);
    }
    *flow_context_input_value(ctx, input_pin) = *out;
    return FLOW_OK;
}

int flow_context_eval_output(struct flow_context *ctx, int output_pin, pin_value *out)
{
    if (!ctx || !ctx->run || !out || output_pin < 0 ||
        output_pin >= ctx->node->output_count ||
        ctx->node->output_pins[output_pin].kind != PIN_DATA) {
        return flow_context_fail(ctx, FLOW_ERR_ARGUMENT, "invalid data output");
    }
    int result;
    if (ctx->run->program) {
        result = behavior_program_output(
            ctx->run->program,
            ctx->node,
            output_pin,
            &ctx->run->scratch,
            out,
            &ctx->run->eval,
            ctx->run->backend
        );
    } else {
        result = graph_eval_output(
            ctx->run->graph,
            ctx->node,
            output_pin,
            &ctx->run->scratch,
            out,
            &ctx->run->eval
        );
    }
    if (!result) {
        return FLOW_OK;
    }
    const eval_status *status = eval_context_status(&ctx->run->eval);
    flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, status->message);
    ctx->run->error.node = status->node;
    ctx->run->error.pin = status->pin;
    return ctx->run->error.code;
}

int flow_context_fail(struct flow_context *ctx, int code, const char *message)
{
    if (!ctx || !ctx->run) {
        return code;
    }
    ctx->failed = 1;
    flow_fail(ctx->run, code, ctx->node, message ? message : "flow node failed");
    return code;
}

int flow_context_pause_until(struct flow_context *ctx, double monotonic_time)
{
    if (!ctx || !ctx->run) {
        return FLOW_ERR_ARGUMENT;
    }
    if (monotonic_time <= ctx->run->current_time) {
        return FLOW_OK;
    }
    ctx->pause_requested = 1;
    ctx->pause_time = monotonic_time;
    ctx->pause_output = -1;
    return FLOW_SUSPENDED_RESULT;
}

int flow_context_continue(struct flow_context *ctx, int output_pin)
{
    if (!ctx || !ctx->run) {
        return FLOW_ERR_ARGUMENT;
    }
    if (ctx->pause_requested) {
        ctx->pause_output = output_pin;
        return FLOW_OK;
    }
    return schedule_output(ctx, output_pin);
}

int flow_context_loop_range(
    struct flow_context *ctx,
    int first,
    int last,
    int body_output,
    int completed_output,
    int index_output
)
{
    if (!ctx || !ctx->run) {
        return FLOW_ERR_ARGUMENT;
    }
    if (body_output < 0 || completed_output < 0 || index_output < 0 ||
        body_output >= ctx->node->output_count ||
        completed_output >= ctx->node->output_count ||
        index_output >= ctx->node->output_count ||
        ctx->node->output_pins[body_output].kind != PIN_EXEC ||
        ctx->node->output_pins[completed_output].kind != PIN_EXEC ||
        ctx->node->output_pins[index_output].kind != PIN_DATA) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_NODE, "invalid loop descriptor");
    }
    runtime_node *state = flow_runtime_node(ctx->run, ctx->node);
    runtime_node_set_computed(state, 0);
    runtime_node_outputs(state)[index_output] = (pin_value){
        .type = PIN_I32,
        .type_name = "int",
        .i = first,
    };
    push_frame(
        ctx->run,
        (activation_frame){
            .node = ctx->node,
            .kind = ACTIVATION_LOOP,
            .input_pin = -1,
            .current = first,
            .last = last,
            .body_output = body_output,
            .completed_output = completed_output,
            .index_output = index_output,
            .value_output = -1,
        }
    );
    return flow_context_continue(ctx, body_output);
}

int flow_context_loop(struct flow_context *ctx, int first, int last)
{
    return flow_context_loop_range(ctx, first, last, 0, 2, 1);
}

int flow_context_break(struct flow_context *ctx)
{
    if (!ctx || !ctx->run) {
        return FLOW_ERR_ARGUMENT;
    }
    for (int i = acount(ctx->run->frames) - 1; i >= 0; i--) {
        activation_frame *frame = &ctx->run->frames[i];
        if (frame->kind != ACTIVATION_LOOP && frame->kind != ACTIVATION_FOREACH &&
            frame->kind != ACTIVATION_WHILE) {
            continue;
        }
        frame->break_requested = 1;
        ctx->inputs_unchanged = true;
        return FLOW_OK;
    }
    return flow_context_fail(ctx, FLOW_ERR_STATE, "Break is outside a loop");
}

int flow_context_while(flow_context *ctx, int condition, int body, int completed)
{
    pin_value value;
    int result = flow_context_eval_data(ctx, condition, &value);
    if (result) {
        return result;
    }
    if (value.type != PIN_BOOL) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "WhileLoop condition is not bool");
    }
    if (!value.b) {
        return flow_context_continue(ctx, completed);
    }
    push_frame(
        ctx->run,
        ((activation_frame){ .node = ctx->node,
                             .kind = ACTIVATION_WHILE,
                             .index_output = condition,
                             .body_output = body,
                             .completed_output = completed })
    );
    return flow_context_continue(ctx, body);
}

bool flow_run_retrigger_delay(flow_run *current, const node *waiting, double deadline)
{
    behavior_instance *instance = current->instance;
    assert(instance);
    for (int i = 0; i < acount(instance->activations); i++) {
        flow_run *run = instance->activations[i];
        while (run->active_child) {
            run = run->active_child;
        }
        if (run != current && run->state == FLOW_SUSPENDED &&
            !run->host_request && run->waiting_node == waiting) {
            run->wake_time = deadline;
            if (run->compiled) {
                flow_compiled_retime(run, deadline);
            }
            return true;
        }
    }
    return false;
}

int flow_context_retrigger_delay(flow_context *ctx, double deadline)
{
    if (!ctx->run->instance) {
        return flow_context_fail(ctx, FLOW_ERR_STATE, "Retriggerable Delay requires an instance");
    }
    if (flow_run_retrigger_delay(ctx->run, ctx->node, deadline)) {
        return FLOW_OK;
    }
    int result = flow_context_pause_until(ctx, deadline);
    return result < 0 ? result : flow_context_continue(ctx, 0);
}

int flow_context_foreach(
    struct flow_context *ctx,
    const pin_value *value,
    int body_output,
    int value_output,
    int index_output,
    int completed_output
)
{
    if (!ctx || !ctx->run || !value ||
        value->container != PIN_CONTAINER_ARRAY || !value->ptr) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "ForEach input is not an array");
    }
    const pin_array *source = (const pin_array *)value->ptr;
    arena snapshot = { 0 };
    pin_array *array = pin_array_snapshot_to_arena(&snapshot, source);
    if (!array) {
        arena_free(&snapshot);
        return flow_context_fail(ctx, FLOW_ERR_NODE, "ForEach array copy failed");
    }
    if (body_output < 0 || value_output < 0 || index_output < 0 ||
        completed_output < 0 || body_output >= ctx->node->output_count ||
        value_output >= ctx->node->output_count ||
        index_output >= ctx->node->output_count ||
        completed_output >= ctx->node->output_count) {
        arena_free(&snapshot);
        return flow_context_fail(ctx, FLOW_ERR_INVALID_NODE, "invalid ForEach descriptor");
    }
    runtime_node *state = flow_runtime_node(ctx->run, ctx->node);
    runtime_node_set_computed(state, 0);
    if (!array->count) {
        arena_free(&snapshot);
        return flow_context_continue(ctx, completed_output);
    }
    /* Published values can be read by delayed Completed consumers after the
     * iteration array has been released. Keep their data in the run arena. */
    if (pin_value_snapshot_to_arena(
            &ctx->run->scratch,
            &runtime_node_outputs(state)[value_output],
            &array->items[0]
        )) {
        arena_free(&snapshot);
        return flow_context_fail(ctx, FLOW_ERR_NODE, "ForEach element copy failed");
    }
    runtime_node_outputs(state)[index_output] = (pin_value){ .type = PIN_I32,
                                                             .type_name = "int",
                                                             .i = 0 };
    push_frame(
        ctx->run,
        (activation_frame){
            .node = ctx->node,
            .kind = ACTIVATION_FOREACH,
            .input_pin = -1,
            .current = 1,
            .last = array->count - 1,
            .body_output = body_output,
            .value_output = value_output,
            .completed_output = completed_output,
            .index_output = index_output,
            .array = array,
            .array_storage = snapshot,
        }
    );
    return flow_context_continue(ctx, body_output);
}

int flow_context_emit(struct flow_context *ctx, const char *message)
{
    if (!ctx || !ctx->run || !message) {
        return flow_context_fail(ctx, FLOW_ERR_ARGUMENT, "invalid flow message");
    }
    if (ctx->run->sink) {
        ctx->run->sink(ctx->node, message, ctx->run->sink_user);
    }
    return FLOW_OK;
}

flow_inputs flow_context_inputs(const struct flow_context *ctx)
{
    return ctx && ctx->run ? flow_run_inputs(ctx->run)
                           : (flow_inputs){ NULL, 0, NULL };
}

behavior_instance *flow_context_instance(const struct flow_context *ctx)
{
    return ctx && ctx->run ? ctx->run->instance : NULL;
}

static bool flow_run_observed(const flow_run *run)
{
    if (acount(run->host_requests)) {
        return true;
    }
    for (int i = 0; i < acount(run->children); i++) {
        if (flow_run_observed(run->children[i])) {
            return true;
        }
    }
    return false;
}

static void flow_run_prune_calls(flow_run *run, const node *call)
{
    for (int i = acount(run->children) - 1; i >= 0; i--) {
        flow_run *child = run->children[i];
        if (child->invocation != call || child->state != FLOW_COMPLETED ||
            flow_run_observed(child)) {
            continue;
        }
        flow_run_release_call(run, child);
        memmove(
            run->children + i,
            run->children + i + 1,
            (size_t)(acount(run->children) - i - 1) * sizeof *run->children
        );
        asetlen(run->children, acount(run->children) - 1);
    }
}

int flow_context_call(struct flow_context *ctx, graph *target, flow_inputs inputs)
{
    flow_run *parent = ctx->run;
    assert(!parent->active_child);
    flow_run_prune_calls(parent, ctx->node);
    node *entry = graph_node_by_id(target, target->entry_id);
    flow_options options = {
        .step_budget = (unsigned)parent->step_budget,
        .now = parent->now,
        .now_user = parent->now_user,
        .initial_time = parent->current_time,
        .delta_seconds = parent->eval.delta_seconds,
        .sink = parent->sink,
        .sink_user = parent->sink_user,
        .instance = parent->instance,
        .call_depth = parent->call_depth + 1,
        .breakpoints_override = 1,
        .breakpoints_enabled = parent->breakpoints_enabled,
    };
    flow_error error = { 0 };
    flow_run *child = NULL;
    for (int i = 0; i < acount(parent->call_pool); i++) {
        flow_run *candidate = parent->call_pool[i];
        if (candidate->invocation != ctx->node || candidate->graph != target) {
            continue;
        }
        parent->call_pool[i] = apop(parent->call_pool);
        child = flow_run_start_reuse(candidate, target, entry, inputs, &options, &error);
        if (!child) {
            flow_run_free(candidate);
            return flow_context_fail(ctx, error.code, error.message);
        }
        break;
    }
    if (!child) {
        child = flow_run_start(target, entry, inputs, &options, &error);
    }
    if (!child) {
        return flow_context_fail(ctx, error.code, error.message);
    }
    child->entry_output = 0;
    if (ctx->node->desc == &NODE_CallMacro) {
        int ordinal = 0;
        for (int i = 0; i < ctx->input_pin; i++) {
            ordinal += ctx->node->input_pins[i].kind == PIN_EXEC;
        }
        for (int i = 0; i < entry->output_count; i++) {
            if (entry->output_pins[i].kind == PIN_EXEC && ordinal-- == 0) {
                child->entry_output = i;
                break;
            }
        }
    }
    child->parent = parent;
    child->invocation = ctx->node;
    child->step_mode = parent->step_mode && !parent->step_over;
    child->activation_budget = child->step_mode ? 1 : 0;
    aclear(child->services);
    for (int i = 0; i < acount(parent->services); i++) {
        apush(child->services, parent->services[i]);
    }
    apush(parent->children, child);
    parent->active_child = child;
    parent->call_node = ctx->node;
    return flow_run_advance_child(parent);
}

int flow_context_entry_output(const flow_context *ctx)
{
    return ctx->run->entry_output;
}

int flow_context_return(struct flow_context *ctx)
{
    ctx->run->returned = ctx->node;
    ctx->run->return_input = ctx->input_pin;
    frame_arrays_free(ctx->run);
    aclear(ctx->run->frames);
    return FLOW_OK;
}

arena *flow_context_scratch(struct flow_context *ctx)
{
    return &ctx->run->scratch;
}

void *flow_context_scratch_alloc(struct flow_context *ctx, size_t size)
{
    if (!ctx || !ctx->run || !size) {
        return NULL;
    }
    return arena_alloc(&ctx->run->scratch, size);
}
