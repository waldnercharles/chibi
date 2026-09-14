#ifndef CHIBI_GRAPH_LAYOUT_H
#define CHIBI_GRAPH_LAYOUT_H
#include "graph/model/graph.h"
/* Arrange authored graph data without a UI. Pin rows use standard dimensions;
 * the editor may refine those measurements using its current font metrics. */
void graph_layout(graph *graph);
#endif
