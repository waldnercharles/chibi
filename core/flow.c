#include "flow.h"

#include "arena.h"
#include "eval.h"
#include "instance.h"
#include "pin_conv.h"
#include "registry.h"
#include "runtime.h"
#include "service.h"
#include "value_array.h"

#include <stdlib.h>
#include <string.h>

#define FLOW_TRAVERSAL_MAX 256

typedef enum
{
    FLOW_FRAME_NODE,
    FLOW_FRAME_LOOP,
    FLOW_FRAME_FOREACH,
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

struct flow_run
{
    graph *graph;
    blueprint_instance *instance; /* owning instance, or NULL standalone */
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
    int graph_paused;
    int pause_requested;
    int step_mode;
    unsigned long activation_budget;
    unsigned call_depth;
    node *breakpoint_node;
    int breakpoint_skipped;
    service_binding *services;
    flow_error error;
    eval_context eval;
    runtime_store *runtime;
    flow_run **children;
    flow_frame *frames;
    flow_call_frame *stack_cache;
    pin_value *inputs;
    int input_count;
    const void *input_user_data;
    flow_sink_fn sink;
    void *sink_user;
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

static runtime_node *flow_runtime_node(flow_run *run, const node *n)
{
    return runtime_store_node(run->runtime, n);
}

static int flow_copy_nested_structs(arena *a, pin_value *value)
{
    if (value->container >= PIN_CONTAINER_ARRAY) {
        if (!value->ptr) {
            return 0;
        }
        pin_array *array = (pin_array *)value->ptr;
        for (int i = 0; i < array->count; i++) {
            if (flow_copy_nested_structs(a, &array->items[i])) {
                return -1;
            }
        }
        return 0;
    }
    if (value->type != PIN_STRUCT || !value->ptr) {
        return 0;
    }
    const type_desc *type = value->type_name ? registry_type_by_name(value->type_name)
                                             : NULL;
    if (!type || !type->size) {
        return -1;
    }
    void *copy = arena_alloc(a, type->size);
    if (!copy) {
        return -1;
    }
    memcpy(copy, value->ptr, type->size);
    value->ptr = copy;
    return 0;
}

static int flow_copy_input(arena *a, pin_value *dst, const pin_value *src)
{
    if (pin_value_copy_to_arena(a, dst, src)) {
        return -1;
    }
    return flow_copy_nested_structs(a, dst);
}

static void set_error(flow_error *error, int code, const node *n, const char *message)
{
    if (error) {
        *error = (flow_error){ code, n, -1, message, 0 };
    }
}

static int graph_has_node(const graph *g, const node *n)
{
    if (!g || !n) {
        return 0;
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i] == n) {
            return 1;
        }
    }
    return 0;
}

