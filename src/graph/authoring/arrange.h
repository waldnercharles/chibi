#ifndef CHIBI_ARRANGE_H
#define CHIBI_ARRANGE_H

#include "support/math.h"

#include <stdbool.h>

/* Layered auto-arrange for the node canvas.
 *
 * This is pure geometry: no ImGui, no graph types, no editor state. The caller
 * measures its nodes, describes its wires, and gets one position per node
 * back, so the layout quality is measurable in a headless test. */

/* A measured node box. */
typedef struct arrange_box
{
    float width;
    float height;
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

/* Readability defects of a placement, counted on the wires as the canvas
 * draws them, routes included. */
typedef struct arrange_score
{
    int wire_crossings; /* wire pairs whose curves intersect */
    int wire_node_hits; /* (wire, unrelated node) collisions */
    int node_overlaps;  /* node pairs whose boxes overlap */
    float wire_length;
} arrange_score;

/* Arranges the graph into layered columns with the wire crossings driven
 * down, leaving a lane beside the boxes of every column a wire skips. `pos` is
 * in-out: on entry it holds the authored positions, which seed the
 * within-column order so the result stays recognisable; on exit it holds one
 * position per box. */
void arrange_layout(const arrange_graph *g, arrange_point *pos);

/* Waypoints that keep wires in the lanes between boxes. Link i passes through
 * points[start[i]] up to points[start[i + 1]]; a link without waypoints is
 * drawn as one curve. Both arrays use support/array.h. */
typedef struct arrange_routes
{
    int *start;
    arrange_point *points;
} arrange_routes;

/* Routes every wire that skips a column of boxes through a lane in that
 * column. Routes depend only on the graph and the positions, so callers
 * recompute them rather than store them. `routes` is reused across calls. */
void arrange_route(const arrange_graph *g, const arrange_point *pos, arrange_routes *routes);
void arrange_routes_free(arrange_routes *routes);

arrange_score arrange_measure(const arrange_graph *g, const arrange_point *pos);

/* The single scalar the layout minimises. */
float arrange_cost(arrange_score score);

#endif
