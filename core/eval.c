#include "eval.h"

#include "instance.h"
#include "nodes.h"
#include "pin_conv.h"

void eval_context_init(eval_context *ctx)
{
    if (ctx) *ctx = (eval_context){ 0 };
}

void eval_context_reset(eval_context *ctx)
{
    if (ctx) ctx->status = (eval_status){ 0, NULL, -1, NULL };
}

const eval_status *eval_context_status(const eval_context *ctx)
{ return ctx ? &ctx->status : NULL; }

static int fail(eval_context *ctx, node *n, int pin, int code, const char *message)
{
    if (ctx) ctx->status = (eval_status){ code, n, pin, message };
    if (n) {
        n->visiting = 0;
        n->eval_error = code;
        n->eval_error_message = message;
    }
    return code;
}

static int eval_node(graph *g, node *n, arena *a, eval_context *ctx)
{
    if (n->computed) return 0;
    if (n->visiting) { return fail(ctx, n, -1, -2, "cycle detected"); }

    n->visiting = 1;
    for (int i = 0; i < n->input_count; i++) {
        node *src;
        int src_pin;
        if (graph_input_source(g, n, i, &src, &src_pin)) {
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
                !n->outputs[0].ptr)
                return fail(ctx, n, 0, NODE_EVAL_ALLOCATION, "arena allocation failed");
            if (n->output_count > 0 && n->outputs[0].type != PIN_STRUCT)
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "Make node produced invalid output");
            break;
        case NODE_PURE_BREAK:
            if (!n->inputs[0].ptr)
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "Break node has invalid struct input");
            node_eval_break(n, a);
            for (int i = 0; i < n->output_count; i++)
                if (n->outputs[i].type != n->output_pins[i].type)
                    return fail(ctx, n, i, NODE_EVAL_INVALID, "Break node produced invalid output");
            break;
        case NODE_PURE_CALL:
            node_eval_call(n, a);
            if (n->output_count > 0 && n->output_pins[0].type == PIN_STRUCT &&
                !n->outputs[0].ptr)
                return fail(ctx, n, 0, NODE_EVAL_ALLOCATION, "arena allocation failed");
            break;
        case NODE_PURE_GET:
            if (!ctx->instance)
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "Get Variable requires an instance");
            if (blueprint_instance_get_variable(ctx->instance, n->reference_id, &n->outputs[0]) !=
                0)
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "Get Variable references a lost variable");
            break;
        case NODE_PURE_SELF:
            if (!ctx->instance)
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "Self requires an instance");
            n->outputs[0] = blueprint_instance_owner(ctx->instance);
            break;
    }
    n->computed = 1;
    n->visiting = 0;
    return 0;
}

int graph_eval_output(graph *g, node *n, int out_pin, arena *a, pin_value *out, eval_context *ctx)
{
    if (!ctx) return -4;
    eval_context_reset(ctx);
    if (!g || !n || !a || !out)
        return fail(ctx, n, out_pin, -4, "invalid evaluation arguments");
    int in_graph = 0;
    for (int i = 0; i < acount(g->nodes); i++) in_graph |= g->nodes[i] == n;
    if (!in_graph)
        return fail(ctx, n, out_pin, NODE_EVAL_INVALID, "node is not in graph");
    if (out_pin < 0 || out_pin >= n->output_count)
        return fail(ctx, n, out_pin, -1, "output pin index out of range");
    if (n->output_pins[out_pin].kind != PIN_DATA)
        return fail(ctx, n, out_pin, NODE_EVAL_INVALID, "EXEC output is not a data value");
    int r = eval_node(g, n, a, ctx);
    if (r) return r;
    *out = n->outputs[out_pin];
    return 0;
}

void graph_eval_reset(graph *g)
{
    if (!g) return;
    for (int i = 0; i < acount(g->nodes); i++) {
        g->nodes[i]->computed = 0;
        g->nodes[i]->visiting = 0;
        g->nodes[i]->eval_error = 0;
        g->nodes[i]->eval_error_message = NULL;
    }
}
