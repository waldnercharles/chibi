#ifndef CBLUEPRINT_DEMO_GRAPH_H
#define CBLUEPRINT_DEMO_GRAPH_H

#include "core/graph.h"

/* Builds a small working graph in code: const 2.0 + const 3.0 -> add_floats.
 * Returns 0 on success, -1 if a required node type is missing or on OOM. */
int demo_graph_seed(graph *g);

#endif
