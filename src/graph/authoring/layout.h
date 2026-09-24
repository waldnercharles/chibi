#ifndef CHIBI_GRAPH_LAYOUT_H
#define CHIBI_GRAPH_LAYOUT_H
#include "graph/model/graph.h"
/* Arrange authored graph data without a UI. Pin rows use standard dimensions;
 * the editor may refine those measurements using its current font metrics.
 * The result depends only on node IDs, pin rows and edges, and arranged nodes
 * become automatic, so documents omit their positions. */
void graph_layout(graph *graph);
/* Positions automatic nodes with graph_layout's arrangement; placed nodes keep
 * their authored positions. */
void graph_place_unplaced(graph *graph);
#endif
