#ifndef CHIBI_GRAPH_OPTIMIZE_INTERNAL_H
#define CHIBI_GRAPH_OPTIMIZE_INTERNAL_H

#include "graph/authoring/optimize.h"

bool graph_set_output_is_current(const graph *g, const node *source, const node *consumer);
/* The generated Self getter whose reads fold into component bindings: a
 * Get_<Component> node or a Get Component reading Self. NULL otherwise. */
const node_desc *graph_optimize_component_getter(const graph *g, const node *n);
void graph_optimize_cleanup(graph *g, graph_optimize_report *report);

#endif