static void flow_fail(flow_run *run, int code, node *n, const char *message)
{
    run->state = FLOW_FAILED;
    run->error = (flow_error){ code, n, -1, message, run->steps };
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
    if (acount(run->traversals) >= FLOW_TRAVERSAL_MAX) {
        memmove(
            run->traversals,
            run->traversals + 1,
            (size_t)(FLOW_TRAVERSAL_MAX - 1) * sizeof *run->traversals
        );
        asetlen(run->traversals, FLOW_TRAVERSAL_MAX - 1);
    }
    apush(
        run->traversals,
        (flow_traversal){
            from_id,
            from_pin_id,
            to_id,
            to_pin_id,
            kind,
            run->current_time,
            run->steps,
        }
    );
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
    uint64_t to_id = 0;
    uint64_t to_pin_id = 0;
    int found = 0;
    for (int i = 0; i < acount(run->graph->edges); i++) {
        const edge *e = &run->graph->edges[i];
        if (e->kind != GRAPH_EDGE_EXEC || e->from_id != ctx->node->id ||
            e->from_pin_id != ctx->node->output_pins[output_pin].id) {
            continue;
        }
        if (found) {
            return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "EXEC output has ambiguous fan-out");
        }
        found = 1;
        to_id = e->to_id;
        to_pin_id = e->to_pin_id;
        target = graph_node_by_id(run->graph, e->to_id);
        int is_out = 0;
        if (!target ||
            !graph_pin_by_id(target, e->to_pin_id, &is_out, &target_pin) || is_out) {
            target = NULL;
            target_pin = -1;
        }
    }
    if (!found) {
        return FLOW_OK;
    }
    if (!target || target_pin < 0 || target->input_pins[target_pin].kind != PIN_EXEC) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "invalid EXEC target");
    }
    record_traversal(run, ctx->node->id, ctx->node->output_pins[output_pin].id, to_id, to_pin_id, GRAPH_EDGE_EXEC);
    apush(
        run->frames,
        (flow_frame){
            .node = target,
            .kind = FLOW_FRAME_NODE,
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

static int flow_run_wait(flow_run *run)
{
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
    if (schedule_output_for_node(run, node, output_pin)) {
        return run->error.code;
    }
    return FLOW_OK;
}

static const type_desc *watch_type(const flow_run *run, const node *n, int output, int pin)
{
    if (!n || !n->desc) {
        return NULL;
    }
    const type_desc *type = n->desc->type;
    if (!output) {
        node *source = NULL;
        int source_pin = -1;
        if (graph_input_source(run->graph, n, pin, &source, &source_pin) &&
            source && source->desc) {
            type = source->desc->type;
        }
    }
    if (!type) {
        const pin_spec *spec = output ? n->output_pins + pin : n->input_pins + pin;
        if (spec && spec->type_name) {
            return registry_type_by_name(spec->type_name);
        }
    }
    return type;
}

static const flow_watch *watch_find(const flow_run *run, const node *n, int output, int pin)
{
    if (!n || pin < 0) {
        return NULL;
    }
    int count = output ? n->output_count : n->input_count;
    if (pin >= count) {
        return NULL;
    }
    uint64_t pin_id = output ? n->output_pins[pin].id : n->input_pins[pin].id;
    for (int i = 0; i < acount(run->watches); i++) {
        const flow_watch *watch = &run->watches[i];
        if (watch->node_id == n->id && watch->output == output &&
            watch->pin_id == pin_id) {
            return watch;
        }
    }
    return NULL;
}

static void watch_release(flow_watch *watch)
{
    if (!watch || !watch->owned) {
        return;
    }
    if (watch->value.type == PIN_STRUCT &&
        watch->value.container == PIN_CONTAINER_SINGLE) {
        free(watch->owned);
        watch->value = (pin_value){ 0 };
    } else {
        pin_value_destroy(&watch->value);
    }
    watch->owned = NULL;
    watch->owned_size = 0;
}

static int watch_store(flow_run *run, const node *n, int output, int pin, const pin_value *value)
{
    if (!run || !n || !value) {
        return -1;
    }
    flow_watch *watch = (flow_watch *)watch_find(run, n, output, pin);
    if (!watch) {
        uint64_t pin_id = output ? n->output_pins[pin].id : n->input_pins[pin].id;
        apush(
            run->watches,
            (flow_watch){
                .node_id = n->id,
                .pin_id = pin_id,
                .output = output,
                .value = { .type = PIN_FLOW },
                .owned = NULL,
                .owned_size = 0,
            }
        );
        watch = &run->watches[acount(run->watches) - 1];
    }
    watch_release(watch);
    watch->value = *value;

    if (value->container >= PIN_CONTAINER_ARRAY && value->ptr) {
        if (pin_value_copy(&watch->value, value)) {
            watch->value = (pin_value){ .type = PIN_FLOW };
            return -1;
        }
        watch->owned = watch->value.ptr;
    } else if ((value->type == PIN_STR || value->type == PIN_NAME) && value->str) {
        if (pin_value_copy(&watch->value, value)) {
            watch->value = (pin_value){ .type = PIN_FLOW };
            return -1;
        }
        watch->owned = (void *)watch->value.str;
    } else if (value->type == PIN_STRUCT && value->ptr) {
        const type_desc *type = watch_type(run, n, output, pin);
        if (!type || !type->size) {
            watch->value = (pin_value){ .type = PIN_FLOW };
            return -1;
        }
        watch->owned = malloc(type->size);
        if (!watch->owned) {
            watch->value = (pin_value){ .type = PIN_FLOW };
            return -1;
        }
        memcpy(watch->owned, value->ptr, type->size);
        watch->owned_size = type->size;
        watch->value.ptr = watch->owned;
    }
    return 0;
}

static void watch_capture_node(flow_run *run, node *n)
{
    if (!run || !n || !n->desc) {
        return;
    }
    const runtime_node *state = flow_runtime_node(run, n);
    const pin_value *inputs = runtime_node_inputs_const(state);
    const pin_value *outputs = runtime_node_outputs_const(state);
    for (int i = 0; i < n->input_count; i++) {
        if (n->input_pins[i].kind == PIN_DATA) {
            (void)watch_store(run, n, 0, i, &inputs[i]);
        }
    }
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].kind == PIN_DATA) {
            (void)watch_store(run, n, 1, i, &outputs[i]);
        }
    }
}

