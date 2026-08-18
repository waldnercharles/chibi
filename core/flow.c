#include "flow.h"

#include "arena.h"
#include "eval.h"
#include "registry.h"

#include <stdlib.h>
#include <string.h>

typedef enum
{
    FLOW_FRAME_NODE,
    FLOW_FRAME_LOOP,
} flow_frame_kind;

typedef struct
{
    node *node;
    flow_frame_kind kind;
    int current;
    int last;
} flow_frame;

typedef struct
{
    const node *node;
    int pin;
    int output;
    pin_value value;
    void *owned;
    size_t owned_size;
} flow_watch;

struct flow_run
{
    graph *graph;
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
    flow_error error;
    eval_context eval;
    flow_frame *frames;
    int frame_count;
    int frame_cap;
    pin_value *inputs;
    int input_count;
    const void *input_user_data;
    flow_sink_fn sink;
    void *sink_user;
    arena scratch;
    flow_watch *watches;
    int watch_count;
    int watch_cap;
};

struct flow_context
{
    flow_run *run;
    node *node;
    int pause_requested;
    double pause_time;
    int pause_output;
    int failed;
};

static void set_error(flow_error *error, int code, const node *n, const char *message)
{
    if (error) *error = (flow_error){ code, n, -1, message, 0 };
}

static int graph_has_node(const graph *g, const node *n)
{
    if (!g || !n) return 0;
    for (int i = 0; i < g->node_count; i++)
        if (g->nodes[i] == n) return 1;
    return 0;
}

static int frame_push(flow_run *run, flow_frame frame)
{
    if (run->frame_count == run->frame_cap) {
        int next = run->frame_cap ? run->frame_cap * 2 : 16;
        flow_frame *grown = (flow_frame *)realloc(run->frames, (size_t)next * sizeof *grown);
        if (!grown) return -1;
        run->frames = grown;
        run->frame_cap = next;
    }
    run->frames[run->frame_count++] = frame;
    return 0;
}

static void flow_fail(flow_run *run, int code, node *n, const char *message)
{
    run->state = FLOW_FAILED;
    run->error = (flow_error){ code, n, -1, message, run->steps };
}

static void graph_pause(flow_run *run)
{
    if (run->graph_paused) return;
    graph_flow_pause(run->graph);
    run->graph_paused = 1;
}

static void graph_resume(flow_run *run)
{
    if (!run->graph_paused) return;
    graph_flow_resume(run->graph);
    run->graph_paused = 0;
}

/* One output pin has at most one EXEC target. The edge's explicit order is
 * retained by the graph and JSON; node output pins provide Sequence order. */
static int schedule_output(flow_context *ctx, int output_pin)
{
    flow_run *run = ctx->run;
    if (output_pin < 0 || output_pin >= ctx->node->output_count)
        return flow_context_fail(ctx, FLOW_ERR_INVALID_NODE, "flow output pin out of range");
    if (ctx->node->output_pins[output_pin].kind != PIN_EXEC)
        return flow_context_fail(ctx, FLOW_ERR_INVALID_NODE, "continuation is not an EXEC output");

    node *target = NULL;
    int target_pin = 0;
    int found = 0;
    for (int i = 0; i < run->graph->edge_count; i++) {
        const edge *e = &run->graph->edges[i];
        if (e->kind != GRAPH_EDGE_EXEC || e->from != ctx->node || e->from_pin != output_pin)
            continue;
        if (found) {
            return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "EXEC output has ambiguous fan-out");
        }
        found = 1;
        target = e->to;
        target_pin = e->to_pin;
    }
    if (!found) return FLOW_OK;
    if (target_pin < 0 || target_pin >= target->desc->input_count ||
        target->input_pins[target_pin].kind != PIN_EXEC)
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "invalid EXEC target");
    return frame_push(run, (flow_frame){ target, FLOW_FRAME_NODE, 0, 0 })
             ? flow_context_fail(ctx, FLOW_ERR_NODE, "flow continuation allocation failed")
             : FLOW_OK;
}

