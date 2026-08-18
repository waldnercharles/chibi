#include "graph.h"

#include <stdlib.h>
#include <string.h>

static void *xrealloc(void *p, size_t n) { return realloc(p, n); }

void graph_init(graph *g)
{
    if (g) {
        memset(g, 0, sizeof *g);
        g->topology_revision = 1;
    }
}

static int node_in_graph(const graph *g, const node *n)
{
    if (!g || !n) return 0;
    for (int i = 0; i < g->node_count; i++)
        if (g->nodes[i] == n) return 1;
    return 0;
}

static void graph_changed(graph *g)
{
    if (g) g->topology_revision++;
}

static int graph_mutation_allowed(const graph *g)
{ return g && g->flow_pause_count == 0; }

node *graph_add_node(graph *g, const node_desc *desc)
{
    if (!graph_mutation_allowed(g) || !desc || desc->input_count < 0 ||
        desc->output_count < 0 || (desc->input_count && !desc->inputs) ||
        (desc->output_count && !desc->outputs))
        return NULL;
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
    for (int i = 0; i < desc->input_count; i++) {
        n->inputs[i].type = desc->inputs[i].type;
        n->inputs[i].type_name = desc->inputs[i].type_name;
        if (desc->inputs[i].type == PIN_STR) n->inputs[i].str = "";
        /* Ponytail: typed zeros cover unconnected inputs; add defaults only
         * when Blueprint-style per-pin defaults become necessary. */
    }
    for (int i = 0; i < desc->output_count; i++) {
        n->outputs[i].type = desc->outputs[i].type;
        n->outputs[i].type_name = desc->outputs[i].type_name;
        if (desc->outputs[i].type == PIN_STR) n->outputs[i].str = "";
    }
    if (g->node_count == g->node_cap) {
        int next = g->node_cap ? g->node_cap * 2 : 8;
        node **nodes = (node **)xrealloc(g->nodes, (size_t)next * sizeof *nodes);
        if (!nodes) {
            free(n->outputs);
            free(n->inputs);
            free(n);
            return NULL;
        }
        g->nodes = nodes;
        g->node_cap = next;
    }
    g->nodes[g->node_count++] = n;
    graph_changed(g);
    return n;
}

int pin_types_compatible(const pin_spec *a, const pin_spec *b)
{
    if (!a || !b || a->type != b->type) return 0;
    if (a->type == PIN_STRUCT || a->type == PIN_ENUM)
        return a->type_name && b->type_name && strcmp(a->type_name, b->type_name) == 0;
    return 1;
}

static int grow_edges(graph *g)
{
    if (g->edge_count < g->edge_cap) return 0;
    int next = g->edge_cap ? g->edge_cap * 2 : 8;
    edge *edges = (edge *)xrealloc(g->edges, (size_t)next * sizeof *edges);
    if (!edges) return -1;
    g->edges = edges;
    g->edge_cap = next;
    return 0;
}

int graph_connect_data(graph *g, node *from, int from_pin, node *to, int to_pin)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, from) || !node_in_graph(g, to))
        return -1;
    if (from_pin < 0 || from_pin >= from->desc->output_count) return -1;
    if (to_pin < 0 || to_pin >= to->desc->input_count) return -1;
    const pin_spec *out = &from->desc->outputs[from_pin];
    const pin_spec *in = &to->desc->inputs[to_pin];
    if (out->kind != PIN_DATA || in->kind != PIN_DATA) return -1;
    if (!pin_types_compatible(out, in)) return -1;

    /* DATA inputs have one driver. Replacing it keeps graph, editor, and JSON
     * on one invariant. */
    for (int i = 0; i < g->edge_count; i++) {
        edge *old = &g->edges[i];
        if (old->to == to && old->to_pin == to_pin) {
            if (old->kind != GRAPH_EDGE_DATA) return -1;
            *old = (edge){ from, from_pin, to, to_pin, GRAPH_EDGE_DATA, 0 };
            graph_changed(g);
            return 0;
        }
    }
    if (grow_edges(g)) return -1;
    g->edges[g->edge_count++] = (edge){ from,   from_pin,        to,
                                        to_pin, GRAPH_EDGE_DATA, 0 };
    graph_changed(g);
    return 0;
}

int graph_connect(graph *g, node *from, int from_pin, node *to, int to_pin)
{ return graph_connect_data(g, from, from_pin, to, to_pin); }

