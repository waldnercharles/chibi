#ifndef CHIBI_GRAPH_MODEL_INTERNAL_H
#define CHIBI_GRAPH_MODEL_INTERNAL_H

#include "graph/model/behavior_asset.h"
#include "graph/model/graph.h"

#include <stdatomic.h>

typedef struct graph_edge_group
{
    uint64_t id;
    int first;
    int count;
} graph_edge_group;

typedef struct graph_edge_index
{
    graph_edge_group *inputs; /* Sorted destination pin IDs, first DATA edge. */
    graph_edge_group *outputs; /* Sorted source pin IDs, authored EXEC order. */
    int *next; /* EXEC edge slot -> next slot + 1, in authored edge order. */
    graph_edge_group *sources; /* Sorted source node IDs, outgoing edge counts. */
    atomic_ullong revision;
    atomic_flag building;
} graph_edge_index;

int graph_contains_node(const graph *g, const node *n);

/* Compact index slot, or -1. IDs change only before graph_reindex. */
int graph_node_slot(const graph *g, uint64_t id);

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

const edge *graph_only_output_edge(const graph *g, const node *n);

bool graph_vector_descriptor(const node_desc *desc);

int behavior_asset_variable_name_unique(const behavior_asset *asset, const char *name, uint64_t except_id);

bool graph_local_declaration_valid(graph *g, node *source, int pin, const char *name);

#endif
