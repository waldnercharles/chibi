#ifndef CBLUEPRINT_GRAPH_H
#define CBLUEPRINT_GRAPH_H

#include "node.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        GRAPH_EDGE_DATA = 0,
        GRAPH_EDGE_EXEC = 1,
        GRAPH_EDGE_KIND_DATA = GRAPH_EDGE_DATA,
        GRAPH_EDGE_KIND_EXEC = GRAPH_EDGE_EXEC,
    } graph_edge_kind;

    typedef struct edge
    {
        node *from;
        int from_pin; /* output pin index */
        node *to;
        int to_pin; /* input pin index */
        graph_edge_kind kind;
        union
        {
            int exec_order; /* explicit order for EXEC edges; zero for DATA edges */
            int order;
        };
    } edge;

    typedef struct graph
    {
        node **nodes;
        int node_count, node_cap;
        edge *edges;
        int edge_count, edge_cap;
        char **strings; /* owned PIN_STR literals; freed by graph_free */
        int string_count, string_cap;
        unsigned long long topology_revision;
        unsigned flow_pause_count; /* guarded by flow.c while a run is non-idle */
    } graph;

    void graph_init(graph *g);
    void graph_free(graph *g);

    node *graph_add_node(graph *g, const node_desc *desc);

    /* DATA connection. Each data input has one driver; a new connection
     * replaces the old one. EXEC pins must use graph_connect_exec(). */
    int graph_connect_data(graph *g, node *from, int from_pin, node *to, int to_pin);
    int graph_connect(graph *g, node *from, int from_pin, node *to, int to_pin);

    /* EXEC connection. `exec_order` is persisted and must be non-negative. An
     * ordinary exec output accepts one target; Sequence outputs provide ordered
     * fan-out through their distinct output pins. */
    int graph_connect_exec(graph *g, node *from, int from_pin, node *to, int to_pin, int exec_order);

    /* Return the graph's mutation counter. A non-idle flow_run rejects changes. */
    unsigned long long graph_topology_revision(const graph *g);

    /* Event metadata is part of the graph model and is serialized with v2. */
    int graph_set_event_name(graph *g, node *event_node, const char *name);
    const char *graph_event_name(const node *event_node);

    /* Internal flow guard. Graph mutation APIs reject changes while non-idle. */
    void graph_flow_pause(graph *g);
    void graph_flow_resume(graph *g);

    /* Removes n and every edge touching it. Returns 0 on success, -1 if n is
     * not in the graph. */
    int graph_remove_node(graph *g, node *n);

    /* Removes the first edge matching the endpoints. Returns 0 on success, -1
     * if no such edge exists. */
    int graph_remove_edge(graph *g, node *from, int from_pin, node *to, int to_pin);

    /* Fills *src / *src_pin if n's input pin is driven by an edge. Returns 1 if
     * connected, 0 otherwise (pin holds a literal). */
    int graph_input_source(const graph *g, const node *n, int pin, node **src, int *src_pin);

    /* Fills *src / *src_pin / *order for an EXEC input. */
    int graph_exec_input_source(const graph *g, const node *n, int pin, node **src, int *src_pin, int *order);

    /* Find the target of one ordered EXEC output. */
    int graph_exec_output_target(const graph *g, const node *n, int pin, int order, node **dst, int *dst_pin);
    int graph_exec_target_at(const graph *g, const node *n, int pin, int order, node **dst, int *dst_pin);

    int pin_types_compatible(const pin_spec *a, const pin_spec *b);

    /* Duplicate s (len bytes, plus NUL) into graph-owned storage. Returns a
     * stable pointer the graph frees, or NULL on OOM. */
    char *graph_own_str(graph *g, const char *s, size_t len);

#ifdef __cplusplus
}
#endif

#endif