static int schedule_output_for_node(flow_run *run, node *n, int output_pin)
{
    flow_context ctx = { run, n, 0, 0.0, -1, 0 };
    return schedule_output(&ctx, output_pin);
}

static int flow_run_check_topology(flow_run *run)
{
    if (run->topology_revision == graph_topology_revision(run->graph)) return 0;
    flow_fail(run, FLOW_ERR_TOPOLOGY, NULL, "graph topology changed during flow run");
    return FLOW_ERR_TOPOLOGY;
}

static int flow_run_wait(flow_run *run)
{
    if (!run->waiting_node) return FLOW_OK;
    if (run->current_time < run->wake_time) {
        run->state = FLOW_SUSPENDED;
        return FLOW_SUSPENDED_RESULT;
    }

    node *node = run->waiting_node;
    int output_pin = run->waiting_output;
    run->waiting_node = NULL;
    run->waiting_output = -1;
    if (schedule_output_for_node(run, node, output_pin)) return run->error.code;
    return FLOW_OK;
}

static const type_desc *watch_type(const flow_run *run, const node *n, int output, int pin)
{
    if (!n || !n->desc) return NULL;
    const type_desc *type = n->desc->type;
    if (!output) {
        node *source = NULL;
        int source_pin = -1;
        if (graph_input_source(run->graph, n, pin, &source, &source_pin) &&
            source && source->desc)
            type = source->desc->type;
    }
    if (!type) {
        const pin_spec *spec = output ? n->output_pins + pin : n->input_pins + pin;
        if (spec && spec->type_name)
            for (int i = 0; i < REGISTRY_TYPE_COUNT; i++)
                if (strcmp(REGISTRY_TYPES[i]->name, spec->type_name) == 0)
                    return REGISTRY_TYPES[i];
    }
    return type;
}

static int watch_reserve(flow_run *run)
{
    if (run->watch_count < run->watch_cap) return 0;
    int next = run->watch_cap ? run->watch_cap * 2 : 16;
    flow_watch *grown = (flow_watch *)realloc(run->watches, (size_t)next * sizeof *grown);
    if (!grown) return -1;
    run->watches = grown;
    run->watch_cap = next;
    return 0;
}

static flow_watch *watch_find(flow_run *run, const node *n, int output, int pin)
{
    for (int i = 0; i < run->watch_count; i++) {
        flow_watch *watch = &run->watches[i];
        if (watch->node == n && watch->output == output && watch->pin == pin)
            return watch;
    }
    return NULL;
}

static int watch_store(flow_run *run, const node *n, int output, int pin, const pin_value *value)
{
    if (!run || !n || !value) return -1;
    flow_watch *watch = watch_find(run, n, output, pin);
    if (!watch) {
        if (watch_reserve(run)) return -1;
        watch = &run->watches[run->watch_count++];
        *watch = (flow_watch){
            .node = n,
            .pin = pin,
            .output = output,
            .value = { .type = PIN_FLOW },
            .owned = NULL,
            .owned_size = 0,
        };
    }
    free(watch->owned);
    watch->owned = NULL;
    watch->owned_size = 0;
    watch->value = *value;

    if (value->type == PIN_STR && value->str) {
        size_t size = strlen(value->str) + 1;
        watch->owned = malloc(size);
        if (!watch->owned) return -1;
        memcpy(watch->owned, value->str, size);
        watch->owned_size = size;
        watch->value.str = (const char *)watch->owned;
    } else if (value->type == PIN_STRUCT && value->ptr) {
        const type_desc *type = watch_type(run, n, output, pin);
        if (!type || !type->size) return -1;
        watch->owned = malloc(type->size);
        if (!watch->owned) return -1;
        memcpy(watch->owned, value->ptr, type->size);
        watch->owned_size = type->size;
        watch->value.ptr = watch->owned;
    }
    return 0;
}