static void watch_clear(flow_run *run)
{
    for (int i = 0; i < acount(run->watches); i++) {
        watch_release(&run->watches[i]);
    }
    afree(run->watches);
}

static void frame_arrays_free(flow_run *run)
{
    for (int i = 0; i < acount(run->frames); i++) {
        if (run->frames[i].kind == FLOW_FRAME_FOREACH) {
            pin_array_free(run->frames[i].array);
        }
    }
}

static void flow_run_free_children(flow_run *run)
{
    for (int i = 0; i < acount(run->children); i++) {
        flow_run_free(run->children[i]);
    }
    afree(run->children);
}

static int flow_run_activate(flow_run *run)
{
    if (run->steps >= run->step_budget) {
        node *n = acount(run->frames) ? run->frames[acount(run->frames) - 1].node
                                      : NULL;
        flow_fail(run, FLOW_ERR_STEP_BUDGET, n, "flow step budget exceeded");
        return FLOW_ERR_STEP_BUDGET;
    }

    /* Each synchronous node activation gets a fresh pull-eval memo. Runtime
     * values and the arena remain live for the run so flow data outputs and
     * pointer-bearing values can cross activation boundaries safely. */
    runtime_store_reset_memo(run->runtime);
    eval_context_reset(&run->eval);

    flow_frame frame = apop(run->frames);
    run->steps++;
    if (frame.kind == FLOW_FRAME_LOOP || frame.kind == FLOW_FRAME_FOREACH) {
        if (frame.kind == FLOW_FRAME_FOREACH) {
            if (frame.break_requested || !frame.array || frame.current > frame.last) {
                watch_capture_node(run, frame.node);
                pin_array_free(frame.array);
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
            outputs[frame.value_output] = frame.array->items[index];
            if (frame.current < frame.last) {
                frame.current++;
            } else {
                frame.current = frame.last + 1;
            }
            apush(run->frames, frame);
            watch_capture_node(run, frame.node);
            return schedule_output_for_node(run, frame.node, frame.body_output);
        }
        if (frame.current != frame.last) {
            frame.current += frame.current < frame.last ? 1 : -1;
            runtime_node *state = flow_runtime_node(run, frame.node);
            pin_value *outputs = runtime_node_outputs(state);
            runtime_node_set_computed(state, 0);
            outputs[frame.index_output].type = PIN_I32;
            outputs[frame.index_output].type_name = "int";
            outputs[frame.index_output].i = frame.current;
            apush(run->frames, frame);
            watch_capture_node(run, frame.node);
            return schedule_output_for_node(run, frame.node, frame.body_output);
        }
        watch_capture_node(run, frame.node);
        return schedule_output_for_node(run, frame.node, frame.completed_output);
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
        watch_capture_node(run, n);
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
    int result = n->desc->flow(n, &ctx);
    if (ctx.failed || result < 0) {
        if (run->state != FLOW_FAILED) {
            flow_fail(run, result < 0 ? result : FLOW_ERR_NODE, n, "flow node failed");
        }
        return run->error.code;
    }
    watch_capture_node(run, n);
    if (ctx.pause_requested) {
        run->waiting_node = n;
        run->waiting_output = ctx.pause_output;
        run->wake_time = ctx.pause_time;
        run->state = FLOW_SUSPENDED;
        return FLOW_SUSPENDED_RESULT;
    }
    return FLOW_OK;
}

static int flow_run_execute(flow_run *run)
{
    if (flow_run_check_topology(run)) {
        return FLOW_ERR_TOPOLOGY;
    }
    int waiting = flow_run_wait(run);
    if (waiting) {
        return waiting;
    }

    while (run->state == FLOW_RUNNING && acount(run->frames) > 0) {
        node *next = run->frames[acount(run->frames) - 1].node;
        if (next && next->breakpoint && run->graph->breakpoints_enabled &&
            !run->breakpoint_skipped) {
            run->breakpoint_node = next;
            run->state = FLOW_PAUSED;
            return FLOW_PAUSED_RESULT;
        }
        run->breakpoint_skipped = 0;
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

    flow_run *run = (flow_run *)calloc(1, sizeof *run);
    if (!run) {
        set_error(error, FLOW_ERR_NODE, entry, "flow allocation failed");
        return NULL;
    }
    run->graph = g;
    run->runtime = runtime_store_create(g);
    run->instance = options ? options->instance : NULL;
    /* Instance-owned activations share the instance's mutation guard; only a
     * standalone run acquires and releases the graph guard itself. */
    run->graph_guard_owned = run->instance == NULL;
    run->state = FLOW_RUNNING;
    run->step_budget = options && options->step_budget ? options->step_budget : 10000;
    run->now = options ? options->now : NULL;
    run->now_user = options ? options->now_user : NULL;
    run->current_time = options ? options->initial_time : 0.0;
    run->sink = options ? options->sink : NULL;
    run->sink_user = options ? options->sink_user : NULL;
    run->call_depth = options ? options->call_depth : 0;
    if (run->instance) {
        for (int i = 0; i < acount(run->instance->services); i++) {
            apush(run->services, run->instance->services[i]);
        }
    }
    run->topology_revision = graph_topology_revision(g);
    run->input_count = inputs.count;
    run->input_user_data = inputs.user_data;
    run->waiting_output = -1;
    eval_context_init(&run->eval);
    eval_context_attach_runtime(&run->eval, run->runtime);
    run->eval.instance = run->instance;
    run->eval.on_data_edge = record_evaluated_data_edge;
    run->eval.on_data_edge_user = run;
    arena_init(&run->input_arena, 65536);
    arena_init(&run->scratch, 65536);
    if (inputs.count) {
        run->inputs = (pin_value *)calloc((size_t)inputs.count, sizeof *run->inputs);
        if (!run->inputs) {
            arena_free(&run->input_arena);
            arena_free(&run->scratch);
            eval_context_detach_runtime(&run->eval);
            runtime_store_free(run->runtime);
            free(run);
            set_error(error, FLOW_ERR_NODE, entry, "flow input allocation failed");
            return NULL;
        }
        for (int i = 0; i < inputs.count; i++) {
            if (flow_copy_input(&run->input_arena, &run->inputs[i], &inputs.values[i])) {
                free(run->inputs);
                arena_free(&run->input_arena);
                arena_free(&run->scratch);
                eval_context_detach_runtime(&run->eval);
                runtime_store_free(run->runtime);
                free(run);
                set_error(error, FLOW_ERR_NODE, entry, "flow input copy failed");
                return NULL;
            }
        }
    }
    runtime_store_reset(run->runtime);
    apush(run->frames, (flow_frame){ .node = entry, .kind = FLOW_FRAME_NODE, .input_pin = -1 });
    /* EXEC-8/EXEC-19: a standalone run keeps the graph guarded for its whole
     * non-idle lifetime; an instance-owned run does not (the instance does). */
    if (run->graph_guard_owned) {
        graph_pause(run);
    }
    return run;
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

static int flow_run_continue(flow_run *run, int clear_step_mode)
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
        run->frames[acount(run->frames) - 1].node == run->breakpoint_node) {
        run->breakpoint_skipped = 1;
    }
    run->state = FLOW_RUNNING;
    if (clear_step_mode) {
        run->step_mode = 0;
        run->activation_budget = 0;
    } else if (run->step_mode) {
        if (run->activation_budget == 0) {
            run->activation_budget = 1;
        }
    } else {
        run->activation_budget = 0;
    }
    return flow_run_execute(run);
}

int flow_run_resume(flow_run *run)
{
    return flow_run_continue(run, 1);
}

int flow_run_tick(flow_run *run)
{
    if (!run) {
        return FLOW_ERR_ARGUMENT;
    }
    if (run->now) {
        run->current_time = run->now(run->now_user);
    }
    return flow_run_continue(run, 0);
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
    if (run->state == FLOW_RUNNING) {
        run->pause_requested = 1;
        return FLOW_OK;
    }
    if (run->state == FLOW_SUSPENDED) {
        run->step_mode = 1;
        run->activation_budget = 1;
        return FLOW_OK;
    }
    if (run->state == FLOW_PAUSED) {
        return FLOW_OK;
    }
    return FLOW_ERR_STATE;
}

int flow_run_step(flow_run *run)
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
        run->frames[acount(run->frames) - 1].node == run->breakpoint_node) {
        run->breakpoint_skipped = 1;
    }
    run->state = FLOW_RUNNING;
    run->pause_requested = 0;
    run->step_mode = 1;
    run->activation_budget = 1;
    return flow_run_execute(run);
}

