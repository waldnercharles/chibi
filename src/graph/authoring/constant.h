#ifndef CHIBI_GRAPH_CONSTANT_H
#define CHIBI_GRAPH_CONSTANT_H

#include "graph/model/graph.h"

/* Evaluate one declared foldable pure call with literal inputs only. Values
 * follow the descriptor's pure-call lifetime contract; authored defaults remain
 * borrowed. Runtime bindings and dependencies are refused. */
bool graph_constant_value(const graph *g, const node *n, arena *storage, pin_value *value);

#endif
