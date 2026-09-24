#ifndef CHIBI_GRAPH_LAYOUT_H
#define CHIBI_GRAPH_LAYOUT_H
#include "graph/authoring/arrange.h"
#include "graph/model/graph.h"
/* Arrange authored graph data without a UI. Pin rows use standard dimensions;
 * the editor may refine those measurements using its current font metrics.
 * The result depends only on node IDs, pin rows and edges, and arranged nodes
 * become automatic, so documents omit their positions. */
void graph_layout(graph *graph);
/* Positions automatic nodes with graph_layout's arrangement; placed nodes keep
 * their authored positions. */
void graph_place_unplaced(graph *graph);
/* Auto-arrange stacks a graph's event chains top to bottom by this band:
 * Begin, Tick, the other host events such as input and contact, custom
 * events, End, then everything else. Chains that share nodes form one band. */
int graph_layout_band(const node *n);
/* The fixed measurement graph_layout arranges with. Rows follow Studio's full
 * nodes: execution pins, then data outputs, then data inputs, counting only
 * the pins a node draws. */
arrange_box graph_layout_box(const graph *g, const node *n);
float graph_layout_pin_y(const graph *g, const node *n, bool output, int pin);
#endif