int flow_run_step_over(flow_run *run)
{
    /* Function calls are one interpreter activation, so stepping one
     * activation already returns at the caller's next node. */
    return flow_run_step(run);
}

int flow_run_stop(flow_run *run)
{
    if (!run) {
        return FLOW_ERR_ARGUMENT;
    }
    if (run->state == FLOW_IDLE) {
        return FLOW_OK;
    }
    run->state = FLOW_IDLE;
    frame_arrays_free(run);
    aclear(run->frames);
    run->waiting_node = NULL;
    run->waiting_output = -1;
    run->pause_requested = 0;
    run->step_mode = 0;
    run->activation_budget = 0;
    run->breakpoint_node = NULL;
    run->breakpoint_skipped = 0;
    flow_run_free_children(run);
    runtime_store_reset(run->runtime);
    eval_context_reset(&run->eval);
    watch_clear(run);
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
    runtime_store_reset(run->runtime);
    eval_context_reset(&run->eval);
    frame_arrays_free(run);
    afree(run->frames);
    afree(run->stack_cache);
    afree(run->services);
    free(run->inputs);
    run->inputs = NULL;
    run->input_count = 0;
    run->input_user_data = NULL;
    arena_free(&run->input_arena);
    arena_free(&run->scratch);
    eval_context_detach_runtime(&run->eval);
    runtime_store_free(run->runtime);
    run->runtime = NULL;
    watch_clear(run);
    afree(run->traversals);
    run->steps = 0;
    run->topology_revision = graph_topology_revision(run->graph);
    run->error = (flow_error){ 0, NULL, -1, NULL, 0 };
    return FLOW_OK;
}

