#include "eval.h"

#include "instance.h"
#include "nodes.h"
#include "pin_conv.h"
#include "runtime.h"

void eval_context_init(eval_context *ctx)
{
    if (ctx) {
        *ctx = (eval_context){ 0 };
    }
}

void eval_context_free(eval_context *ctx)
{
    eval_context_detach_runtime(ctx);
}

void eval_context_reset(eval_context *ctx)
{
    if (ctx) {
        ctx->status = (eval_status){ 0, NULL, -1, NULL };
    }
}

void eval_context_reset_runtime(eval_context *ctx)
{
    if (ctx) {
        runtime_store_reset(ctx->runtime);
    }
}

void eval_context_attach_runtime(eval_context *ctx, runtime_store *runtime)
{
    if (!ctx) {
        return;
    }
    eval_context_detach_runtime(ctx);
    ctx->runtime = runtime;
    ctx->runtime_owned = 0;
}

void eval_context_detach_runtime(eval_context *ctx)
{
    if (!ctx) {
        return;
    }
    if (ctx->runtime_owned) {
        runtime_store_free(ctx->runtime);
    }
    ctx->runtime = NULL;
    ctx->runtime_owned = 0;
}

const eval_status *eval_context_status(const eval_context *ctx)
{
    return ctx ? &ctx->status : NULL;
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

static runtime_node *runtime_state(eval_context *ctx, node *n)
{
    if (!ctx || !ctx->runtime ||
        !graph_has_node(runtime_store_graph(ctx->runtime), n)) {
        return NULL;
    }
    return runtime_store_node(ctx->runtime, n);
}

static int fail(eval_context *ctx, node *n, int pin, int code, const char *message)
{
    if (ctx) {
        ctx->status = (eval_status){ code, n, pin, message };
        runtime_node *state = runtime_state(ctx, n);
        if (state) {
            runtime_node_set_visiting(state, 0);
            runtime_node_error(state, code, message);
        }
    }
    return code;
}

static int eval_pure_function(graph *g, node *n, arena *a, eval_context *ctx)
{
    if (!ctx->instance) {
        return fail(ctx, n, 0, NODE_EVAL_INVALID, "pure function requires an instance");
    }
    graph *target = blueprint_find_graph(ctx->instance->bp, n->reference_id);
    if (!target || target->kind != GRAPH_FUNCTION) {
        return fail(ctx, n, 0, NODE_EVAL_INVALID, "pure function graph is missing");
    }
    if (target->call_depth_limit &&
        ctx->call_depth >= (unsigned)target->call_depth_limit) {
        return fail(ctx, n, 0, NODE_EVAL_INVALID, "pure function call depth limit exceeded");
    }
    node *entry = graph_node_by_id(target, target->entry_id);
    node *ret = graph_node_by_id(target, target->return_id);
    if (!entry || !ret) {
        return fail(ctx, n, 0, NODE_EVAL_INVALID, "pure function has no entry or return");
    }

    runtime_store *target_runtime = runtime_store_create(target);
    eval_context nested;
    eval_context_init(&nested);
    nested.instance = ctx->instance;
    nested.call_depth = ctx->call_depth + 1;
    nested.on_data_edge = ctx->on_data_edge;
    nested.on_data_edge_user = ctx->on_data_edge_user;
    eval_context_attach_runtime(&nested, target_runtime);

    runtime_node *caller_state = runtime_state(ctx, n);
    runtime_node *entry_state = runtime_store_node(target_runtime, entry);
    runtime_node *return_state = runtime_store_node(target_runtime, ret);
    pin_value *caller_inputs = runtime_node_inputs(caller_state);
    pin_value *caller_outputs = runtime_node_outputs(caller_state);
    pin_value *entry_outputs = runtime_node_outputs(entry_state);
    pin_value *return_inputs = runtime_node_inputs(return_state);
    int result = 0;
    int argument = 0;
    for (int i = 0; i < entry->output_count; i++) {
        if (entry->output_pins[i].kind != PIN_DATA) {
            continue;
        }
        if (argument >= n->input_count) {
            result = fail(ctx, n, i, NODE_EVAL_INVALID, "pure function argument count mismatch");
            goto done;
        }
        entry_outputs[i] = caller_inputs[argument++];
        pin_coerce(&entry_outputs[i], &entry->output_pins[i]);
    }
    runtime_store_reset_memo(target_runtime);
    int output = 0;
    for (int i = 0; i < ret->input_count; i++) {
        if (ret->input_pins[i].kind != PIN_DATA) {
            continue;
        }
        node *source = NULL;
        int source_pin = -1;
        pin_value value = ret->input_pins[i].default_value;
        if (graph_input_source(target, ret, i, &source, &source_pin)) {
            if (ctx->on_data_edge) {
                ctx->on_data_edge(source, source_pin, ret, i, ctx->on_data_edge_user);
            }
            result = graph_eval_output(target, source, source_pin, a, &value, &nested);
            if (result) {
                ctx->status = nested.status;
                goto done;
            }
            pin_coerce(&value, &ret->input_pins[i]);
        }
        return_inputs[i] = value;
        if (output < n->output_count) {
            caller_outputs[output++] = value;
        }
    }

done:
    eval_context_free(&nested);
    runtime_store_free(target_runtime);
    return result;
}

static int eval_node(graph *g, node *n, arena *a, eval_context *ctx)
{
    runtime_node *state = runtime_state(ctx, n);
    if (!state) {
        return fail(ctx, n, -1, NODE_EVAL_INVALID, "node runtime state is unavailable");
    }
    pin_value *inputs = runtime_node_inputs(state);
    pin_value *outputs = runtime_node_outputs(state);
    if (runtime_node_computed(state)) {
        return 0;
    }
    if (runtime_node_visiting(state)) {
        return fail(ctx, n, -1, -2, "cycle detected");
    }

    runtime_node_set_visiting(state, 1);
    for (int i = 0; i < n->input_count; i++) {
        node *src;
        int src_pin;
        if (graph_input_source(g, n, i, &src, &src_pin)) {
            if (ctx->on_data_edge) {
                ctx->on_data_edge(src, src_pin, n, i, ctx->on_data_edge_user);
            }
            int r = graph_eval_output(g, src, src_pin, a, &inputs[i], ctx);
            if (r) {
                runtime_node_set_visiting(state, 0);
                return r;
            }
            const node_desc *conversion = graph_input_conversion(g, n, i);
            if (conversion) {
                if (!conversion->fn) {
                    runtime_node_set_visiting(state, 0);
                    return fail(ctx, n, i, NODE_EVAL_INVALID, "edge conversion has no function");
                }
                inputs[i] = conversion->fn(&inputs[i], 1, a);
                if (inputs[i].type != n->input_pins[i].type ||
                    (inputs[i].type == PIN_STRUCT && !inputs[i].ptr)) {
                    runtime_node_set_visiting(state, 0);
                    return fail(ctx, n, i, NODE_EVAL_INVALID, "edge conversion failed");
                }
            }
            pin_coerce(&inputs[i], &n->input_pins[i]);
        } else {
            inputs[i] = n->input_pins[i].default_value;
        }
    }
    /* Reflected wrappers pass structs by value and dereference ptr. */
    if (n->desc->pure_kind == NODE_PURE_CALL) {
        for (int i = 0; i < n->input_count; i++) {
            if (n->input_pins[i].kind == PIN_DATA &&
                n->input_pins[i].type == PIN_STRUCT && !inputs[i].ptr) {
                runtime_node_set_visiting(state, 0);
                return fail(ctx, n, i, NODE_EVAL_INVALID, "data input has no struct value");
            }
        }
    }
    switch (n->desc->pure_kind) {
        case NODE_PURE_CONST: node_eval_const(n, state, a); break;
        case NODE_PURE_MAKE:
            node_eval_make(n, state, a);
            if (n->output_count > 0 && outputs[0].type == PIN_STRUCT &&
                !outputs[0].ptr) {
                return fail(ctx, n, 0, NODE_EVAL_ALLOCATION, "arena allocation failed");
            }
            if (n->output_count > 0 && outputs[0].type != PIN_STRUCT) {
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "Make node produced invalid output");
            }
            break;
        case NODE_PURE_BREAK:
            if (!inputs[0].ptr) {
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "Break node has invalid struct input");
            }
            node_eval_break(n, state, a);
            for (int i = 0; i < n->output_count; i++) {
                if (outputs[i].type != n->output_pins[i].type) {
                    return fail(ctx, n, i, NODE_EVAL_INVALID, "Break node produced invalid output");
                }
            }
            break;
        case NODE_PURE_CALL:
            node_eval_call(n, state, a);
            if (n->output_count > 0 && n->output_pins[0].type == PIN_STRUCT &&
                !outputs[0].ptr) {
                return fail(ctx, n, 0, NODE_EVAL_ALLOCATION, "arena allocation failed");
            }
            break;
        case NODE_PURE_GET:
            if (!ctx->instance) {
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "Get Variable requires an instance");
            }
            if (blueprint_instance_get_variable(ctx->instance, n->reference_id, &outputs[0]) !=
                0) {
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "Get Variable references a lost variable");
            }
            break;
        case NODE_PURE_SELF:
            if (!ctx->instance) {
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "Self requires an instance");
            }
            outputs[0] = blueprint_instance_owner(ctx->instance);
            break;
        case NODE_PURE_FUNCTION:
            if (eval_pure_function(g, n, a, ctx)) {
                runtime_node_set_visiting(state, 0);
                return ctx->status.code;
            }
            break;
        case NODE_PURE_VOID_CALL: break;
    }
    runtime_node_set_computed(state, 1);
    runtime_node_set_visiting(state, 0);
    return 0;
}

