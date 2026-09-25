#ifndef CHIBI_ARRANGE_SCORE_H
#define CHIBI_ARRANGE_SCORE_H

#include "graph/authoring/arrange.h"

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
