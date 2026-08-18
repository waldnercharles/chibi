#ifndef CBLUEPRINT_GRAPH_H
#define CBLUEPRINT_GRAPH_H

#include "node.h"

#include <stddef.h>

typedef struct edge
{
    node *from;
    int from_pin; /* output pin index */
    node *to;
    int to_pin; /* input pin index */
} edge;

typedef struct graph
{
    node **nodes;
    int node_count, node_cap;
    edge *edges;
    int edge_count, edge_cap;
    char **strings; /* owned PIN_STR literals; freed by graph_free */
    int string_count, string_cap;
} graph;

void graph_init(graph *g);
void graph_free(graph *g);

node *graph_add_node(graph *g, const node_desc *desc);

/* Returns 0 on success, non-zero on bad index or incompatible types. */
int graph_connect(graph *g, node *from, int from_pin, node *to, int to_pin);

/* Removes n and every edge touching it. Returns 0 on success, -1 if n is not
 * in the graph. */
int graph_remove_node(graph *g, node *n);

/* Removes the first edge matching the endpoints. Returns 0 on success, -1 if
 * no such edge exists. */
int graph_remove_edge(graph *g, node *from, int from_pin, node *to, int to_pin);

/* Fills *src / *src_pin if n's input pin is driven by an edge. Returns 1 if
 * connected, 0 otherwise (pin holds a literal). */
int graph_input_source(const graph *g, const node *n, int pin, node **src, int *src_pin);

int pin_types_compatible(const pin_spec *a, const pin_spec *b);

/* Duplicate s (len bytes, plus NUL) into graph-owned storage. Returns a stable
 * pointer the graph frees, or NULL on OOM. */
char *graph_own_str(graph *g, const char *s, size_t len);

#endif
