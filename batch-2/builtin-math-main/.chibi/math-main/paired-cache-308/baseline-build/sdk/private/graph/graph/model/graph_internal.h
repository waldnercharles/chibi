#ifndef CHIBI_GRAPH_MODEL_INTERNAL_H
#define CHIBI_GRAPH_MODEL_INTERNAL_H

#include "graph/model/behavior_asset.h"
#include "graph/model/graph.h"

#include <stdatomic.h>

typedef struct exec_edges
{
    int first;
    int count;
} exec_edges;

typedef struct graph_edge_index
{
    int *inputs;         /* Destination pin ID -> edge slot + 1. */
    exec_edges *outputs; /* Source pin ID -> EXEC edge slots. */
    int *next;    /* EXEC edge slot -> next slot + 1, in authored edge order. */
    int *sources; /* Source node ID -> its only outgoing edge slot + 1, or -1. */
    atomic_ullong revision;
    atomic_flag building;
} graph_edge_index;

int graph_contains_node(const graph *g, const node *n);

void graph_changed(graph *g);

int graph_mutation_allowed(const graph *g);

/* Retypes one wildcard pin, keeping its default as authored data where the
 * type holds. */
void graph_wildcard_resolve_pin(graph *g, pin_spec *p, pin_type type, const char *type_name);

void graph_wildcard_resolve_group(graph *g, node *n, int group, pin_type type, const char *type_name);

edge graph_make_edge(
    node *from,
    int from_pin,
    node *to,
    int to_pin,
    graph_edge_kind kind,
    int order,
    const node_desc *conversion
);

node *graph_edge_source(const graph *g, const edge *e, int *pin);

graph_edge_index *graph_edges_index(const graph *g);

const edge *graph_input_edge(const graph *g, const node *n, int pin);

bool graph_vector_descriptor(const node_desc *desc);

int behavior_asset_variable_name_unique(const behavior_asset *asset, const char *name, uint64_t except_id);

bool graph_local_declaration_valid(graph *g, node *source, int pin, const char *name);

#endif
