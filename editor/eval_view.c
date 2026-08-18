#include "eval_view.h"

#include "core/arena.h"
#include "core/eval.h"
#include "core/pin_fmt.h"

#include <stdlib.h>

#define EV_ARENA_CAP 65536

struct eval_view
{
    arena a;
};

eval_view *eval_view_new(void)
{
    eval_view *v = (eval_view *)calloc(1, sizeof *v);
    if (!v) return NULL;
    arena_init(&v->a, EV_ARENA_CAP);
    return v;
}

void eval_view_free(eval_view *v)
{
    if (!v) return;
    arena_free(&v->a);
    free(v);
}

void eval_view_refresh(eval_view *v, graph *g)
{
    graph_eval_reset(g);
    arena_reset(&v->a);
    for (int i = 0; i < g->node_count; i++) {
        node *n = g->nodes[i];
        for (int p = 0; p < n->desc->output_count; p++) {
            pin_value out;
            graph_eval_output(g, n, p, &v->a, &out);
        }
    }
}

const char *eval_view_output(const node *n, int pin)
{
    if (!n || pin < 0 || pin >= n->desc->output_count) return "";
    return pin_value_to_string(&n->outputs[pin]);
}

const char *eval_view_input(const graph *g, const node *n, int pin)
{
    node *src;
    int src_pin;
    if (graph_input_source(g, n, pin, &src, &src_pin)) return NULL;
    pin_value val = n->inputs[pin];
    val.type = n->desc->inputs[pin].type;
    return pin_value_to_string(&val);
}
