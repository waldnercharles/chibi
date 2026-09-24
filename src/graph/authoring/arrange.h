#ifndef CHIBI_ARRANGE_H
#define CHIBI_ARRANGE_H

#include "support/math.h"

#include <stdbool.h>

/* Layered auto-arrange for the node canvas.
 *
 * This is pure geometry: no ImGui, no graph types, no editor state. The caller
 * measures its nodes, describes its wires, and gets one position per node
 * back, so the layout quality is measurable in a headless test.
 *
 * A renderer arranges with arrange_layout, recomputes waypoints with
 * arrange_route whenever positions change, and draws each wire as the cubic
 * pieces arrange_path returns. All three take the same style. */

/* How wires are drawn between their waypoints. */
typedef enum arrange_route_style
{
    /* Cubics that cross every waypoint horizontally. */
    ARRANGE_ROUTE_CURVED,
    /* Horizontal and vertical runs with rounded corners. Each vertical run
     * sits on its own track in the gap between two columns, so parallel runs
     * never overlap. */
    ARRANGE_ROUTE_ORTHOGONAL,
    /* Line segments between waypoints, leaving and entering pins level. */
    ARRANGE_ROUTE_STRAIGHT,
    /* The orthogonal tracks with 45 degree diagonals in place of corners. */
    ARRANGE_ROUTE_SUBWAY,
    ARRANGE_ROUTE_STYLE_COUNT,
} arrange_route_style;

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

/* Positions use the shared scalar vector type while retaining a domain name
 * at the arrange API boundary. */
typedef vec2 arrange_point;

typedef struct arrange_graph
{
    const arrange_box *boxes;
    int box_count;
    const arrange_link *links;
    int link_count;
} arrange_graph;

/* Readability defects of a placement, counted on the wires as the style draws
 * them, routes included. Wires that share a pin never cross or overlap each
 * other, and where they run together they are drawn, crossed and inked
 * once. */
typedef struct arrange_score
{
    int wire_crossings; /* points where wires cross */
    int wire_node_hits; /* (wire, unrelated node) collisions */
    int node_overlaps;  /* node pairs whose boxes overlap */
    int wire_overlaps;  /* wire pairs that run on top of each other */
    int wire_bends;     /* turns, two wherever a wire changes height */
    int flow_bends;     /* the turns on execution wires */
    float wire_length;  /* every wire's full length */
    float ink_length;   /* drawn length, with shared trunks counted once */
} arrange_score;

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
 * calls. Wires with custom handles, and curved backward wires, keep their own
 * curve and get no waypoints. */
void arrange_route(
    const arrange_graph *g,
    const arrange_point *pos,
    arrange_route_style style,
    arrange_routes *routes
);
void arrange_routes_free(arrange_routes *routes);

/* One drawable piece of a wire: a cubic Bezier. Straight pieces keep their
 * handles on their ends (p1 == p0, p2 == p3). `segment` is the stretch of the
 * wire the piece lies on: 0 runs from the start to the first waypoint. */
typedef struct arrange_piece
{
    arrange_point p0;
    arrange_point p1;
    arrange_point p2;
    arrange_point p3;
    int segment;
} arrange_piece;

/* Appends the pieces that draw one wire from its output pin `from` to its
 * input pin `to` through `count` waypoints, in canvas units: arrange_route's
 * waypoints or authored reroutes. Consecutive pieces join end to start. Every
 * renderer and arrange_measure draw wires with this, so what is measured is
 * what is drawn. A link with custom handles draws as its single authored
 * cubic in every style instead. */
void arrange_path(
    arrange_route_style style,
    arrange_point from,
    arrange_point to,
    const arrange_point *knots,
    int count,
    arrange_piece **pieces
);

arrange_score arrange_measure(const arrange_graph *g, const arrange_point *pos, arrange_route_style style);

/* The single scalar a placement is judged by. */
float arrange_cost(arrange_score score);

/* Links long enough to be worth naming as a local: those whose ends are at
 * least ARRANGE_LONG_COLUMNS columns apart, or whose drawn path crosses at
 * least `min_crossings` other wires. Appends link indexes in ascending order
 * to `links` (support/array.h). This only reports; the caller decides which
 * links carry values that can become locals. */
#define ARRANGE_LONG_COLUMNS 3
void arrange_long_links(
    const arrange_graph *g,
    const arrange_point *pos,
    arrange_route_style style,
    int min_crossings,
    int **links
);

#endif