int flow_run_ready(const flow_run *run, double now)
{
    if (!run) {
        return 0;
    }
    if (run->state == FLOW_RUNNING) {
        return 1;
    }
    if (run->state == FLOW_SUSPENDED) {
        return now >= run->wake_time;
    }
    return 0;
}

flow_state flow_run_status(const flow_run *run)
{
    return run ? run->state : FLOW_IDLE;
}

const flow_error *flow_run_error(const flow_run *run)
{
    return run ? &run->error : NULL;
}

unsigned long flow_run_steps(const flow_run *run)
{
    return run ? run->steps : 0;
}

flow_inputs flow_run_inputs(const flow_run *run)
{
    return run ? (flow_inputs){ run->inputs, run->input_count, run->input_user_data }
               : (flow_inputs){ NULL, 0, NULL };
}

const pin_value *flow_run_input_value(const flow_run *run, const node *n, int pin)
{
    if (!run || !n || pin < 0 || pin >= n->input_count) {
        return NULL;
    }
    const runtime_node *state = flow_runtime_node((flow_run *)run, n);
    return &runtime_node_inputs_const(state)[pin];
}

const flow_traversal *flow_run_traversals(const flow_run *run, int *count)
{
    if (count) {
        *count = run ? acount(run->traversals) : 0;
    }
    return run ? run->traversals : NULL;
}

const node *flow_run_current_node(const flow_run *run)
{
    if (!run || !acount(run->frames)) {
        return NULL;
    }
    return run->frames[acount(run->frames) - 1].node;
}

const flow_call_frame *flow_run_call_stack(const flow_run *run, int *count)
{
    if (count) {
        *count = 0;
    }
    if (!run) {
        return NULL;
    }
    flow_run *mutable_run = (flow_run *)run;
    aclear(mutable_run->stack_cache);
    for (int i = 0; i < acount(run->frames); i++) {
        node *n = run->frames[i].node;
        apush(
            mutable_run->stack_cache,
            (flow_call_frame){ run->graph, run->graph->graph_id, n ? n->id : 0 }
        );
    }
    if (count) {
        *count = acount(mutable_run->stack_cache);
    }
    return mutable_run->stack_cache;
}

unsigned flow_run_call_depth(const flow_run *run)
{
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
    if (run->instance) {
        blueprint_instance_remove_activation(run->instance, run);
    }
    flow_run_reset(run);
    free(run);
}

static int flow_run_watch_get(const flow_run *run, const node *n, int output, int pin, pin_value *out)
{
    if (!run || !n || !out || pin < 0) {
        return -1;
    }
    const flow_watch *w = watch_find(run, n, output, pin);
    if (!w) {
        return -1;
    }
    *out = w->value;
    return 0;
}

