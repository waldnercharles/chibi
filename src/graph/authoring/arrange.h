#ifndef CHIBI_ARRANGE_H
#define CHIBI_ARRANGE_H

#include "graph/authoring/arrange_path.h"

#include <stdbool.h>

/* Layered auto-arrange for the node canvas.
 *
 * This is pure geometry: no UI, no graph types, no editor state. The caller
 * measures its nodes, describes its wires, and gets one position per node
 * back, so the layout quality is measurable in a headless test with
 * arrange_score.h.
 *
 * A renderer arranges with arrange_layout, recomputes waypoints with
 * arrange_route whenever positions change, and draws each wire as the cubic
 * pieces arrange_path (arrange_path.h) returns. All three take the same
 * style. */

/* A measured node box. Connected boxes form one horizontal band, and bands
 * stack top to bottom by the lowest `band` among their boxes, then by their
 * lowest box index. */
typedef struct arrange_box
{
    float width;
    float height;
    int band;
} arrange_box;

/* One wire, drawn from an output pin on `from`'s right edge to an input pin on
 * `to`'s left edge. The caller measures where each pin sits: from_y and to_y
 * are offsets below the box tops, and links sharing an offset share the pin. */
typedef struct arrange_link
{
    int from;
    int to;
    float from_y;
    float to_y;
    float from_handle_x;
    float from_handle_y;
    float to_handle_x;
    float to_handle_y;
    bool custom_handles;
    bool flow; /* execution: nodes line up along these first */
} arrange_link;

typedef struct arrange_graph
{
    const arrange_box *boxes;
    int box_count;
    const arrange_link *links;
    int link_count;
} arrange_graph;

/* Arranges the graph into layered columns with the wire crossings driven
 * down, leaving a lane beside the boxes of every column a wire skips. Wires
 * from one output share their lanes until they split. Gaps between columns
 * widen to fit the orthogonal and subway tracks. `pos` is in-out: on entry it
 * holds the authored positions, which seed the within-column order so the
 * result stays recognisable; on exit it holds one position per box. */
void arrange_layout(const arrange_graph *g, arrange_route_style style, arrange_point *pos);

/* Waypoints that keep wires in the lanes between boxes. Link i passes through
 * points[start[i]] up to points[start[i + 1]]; a link without waypoints is
 * drawn by arrange_path from its pins alone. Both arrays use support/array.h. */
typedef struct arrange_routes
{
    int *start;
    arrange_point *points;
} arrange_routes;

/* Routes every wire that skips a column of boxes through a lane in that
 * column, and for orthogonal and subway wires assigns each vertical run a
 * track. Routes depend only on the graph, the positions and the style, so
 * callers recompute them rather than store them. `routes` is reused across
 * calls. Backward wires route around the boxes in every style. Wires with
 * custom handles, and forward wires within one column, keep their own curve
 * and get no waypoints. */
void arrange_route(
    const arrange_graph *g,
    const arrange_point *pos,
    arrange_route_style style,
    arrange_routes *routes
);
void arrange_routes_free(arrange_routes *routes);

#endif
