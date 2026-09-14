#ifndef CHIBI_GRAPH_OPTIMIZE_INTERNAL_H
#define CHIBI_GRAPH_OPTIMIZE_INTERNAL_H

#include "graph/authoring/optimize.h"

bool graph_set_output_is_current(const graph *g, const node *source, const node *consumer);
void graph_optimize_cleanup(graph *g, graph_optimize_report *report);

#endif
