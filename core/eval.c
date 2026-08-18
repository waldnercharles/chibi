#include "eval.h"

static int eval_node(graph *g, node *n, arena *a)
{
    if (n->computed) return 0;
    if (n->visiting) return -2; /* cycle */

    n->visiting = 1;
    for (int i = 0; i < n->desc->input_count; i++) {
        node *src;
        int src_pin;
        if (graph_input_source(g, n, i, &src, &src_pin)) {
            int r = graph_eval_output(g, src, src_pin, a, &n->inputs[i]);
            if (r) {
                n->visiting = 0;
                return r;
            }
        }
    }
    n->desc->eval(n, a);
    n->computed = 1;
    n->visiting = 0;
    return 0;
}

int graph_eval_output(graph *g, node *n, int out_pin, arena *a, pin_value *out)
{
    if (out_pin < 0 || out_pin >= n->desc->output_count) return -1;
    int r = eval_node(g, n, a);
    if (r) return r;
    *out = n->outputs[out_pin];
    return 0;
}

void graph_eval_reset(graph *g)
{
    for (int i = 0; i < g->node_count; i++) {
        g->nodes[i]->computed = 0;
        g->nodes[i]->visiting = 0;
    }
}