int graph_eval_output(graph *g, node *n, int out_pin, arena *a, pin_value *out, eval_context *ctx)
{
    if (!ctx) {
        return -4;
    }
    eval_context_reset(ctx);
    if (!g || !n || !a || !out) {
        return fail(ctx, n, out_pin, -4, "invalid evaluation arguments");
    }
    if (!graph_has_node(g, n)) {
        return fail(ctx, n, out_pin, NODE_EVAL_INVALID, "node is not in graph");
    }
    if (out_pin < 0 || out_pin >= n->output_count) {
        return fail(ctx, n, out_pin, -1, "output pin index out of range");
    }
    if (n->output_pins[out_pin].kind != PIN_DATA) {
        return fail(ctx, n, out_pin, NODE_EVAL_INVALID, "EXEC output is not a data value");
    }
    if (!ctx->runtime) {
        ctx->runtime = runtime_store_create(g);
        ctx->runtime_owned = 1;
    } else if (runtime_store_graph(ctx->runtime) != g) {
        if (!ctx->runtime_owned) {
            return fail(ctx, n, out_pin, NODE_EVAL_INVALID, "evaluation context graph mismatch");
        }
        runtime_store_free(ctx->runtime);
        ctx->runtime = runtime_store_create(g);
    }
    int r = eval_node(g, n, a, ctx);
    if (r) {
        return r;
    }
    const runtime_node *state = runtime_store_node_const(ctx->runtime, n);
    if (!state) {
        return fail(ctx, n, out_pin, NODE_EVAL_INVALID, "node runtime state is unavailable");
    }
    *out = runtime_node_outputs_const(state)[out_pin];
    return 0;
}

