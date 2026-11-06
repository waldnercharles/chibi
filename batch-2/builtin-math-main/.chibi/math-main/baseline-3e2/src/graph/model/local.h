#ifndef CHIBI_GRAPH_LOCAL_H
#define CHIBI_GRAPH_LOCAL_H

#include "graph/model/graph.h"

/* Locals name an existing output. Uses retain ordinary DATA edges, including
 * conversions, so every evaluator and debugger sees the original dependency. */
typedef struct graph_local
{
    uint64_t id;
    const char *name;
    uint64_t source_id;
    uint64_t source_pin_id;
} graph_local;

const graph_local *graph_local_by_id(const graph *g, uint64_t id);
const graph_local *graph_output_local(const graph *g, const node *n, int pin);
const graph_local *graph_input_local(const graph *g, const node *n, int pin);
bool graph_edge_is_local(const graph *g, const edge *e);
/* Declares a proven producer before consumers are wired. */
uint64_t graph_declare_local(graph *g, node *source, int pin, const char *name);

/* Names must stay unique within the graph. */
int graph_rename_local(graph *g, uint64_t id, const char *name);
int graph_expand_local(graph *g, uint64_t id);

void graph_prune_locals(graph *g);

#endif