int graph_connect_exec(graph *g, node *from, int from_pin, node *to, int to_pin, int exec_order)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, from) ||
        !node_in_graph(g, to) || exec_order < 0)
        return -1;
    if (from_pin < 0 || from_pin >= from->desc->output_count) return -1;
    if (to_pin < 0 || to_pin >= to->desc->input_count) return -1;
    const pin_spec *out = &from->desc->outputs[from_pin];
    const pin_spec *in = &to->desc->inputs[to_pin];
    if (out->kind != PIN_EXEC || in->kind != PIN_EXEC) return -1;

    /* Every exec input has one driver. An ordinary output also has one target;
     * ordered fan-out is represented by Sequence's distinct output pins. */
    for (int i = 0; i < g->edge_count; i++) {
        edge *old = &g->edges[i];
        if (old->kind == GRAPH_EDGE_EXEC && old->from == from &&
            old->from_pin == from_pin && (old->to != to || old->to_pin != to_pin))
            return -1;
        if (old->to == to && old->to_pin == to_pin) {
            if (old->kind != GRAPH_EDGE_EXEC) return -1;
            *old = (edge){ from,   from_pin,        to,
                           to_pin, GRAPH_EDGE_EXEC, exec_order };
            graph_changed(g);
            return 0;
        }
    }
    if (grow_edges(g)) return -1;
    g->edges[g->edge_count++] = (edge){ from,   from_pin,        to,
                                        to_pin, GRAPH_EDGE_EXEC, exec_order };
    graph_changed(g);
    return 0;
}

unsigned long long graph_topology_revision(const graph *g)
{ return g ? g->topology_revision : 0; }

void graph_flow_pause(graph *g)
{
    if (g) g->flow_pause_count++;
}

void graph_flow_resume(graph *g)
{
    if (g && g->flow_pause_count) g->flow_pause_count--;
}

int graph_remove_node(graph *g, node *n)
{
    if (!graph_mutation_allowed(g)) return -1;
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
    if (w != g->edge_count) graph_changed(g);
    g->edge_count = w;

    free(n->inputs);
    free(n->outputs);
    free(n);

    for (int i = idx; i < g->node_count - 1; i++) g->nodes[i] = g->nodes[i + 1];
    g->node_count--;
    graph_changed(g);
    return 0;
}

int graph_remove_edge(graph *g, node *from, int from_pin, node *to, int to_pin)
{
    if (!graph_mutation_allowed(g)) return -1;
    for (int i = 0; i < g->edge_count; i++) {
        const edge *e = &g->edges[i];
        if (e->from != from || e->from_pin != from_pin) continue;
        if (e->to != to || e->to_pin != to_pin) continue;
        g->edges[i] = g->edges[g->edge_count - 1];
        g->edge_count--;
        graph_changed(g);
        return 0;
    }
    return -1;
}

int graph_input_source(const graph *g, const node *n, int pin, node **src, int *src_pin)
{
    if (!g || !n || !src || !src_pin || pin < 0 || pin >= n->desc->input_count)
        return 0;
    for (int i = 0; i < g->edge_count; i++) {
        const edge *e = &g->edges[i];
        if (e->kind != GRAPH_EDGE_DATA || e->to != n || e->to_pin != pin)
            continue;
        *src = e->from;
        *src_pin = e->from_pin;
        return 1;
    }
    return 0;
}

int graph_exec_input_source(const graph *g, const node *n, int pin, node **src, int *src_pin, int *order)
{
    if (!g || !n || !src || !src_pin || !order || pin < 0 ||
        pin >= n->desc->input_count || n->desc->inputs[pin].kind != PIN_EXEC)
        return 0;
    for (int i = 0; i < g->edge_count; i++) {
        const edge *e = &g->edges[i];
        if (e->kind != GRAPH_EDGE_EXEC || e->to != n || e->to_pin != pin)
            continue;
        *src = e->from;
        *src_pin = e->from_pin;
        *order = e->exec_order;
        return 1;
    }
    return 0;
}

int graph_exec_output_target(const graph *g, const node *n, int pin, int order, node **dst, int *dst_pin)
{
    if (!g || !n || !dst || !dst_pin || pin < 0 || pin >= n->desc->output_count ||
        n->desc->outputs[pin].kind != PIN_EXEC || order < 0)
        return 0;
    for (int i = 0; i < g->edge_count; i++) {
        const edge *e = &g->edges[i];
        if (e->kind != GRAPH_EDGE_EXEC || e->from != n || e->from_pin != pin ||
            e->exec_order != order)
            continue;
        *dst = e->to;
        *dst_pin = e->to_pin;
        return 1;
    }
    return 0;
}

int graph_exec_target_at(const graph *g, const node *n, int pin, int order, node **dst, int *dst_pin)
{ return graph_exec_output_target(g, n, pin, order, dst, dst_pin); }

void graph_free(graph *g)
{
    if (!g) return;
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

int graph_set_event_name(graph *g, node *event_node, const char *name)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, event_node) ||
        !event_node->desc || event_node->desc->flow_kind != NODE_FLOW_EVENT ||
        !name || !name[0])
        return -1;
    char *copy = graph_own_str(g, name, strlen(name));
    if (!copy) return -1;
    event_node->event_name = copy;
    graph_changed(g);
    return 0;
}

const char *graph_event_name(const node *event_node)
{ return event_node ? event_node->event_name : NULL; }

char *graph_own_str(graph *g, const char *s, size_t len)
{
    if (g->string_count == g->string_cap) {
        int next = g->string_cap ? g->string_cap * 2 : 8;
        char **ns = (char **)xrealloc(g->strings, (size_t)next * sizeof *ns);
        if (!ns) return NULL;
        g->strings = ns;
        g->string_cap = next;
    }
    char *copy = (char *)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len);
    copy[len] = '\0';
    g->strings[g->string_count++] = copy;
    return copy;
}
