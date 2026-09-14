#ifndef CHIBI_ARRANGE_H
#define CHIBI_ARRANGE_H

#include "support/math.h"

#include <stdbool.h>

/* Layered auto-arrange for the node canvas.
 *
 * This is pure geometry: no ImGui, no graph types, no editor state. The caller
 * measures its nodes, describes its wires, and gets one position per node
 * back, so the layout quality is measurable in a headless test. */

/* A measured node box. The pin rows are described rather than listed because
 * the canvas lays every pin out on the same pitch: input row i sits at
 * (x, y + title_height + pad + (i + 0.5) * row_height) and output row i at the
 * same height on x + width. */
typedef struct arrange_box
{
    float width;
    float height;
    float title_height;
    float row_height;
    float pad;
} arrange_box;

/* One wire, drawn from an output row on `from` to an input row on `to`. */
typedef struct arrange_link
{
    int from;
    int to;
    int from_pin;
    int to_pin;
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

/* Readability defects of a placement, counted on the drawn cubic curves. */
typedef struct arrange_score
{
    int wire_crossings; /* wire pairs whose curves intersect */
    int wire_node_hits; /* (wire, unrelated node) collisions */
    int node_overlaps;  /* node pairs whose boxes overlap */
    float wire_length;
} arrange_score;

/* Arranges the graph into layered columns with the wire crossings and
 * wire-node collisions driven down. `pos` is in-out: on entry it holds the
 * authored positions, which seed the within-column order so the result stays
 * recognisable; on exit it holds one position per box. */
void arrange_layout(const arrange_graph *g, arrange_point *pos);

arrange_score arrange_measure(const arrange_graph *g, const arrange_point *pos);

/* The single scalar the layout minimises. */
float arrange_cost(arrange_score score);

#endif