int flow_run_watch_input(const flow_run *run, const node *n, int pin, pin_value *out)
{
    return flow_run_watch_get(run, n, 0, pin, out);
}

int flow_run_watch_output(const flow_run *run, const node *n, int pin, pin_value *out)
{
    return flow_run_watch_get(run, n, 1, pin, out);
}

int flow_run_watch_copy_output(
    const flow_run *run,
    const node *n,
    int pin,
    pin_value *out,
    void *struct_storage,
    size_t struct_storage_size
)
{
    if (!run || !n || !out || pin < 0) {
        return -1;
    }
    const flow_watch *w = watch_find(run, n, 1, pin);
    if (!w) {
        return -1;
    }
    *out = w->value;
    if (out->type == PIN_STRUCT) {
        if (!w->owned || !struct_storage || struct_storage_size < w->owned_size) {
            return -1;
        }
        memcpy(struct_storage, w->owned, w->owned_size);
        out->ptr = struct_storage;
    }
    return 0;
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
            return flow_context_fail(
                ctx,
                FLOW_ERR_INVALID_GRAPH,
                status && status->message ? status->message : "data evaluation failed"
            );
        }
        pin_coerce(out, spec);
        *flow_context_input_value(ctx, input_pin) = *out;
        if (src->output_pins[src_pin].flags & PIN_FLAG_WATCH) {
            (void)watch_store(ctx->run, src, 1, src_pin, out);
        }
        return FLOW_OK;
    }
    *flow_context_input_value(ctx, input_pin) = spec->default_value;
    *out = *flow_context_input_value(ctx, input_pin);
    if (out->type != spec->type && !pin_coerce(out, spec)) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "data input has wrong type");
    }
    out->type_name = spec->type_name;
    return FLOW_OK;
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
    apush(
        ctx->run->frames,
        (flow_frame){
            .node = ctx->node,
            .kind = FLOW_FRAME_LOOP,
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
        flow_frame *frame = &ctx->run->frames[i];
        if (frame->kind != FLOW_FRAME_LOOP && frame->kind != FLOW_FRAME_FOREACH) {
            continue;
        }
        frame->break_requested = 1;
        return FLOW_OK;
    }
    return flow_context_fail(ctx, FLOW_ERR_STATE, "Break is outside a loop");
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
    pin_array *array = pin_array_clone(source);
    if (!array) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "ForEach array copy failed");
    }
    if (body_output < 0 || value_output < 0 || index_output < 0 ||
        completed_output < 0 || body_output >= ctx->node->output_count ||
        value_output >= ctx->node->output_count ||
        index_output >= ctx->node->output_count ||
        completed_output >= ctx->node->output_count) {
        pin_array_free(array);
        return flow_context_fail(ctx, FLOW_ERR_INVALID_NODE, "invalid ForEach descriptor");
    }
    runtime_node *state = flow_runtime_node(ctx->run, ctx->node);
    runtime_node_set_computed(state, 0);
    if (!array->count) {
        pin_array_free(array);
        return flow_context_continue(ctx, completed_output);
    }
    runtime_node_outputs(state)[value_output] = array->items[0];
    runtime_node_outputs(state)[index_output] = (pin_value){ .type = PIN_I32,
                                                             .type_name = "int",
                                                             .i = 0 };
    apush(
        ctx->run->frames,
        (flow_frame){
            .node = ctx->node,
            .kind = FLOW_FRAME_FOREACH,
            .input_pin = -1,
            .current = 1,
            .last = array->count - 1,
            .body_output = body_output,
            .value_output = value_output,
            .completed_output = completed_output,
            .index_output = index_output,
            .array = array,
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

blueprint_instance *flow_context_instance(const struct flow_context *ctx)
{
    return ctx && ctx->run ? ctx->run->instance : NULL;
}

int flow_context_retain_run(struct flow_context *ctx, flow_run *child)
{
    if (!ctx || !ctx->run || !child || child == ctx->run) {
        return -1;
    }
    apush(ctx->run->children, child);
    return 0;
}

void *flow_context_scratch_alloc(struct flow_context *ctx, size_t size)
{
    if (!ctx || !ctx->run || !size) {
        return NULL;
    }
    return arena_alloc(&ctx->run->scratch, size);
}
