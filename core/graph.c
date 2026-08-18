#include "graph.h"

#include <stdlib.h>
#include <string.h>

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n);
    if (!q) {
        free(p);
        return NULL;
    }
    return q;
}

void graph_init(graph *g) { memset(g, 0, sizeof *g); }

node *graph_add_node(graph *g, const node_desc *desc)
{
    node *n = (node *)calloc(1, sizeof *n);
    if (!n) return NULL;
    n->desc = desc;
    if (desc->input_count) {
        n->inputs = (pin_value *)calloc((size_t)desc->input_count, sizeof *n->inputs);
        if (!n->inputs) {
            free(n);
            return NULL;
        }
    }
    if (desc->output_count) {
        n->outputs = (pin_value *)calloc((size_t)desc->output_count, sizeof *n->outputs);
        if (!n->outputs) {
            free(n->inputs);
            free(n);
            return NULL;
        }
    }
    if (g->node_count == g->node_cap) {
        g->node_cap = g->node_cap ? g->node_cap * 2 : 8;
        g->nodes = (node **)xrealloc(g->nodes, (size_t)g->node_cap * sizeof *g->nodes);
        if (!g->nodes) {
            free(n->outputs);
            free(n->inputs);
            free(n);
            return NULL;
        }
    }
    g->nodes[g->node_count++] = n;
    return n;
}

int pin_types_compatible(const pin_spec *a, const pin_spec *b)
{
    if (a->type != b->type) return 0;
    if (a->type == PIN_STRUCT || a->type == PIN_ENUM)
        return a->type_name && b->type_name && strcmp(a->type_name, b->type_name) == 0;
    return 1;
}

int graph_connect(graph *g, node *from, int from_pin, node *to, int to_pin)
{
    if (!from || !to) return -1;
    if (from_pin < 0 || from_pin >= from->desc->output_count) return -1;
    if (to_pin < 0 || to_pin >= to->desc->input_count) return -1;
    if (!pin_types_compatible(&from->desc->outputs[from_pin], &to->desc->inputs[to_pin]))
        return -1;

    if (g->edge_count == g->edge_cap) {
        g->edge_cap = g->edge_cap ? g->edge_cap * 2 : 8;
        g->edges = (edge *)xrealloc(g->edges, (size_t)g->edge_cap * sizeof *g->edges);
        if (!g->edges) return -1;
    }
    g->edges[g->edge_count++] = (edge){ from, from_pin, to, to_pin };
    return 0;
}

int graph_remove_node(graph *g, node *n)
{
    int idx = -1;
    for (int i = 0; i < g->node_count; i++)
        if (g->nodes[i] == n) {
            idx = i;
            break;
        }
    if (idx < 0) return -1;

    /* Drop every edge touching n. */
    int w = 0;
    for (int i = 0; i < g->edge_count; i++) {
        const edge *e = &g->edges[i];
        if (e->from == n || e->to == n) continue;
        g->edges[w++] = *e;
    }
    g->edge_count = w;

    free(n->inputs);
    free(n->outputs);
    free(n);

    for (int i = idx; i < g->node_count - 1; i++) g->nodes[i] = g->nodes[i + 1];
    g->node_count--;
    return 0;
}

int graph_remove_edge(graph *g, node *from, int from_pin, node *to, int to_pin)
{
    for (int i = 0; i < g->edge_count; i++) {
        const edge *e = &g->edges[i];
        if (e->from != from || e->from_pin != from_pin) continue;
        if (e->to != to || e->to_pin != to_pin) continue;
        g->edges[i] = g->edges[g->edge_count - 1];
        g->edge_count--;
        return 0;
    }
    return -1;
}

int graph_input_source(const graph *g, const node *n, int pin, node **src, int *src_pin)
{
    for (int i = 0; i < g->edge_count; i++) {
        const edge *e = &g->edges[i];
        if (e->to == n && e->to_pin == pin) {
            *src = e->from;
            *src_pin = e->from_pin;
            return 1;
        }
    }
    return 0;
}

void graph_free(graph *g)
{
    for (int i = 0; i < g->node_count; i++) {
        free(g->nodes[i]->inputs);
        free(g->nodes[i]->outputs);
        free(g->nodes[i]);
    }
    free(g->nodes);
    free(g->edges);
    for (int i = 0; i < g->string_count; i++) free(g->strings[i]);
    free(g->strings);
    memset(g, 0, sizeof *g);
}

char *graph_own_str(graph *g, const char *s, size_t len)
{
    if (g->string_count == g->string_cap) {
        g->string_cap = g->string_cap ? g->string_cap * 2 : 8;
        char **ns = (char **)xrealloc(g->strings, (size_t)g->string_cap * sizeof *ns);
        if (!ns) return NULL;
        g->strings = ns;
    }
    char *copy = (char *)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len);
    copy[len] = '\0';
    g->strings[g->string_count++] = copy;
    return copy;
}
