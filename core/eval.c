#include "eval.h"

#include "instance.h"
#include "nodes.h"
#include "pin_conv.h"

void eval_context_init(eval_context *ctx)
{
    if (ctx) {
        *ctx = (eval_context){ 0 };
    }
}

void eval_context_reset(eval_context *ctx)
{
    if (ctx) {
        ctx->status = (eval_status){ 0, NULL, -1, NULL };
    }
}

const eval_status *eval_context_status(const eval_context *ctx)
{
    return ctx ? &ctx->status : NULL;
}

static int fail(eval_context *ctx, node *n, int pin, int code, const char *message)
{
    if (ctx) {
        ctx->status = (eval_status){ code, n, pin, message };
    }
    if (n) {
        n->visiting = 0;
        n->eval_error = code;
        n->eval_error_message = message;
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
    node *entry = graph_node_by_id(target, target->entry_id);
    node *ret = graph_node_by_id(target, target->return_id);
    if (!entry || !ret) {
        return fail(ctx, n, 0, NODE_EVAL_INVALID, "pure function has no entry or return");
    }
    int argument = 0;
    for (int i = 0; i < entry->output_count; i++) {
        if (entry->output_pins[i].kind != PIN_DATA) {
            continue;
        }
        if (argument >= n->input_count) {
            return fail(ctx, n, i, NODE_EVAL_INVALID, "pure function argument count mismatch");
        }
        entry->outputs[i] = n->inputs[argument++];
        pin_coerce(&entry->outputs[i], &entry->output_pins[i]);
    }
    graph_eval_reset(target);
    int result = 0;
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
            eval_context nested = *ctx;
            result = graph_eval_output(target, source, source_pin, a, &value, &nested);
            if (result) {
                ctx->status = nested.status;
                return result;
            }
            pin_coerce(&value, &ret->input_pins[i]);
        }
        ret->inputs[i] = value;
        if (output < n->output_count) {
            n->outputs[output++] = value;
        }
    }
    return 0;
}

static int eval_node(graph *g, node *n, arena *a, eval_context *ctx)
{
    if (n->computed) {
        return 0;
    }
    if (n->visiting) {
        return fail(ctx, n, -1, -2, "cycle detected");
    }

    n->visiting = 1;
    for (int i = 0; i < n->input_count; i++) {
        node *src;
        int src_pin;
        if (graph_input_source(g, n, i, &src, &src_pin)) {
            if (ctx->on_data_edge) {
                ctx->on_data_edge(src, src_pin, n, i, ctx->on_data_edge_user);
            }
            int r = graph_eval_output(g, src, src_pin, a, &n->inputs[i], ctx);
            if (r) {
                n->visiting = 0;
                return r;
            }
            pin_coerce(&n->inputs[i], &n->input_pins[i]);
        } else {
            n->inputs[i] = n->input_pins[i].default_value;
        }
    }
    switch (n->desc->pure_kind) {
        case NODE_PURE_CONST: node_eval_const(n, a); break;
        case NODE_PURE_MAKE:
            node_eval_make(n, a);
            if (n->output_count > 0 && n->outputs[0].type == PIN_STRUCT &&
                !n->outputs[0].ptr) {
                return fail(ctx, n, 0, NODE_EVAL_ALLOCATION, "arena allocation failed");
            }
            if (n->output_count > 0 && n->outputs[0].type != PIN_STRUCT) {
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "Make node produced invalid output");
            }
            break;
        case NODE_PURE_BREAK:
            if (!n->inputs[0].ptr) {
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "Break node has invalid struct input");
            }
            node_eval_break(n, a);
            for (int i = 0; i < n->output_count; i++) {
                if (n->outputs[i].type != n->output_pins[i].type) {
                    return fail(ctx, n, i, NODE_EVAL_INVALID, "Break node produced invalid output");
                }
            }
            break;
        case NODE_PURE_CALL:
            node_eval_call(n, a);
            if (n->output_count > 0 && n->output_pins[0].type == PIN_STRUCT &&
                !n->outputs[0].ptr) {
                return fail(ctx, n, 0, NODE_EVAL_ALLOCATION, "arena allocation failed");
            }
            break;
        case NODE_PURE_GET:
            if (!ctx->instance) {
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "Get Variable requires an instance");
            }
            if (blueprint_instance_get_variable(ctx->instance, n->reference_id, &n->outputs[0]) !=
                0) {
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "Get Variable references a lost variable");
            }
            break;
        case NODE_PURE_SELF:
            if (!ctx->instance) {
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "Self requires an instance");
            }
            n->outputs[0] = blueprint_instance_owner(ctx->instance);
            break;
        case NODE_PURE_FUNCTION:
            if (eval_pure_function(g, n, a, ctx)) {
                return ctx->status.code;
            }
            break;
        case NODE_PURE_VOID_CALL: break;
    }
    n->computed = 1;
    n->visiting = 0;
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
    int in_graph = 0;
    for (int i = 0; i < acount(g->nodes); i++) {
        in_graph |= g->nodes[i] == n;
    }
    if (!in_graph) {
        return fail(ctx, n, out_pin, NODE_EVAL_INVALID, "node is not in graph");
    }
    if (out_pin < 0 || out_pin >= n->output_count) {
        return fail(ctx, n, out_pin, -1, "output pin index out of range");
    }
    if (n->output_pins[out_pin].kind != PIN_DATA) {
        return fail(ctx, n, out_pin, NODE_EVAL_INVALID, "EXEC output is not a data value");
    }
    int r = eval_node(g, n, a, ctx);
    if (r) {
        return r;
    }
    *out = n->outputs[out_pin];
    return 0;
}

void graph_eval_reset(graph *g)
{
    if (!g) {
        return;
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        g->nodes[i]->computed = 0;
        g->nodes[i]->visiting = 0;
        g->nodes[i]->eval_error = 0;
        g->nodes[i]->eval_error_message = NULL;
    }
}
