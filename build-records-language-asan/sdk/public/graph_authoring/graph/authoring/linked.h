#ifndef CHIBI_GRAPH_LINKED_H
#define CHIBI_GRAPH_LINKED_H

#include "graph/model/graph.h"

#include <stdbool.h>

/* Nodes wired to the inputs (upstream) or the outputs of `from`, for growing
 * a selection: the direct neighbours (Blender's Select Linked From and To) or,
 * with `all`, every node reached through them (Unreal's Select All Input and
 * Output Nodes). Inline nodes are passed through, since their consumer draws
 * them. Appends each node outside `from` once to *out (support/array.h). */
void graph_linked_nodes(const graph *g, node *const *from, int count, bool upstream, bool all, node ***out);

#endif