static void watch_capture_node(flow_run *run, node *n)
{
    if (!run || !n || !n->desc) return;
    for (int i = 0; i < n->input_count; i++)
        if (n->input_pins[i].kind == PIN_DATA)
            (void)watch_store(run, n, 0, i, &n->inputs[i]);
    for (int i = 0; i < n->output_count; i++)
        if (n->output_pins[i].kind == PIN_DATA)
            (void)watch_store(run, n, 1, i, &n->outputs[i]);
}

static void watch_clear(flow_run *run)
{
    for (int i = 0; i < run->watch_count; i++) free(run->watches[i].owned);
    free(run->watches);
    run->watches = NULL;
    run->watch_count = 0;
    run->watch_cap = 0;
}

static int flow_run_activate(flow_run *run)
{
    if (run->steps >= run->step_budget) {
        node *n = run->frame_count ? run->frames[run->frame_count - 1].node : NULL;
        flow_fail(run, FLOW_ERR_STEP_BUDGET, n, "flow step budget exceeded");
        return FLOW_ERR_STEP_BUDGET;
    }

    /* Each synchronous node activation gets a fresh pull-eval memo and
     * scratch lifetime. This keeps loop Index-dependent data current. */
    graph_eval_reset(run->graph);
    eval_context_reset(&run->eval);
    arena_reset(&run->scratch);

    flow_frame frame = run->frames[--run->frame_count];
    run->steps++;
    if (frame.kind == FLOW_FRAME_LOOP) {
        if (frame.current < frame.last) {
            frame.current++;
            frame.node->computed = 0;
            frame.node->outputs[1].type = PIN_I32;
            frame.node->outputs[1].type_name = "int";
            frame.node->outputs[1].i = frame.current;
            if (frame_push(run, frame)) {
                flow_fail(run, FLOW_ERR_NODE, frame.node, "loop continuation allocation failed");
                return FLOW_ERR_NODE;
            }
            watch_capture_node(run, frame.node);
            return schedule_output_for_node(run, frame.node, 0);
        }
        watch_capture_node(run, frame.node);
        return schedule_output_for_node(run, frame.node, 2);
    }

    node *n = frame.node;
    if (!n || !n->desc || !n->desc->flow) {
        flow_fail(run, FLOW_ERR_INVALID_NODE, n, "node has no flow evaluator");
        return FLOW_ERR_INVALID_NODE;
    }

    if (n->disabled) {
        for (int i = 0; i < n->output_count; i++) {
            if (n->output_pins[i].kind != PIN_DATA) continue;
            n->outputs[i] = (pin_value){ .type = n->output_pins[i].type,
                                         .type_name = n->output_pins[i].type_name };
        }
        watch_capture_node(run, n);
        for (int i = 0; i < n->output_count; i++)
            if (n->output_pins[i].kind == PIN_EXEC)
                return schedule_output_for_node(run, n, i);
        return FLOW_OK;
    }

    flow_context ctx = { run, n, 0, 0.0, -1, 0 };
    int result = n->desc->flow(n, &ctx);
    if (ctx.failed || result < 0) {
        if (run->state != FLOW_FAILED)
            flow_fail(run, result < 0 ? result : FLOW_ERR_NODE, n, "flow node failed");
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
    if (flow_run_check_topology(run)) return FLOW_ERR_TOPOLOGY;
    int waiting = flow_run_wait(run);
    if (waiting) return waiting;

    while (run->state == FLOW_RUNNING && run->frame_count > 0) {
        if (run->pause_requested) {
            run->pause_requested = 0;
            run->state = FLOW_PAUSED;
            return FLOW_PAUSED_RESULT;
        }
        int result = flow_run_activate(run);
        if (result) return result;
        if (run->step_mode && run->activation_budget && --run->activation_budget == 0) {
            run->state = FLOW_PAUSED;
            return FLOW_PAUSED_RESULT;
        }
    }
    if (run->state == FLOW_RUNNING && run->frame_count == 0)
        run->state = FLOW_COMPLETED;
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
    run->state = FLOW_RUNNING;
    run->step_budget = options && options->step_budget ? options->step_budget : 10000;
    run->now = options ? options->now : NULL;
    run->now_user = options ? options->now_user : NULL;
    run->current_time = options ? options->initial_time : 0.0;
    run->sink = options ? options->sink : NULL;
    run->sink_user = options ? options->sink_user : NULL;
    run->topology_revision = graph_topology_revision(g);
    run->input_count = inputs.count;
    run->input_user_data = inputs.user_data;
    run->waiting_output = -1;
    eval_context_init(&run->eval);
    if (inputs.count) {
        run->inputs = (pin_value *)malloc((size_t)inputs.count * sizeof *run->inputs);
        if (!run->inputs) {
            free(run);
            set_error(error, FLOW_ERR_NODE, entry, "flow input allocation failed");
            return NULL;
        }
        memcpy(run->inputs, inputs.values, (size_t)inputs.count * sizeof *run->inputs);
    }
    arena_init(&run->scratch, 65536);
    graph_eval_reset(g);
    if (frame_push(run, (flow_frame){ entry, FLOW_FRAME_NODE, 0, 0 })) {
        arena_free(&run->scratch);
        free(run->frames);
        free(run->inputs);
        free(run);
        set_error(error, FLOW_ERR_NODE, entry, "flow frame allocation failed");
        return NULL;
    }
    /* EXEC-8: the graph stays guarded for the entire non-idle run. */
    graph_pause(run);
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
    for (int i = 0; i < g->node_count; i++) {
        node *n = g->nodes[i];
        if (n->desc->flow_kind != NODE_FLOW_EVENT || !n->reference_name ||
            strcmp(n->reference_name, name) != 0)
            continue;
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
    if (!run) return NULL;
    flow_run_resume(run);
    return run;
}

static int flow_run_continue(flow_run *run, int clear_step_mode)
{
    if (!run) return FLOW_ERR_ARGUMENT;
    if (run->state == FLOW_IDLE) return FLOW_ERR_STATE;
    if (run->state == FLOW_COMPLETED || run->state == FLOW_FAILED)
        return run->error.code;
    run->state = FLOW_RUNNING;
    if (clear_step_mode) {
        run->step_mode = 0;
        run->activation_budget = 0;
    } else if (run->step_mode) {
        if (run->activation_budget == 0) run->activation_budget = 1;
    } else {
        run->activation_budget = 0;
    }
    return flow_run_execute(run);
}

int flow_run_resume(flow_run *run) { return flow_run_continue(run, 1); }

int flow_run_tick(flow_run *run)
{
    if (!run) return FLOW_ERR_ARGUMENT;
    if (run->now) run->current_time = run->now(run->now_user);
    return flow_run_continue(run, 0);
}

int flow_run_tick_at(flow_run *run, double monotonic_time)
{
    if (!run) return FLOW_ERR_ARGUMENT;
    run->current_time = monotonic_time;
    return flow_run_continue(run, 0);
}

int flow_run_pause(flow_run *run)
{
    if (!run) return FLOW_ERR_ARGUMENT;
    if (run->state == FLOW_RUNNING) {
        run->pause_requested = 1;
        return FLOW_OK;
    }
    if (run->state == FLOW_SUSPENDED) {
        run->step_mode = 1;
        run->activation_budget = 1;
        return FLOW_OK;
    }
    if (run->state == FLOW_PAUSED) return FLOW_OK;
    return FLOW_ERR_STATE;
}

int flow_run_step(flow_run *run)
{
    if (!run) return FLOW_ERR_ARGUMENT;
    if (run->state == FLOW_IDLE) return FLOW_ERR_STATE;
    if (run->state == FLOW_COMPLETED || run->state == FLOW_FAILED)
        return run->error.code;
    run->state = FLOW_RUNNING;
    run->pause_requested = 0;
    run->step_mode = 1;
    run->activation_budget = 1;
    return flow_run_execute(run);
}

int flow_run_stop(flow_run *run)
{
    if (!run) return FLOW_ERR_ARGUMENT;
    if (run->state == FLOW_IDLE) return FLOW_OK;
    run->state = FLOW_IDLE;
    run->frame_count = 0;
    run->waiting_node = NULL;
    run->waiting_output = -1;
    run->pause_requested = 0;
    run->step_mode = 0;
    run->activation_budget = 0;
    graph_eval_reset(run->graph);
    eval_context_reset(&run->eval);
    watch_clear(run);
    run->error = (flow_error){ 0, NULL, -1, NULL, run->steps };
    graph_resume(run);
    return FLOW_OK;
}

int flow_run_reset(flow_run *run)
{
    if (!run) return FLOW_ERR_ARGUMENT;
    flow_run_stop(run);
    graph_eval_reset(run->graph);
    eval_context_reset(&run->eval);
    free(run->frames);
    run->frames = NULL;
    run->frame_count = 0;
    run->frame_cap = 0;
    free(run->inputs);
    run->inputs = NULL;
    run->input_count = 0;
    run->input_user_data = NULL;
    arena_free(&run->scratch);
    watch_clear(run);
    run->steps = 0;
    run->topology_revision = graph_topology_revision(run->graph);
    run->error = (flow_error){ 0, NULL, -1, NULL, 0 };
    return FLOW_OK;
}

flow_state flow_run_status(const flow_run *run)
{ return run ? run->state : FLOW_IDLE; }

const flow_error *flow_run_error(const flow_run *run)
{ return run ? &run->error : NULL; }

unsigned long flow_run_steps(const flow_run *run)
{ return run ? run->steps : 0; }

flow_inputs flow_run_inputs(const flow_run *run)
{
    return run ? (flow_inputs){ run->inputs, run->input_count, run->input_user_data }
               : (flow_inputs){ NULL, 0, NULL };
}

void flow_run_free(flow_run *run)
{
    if (!run) return;
    flow_run_reset(run);
    free(run);
}

static int flow_run_watch_get(const flow_run *run, const node *n, int output, int pin, pin_value *out)
{
    if (!run || !n || !out || pin < 0) return -1;
    for (int i = 0; i < run->watch_count; i++) {
        const flow_watch *watch = &run->watches[i];
        if (watch->node == n && watch->output == output && watch->pin == pin) {
            *out = watch->value;
            return 0;
        }
    }
    return -1;
}

int flow_run_watch_input(const flow_run *run, const node *n, int pin, pin_value *out)
{ return flow_run_watch_get(run, n, 0, pin, out); }

int flow_run_watch_output(const flow_run *run, const node *n, int pin, pin_value *out)
{ return flow_run_watch_get(run, n, 1, pin, out); }

int flow_run_watch_copy_output(
    const flow_run *run,
    const node *n,
    int pin,
    pin_value *out,
    void *struct_storage,
    size_t struct_storage_size
)
{
    if (!run || !n || !out || pin < 0) return -1;
    for (int i = 0; i < run->watch_count; i++) {
        const flow_watch *watch = &run->watches[i];
        if (watch->node != n || !watch->output || watch->pin != pin) continue;
        *out = watch->value;
        if (out->type == PIN_STRUCT) {
            if (!watch->owned || !struct_storage || struct_storage_size < watch->owned_size)
                return -1;
            memcpy(struct_storage, watch->owned, watch->owned_size);
            out->ptr = struct_storage;
        }
        return 0;
    }
    return -1;
}

double flow_context_now(const struct flow_context *ctx)
{ return ctx && ctx->run ? ctx->run->current_time : 0.0; }

static void flow_coerce_real(pin_value *value, const pin_spec *target)
{
    if (value->type == PIN_F32 && target->type == PIN_F64) {
        value->type = PIN_F64;
        value->type_name = target->type_name;
        value->d = (double)value->f;
    } else if (value->type == PIN_F64 && target->type == PIN_F32) {
        value->type = PIN_F32;
        value->type_name = target->type_name;
        value->f = (float)value->d;
    }
}

int flow_context_eval_data(struct flow_context *ctx, int input_pin, pin_value *out)
{
    if (!ctx || !ctx->run || !out || input_pin < 0 || input_pin >= ctx->node->input_count)
        return flow_context_fail(ctx, FLOW_ERR_ARGUMENT, "invalid data input");
    const pin_spec *spec = &ctx->node->input_pins[input_pin];
    if (spec->kind != PIN_DATA)
        return flow_context_fail(ctx, FLOW_ERR_INVALID_NODE, "EXEC input used as data");

    node *src = NULL;
    int src_pin = 0;
    if (graph_input_source(ctx->run->graph, ctx->node, input_pin, &src, &src_pin)) {
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
        flow_coerce_real(out, spec);
        ctx->node->inputs[input_pin] = *out;
        return FLOW_OK;
    }
    *out = ctx->node->inputs[input_pin];
    out->type = spec->type;
    out->type_name = spec->type_name;
    return FLOW_OK;
}

int flow_context_fail(struct flow_context *ctx, int code, const char *message)
{
    if (!ctx || !ctx->run) return code;
    ctx->failed = 1;
    flow_fail(ctx->run, code, ctx->node, message ? message : "flow node failed");
    return code;
}

int flow_context_pause_until(struct flow_context *ctx, double monotonic_time)
{
    if (!ctx || !ctx->run) return FLOW_ERR_ARGUMENT;
    if (monotonic_time <= ctx->run->current_time) return FLOW_OK;
    ctx->pause_requested = 1;
    ctx->pause_time = monotonic_time;
    ctx->pause_output = -1;
    return FLOW_SUSPENDED_RESULT;
}

int flow_context_continue(struct flow_context *ctx, int output_pin)
{
    if (!ctx || !ctx->run) return FLOW_ERR_ARGUMENT;
    if (ctx->pause_requested) {
        ctx->pause_output = output_pin;
        return FLOW_OK;
    }
    return schedule_output(ctx, output_pin);
}

int flow_context_loop(struct flow_context *ctx, int first, int last)
{
    if (!ctx || !ctx->run) return FLOW_ERR_ARGUMENT;
    if (ctx->node->output_count < 3 || ctx->node->output_pins[0].kind != PIN_EXEC ||
        ctx->node->output_pins[1].kind != PIN_DATA ||
        ctx->node->output_pins[2].kind != PIN_EXEC)
        return flow_context_fail(ctx, FLOW_ERR_INVALID_NODE, "invalid ForLoop descriptor");

    if (first > last) return flow_context_continue(ctx, 2);
    ctx->node->computed = 0;
    ctx->node->outputs[1].type = PIN_I32;
    ctx->node->outputs[1].type_name = "int";
    ctx->node->outputs[1].i = first;
    if (frame_push(ctx->run, (flow_frame){ ctx->node, FLOW_FRAME_LOOP, first, last }))
        return flow_context_fail(ctx, FLOW_ERR_NODE, "loop continuation allocation failed");
    return flow_context_continue(ctx, 0);
}

int flow_context_emit(struct flow_context *ctx, const char *message)
{
    if (!ctx || !ctx->run || !message)
        return flow_context_fail(ctx, FLOW_ERR_ARGUMENT, "invalid flow message");
    if (ctx->run->sink) ctx->run->sink(ctx->node, message, ctx->run->sink_user);
    return FLOW_OK;
}

flow_inputs flow_context_inputs(const struct flow_context *ctx)
{
    return ctx && ctx->run ? flow_run_inputs(ctx->run)
                           : (flow_inputs){ NULL, 0, NULL };
}