int eval_context_input(const eval_context *ctx, const node *n, int pin, pin_value *out)
{
    if (!ctx || !ctx->runtime || !n || !out || pin < 0 ||
        pin >= n->input_count || runtime_store_graph(ctx->runtime) == NULL ||
        !graph_has_node(runtime_store_graph(ctx->runtime), n)) {
        return -1;
    }
    const runtime_node *state = runtime_store_node_const(ctx->runtime, n);
    if (!state) {
        return -1;
    }
    *out = runtime_node_inputs_const(state)[pin];
    return 0;
}

int eval_context_output(const eval_context *ctx, const node *n, int pin, pin_value *out)
{
    if (!ctx || !ctx->runtime || !n || !out || pin < 0 ||
        pin >= n->output_count || runtime_store_graph(ctx->runtime) == NULL ||
        !graph_has_node(runtime_store_graph(ctx->runtime), n)) {
        return -1;
    }
    const runtime_node *state = runtime_store_node_const(ctx->runtime, n);
    if (!state) {
        return -1;
    }
    *out = runtime_node_outputs_const(state)[pin];
    return 0;
}

/* Deprecated: the graph no longer owns memo state, so only the caller's
 * eval_context can be reset. */
void graph_eval_reset(graph *g)
{
    (void)g;
}
