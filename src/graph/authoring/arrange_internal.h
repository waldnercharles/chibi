#ifndef CHIBI_ARRANGE_INTERNAL_H
#define CHIBI_ARRANGE_INTERNAL_H

#include "graph/authoring/arrange.h"

/* Shared by the arrange units: arrange.c places boxes with the layered
 * solver, arrange_route.c routes wires through its lanes and the tracks of
 * each gap, arrange_path.c shapes each wire, and arrange_score.c measures
 * the drawing. */

#define AR_WIRE_LANE 22.0f
#define AR_FREE 1.0e9f

typedef struct ar_key
{
    float key;
    int index;
} ar_key;

typedef struct ar_lnode
{
    int box;
    int rank;
    int slot;
    float y;
    float height;
    float seed;
    int priority;
    bool fixed;
    bool pinned; /* a box whose position the caller set; it keeps its y and order */
} ar_lnode;

typedef struct ar_ledge
{
    int from;
    int to;
    float from_anchor;
    float to_anchor;
    bool back; /* part of a wire drawn right to left */
    bool flow; /* part of an execution wire */
} ar_ledge;

typedef struct ar_ctx
{
    ar_lnode *nodes;
    ar_ledge *edges;
    int *flat;
    int *rank_start;
    int rank_count;
    int *out_start;
    int *out_edge;
    int *in_start;
    int *in_edge;
    int *rank_edge_start;
    int *rank_edge;
    int *scratch;
    float *values;
    arrange_point *pulls; /* (y, anchor) a neighbour pulls a node toward */
    int *best_order;
} ar_ctx;

/* Each wire's corridors, one per rank it passes, in rank order. Wires from one
 * output share a trunk: a wire reuses the lanes of the earlier wire from the
 * same output with the most lanes, for the ranks both pass. */
typedef struct ar_lanes
{
    int *start; /* per wire: into `lanes` */
    int *lanes;
    int *head;  /* per box node: its latest shareable wire, or -1 */
    int *next;  /* per wire: the previous shareable wire from its box, or -1 */
    float *out; /* per wire: the offset of the output pin it leaves */
} ar_lanes;

/* One wire's crossing of the gap right of column or rank `gap`: it arrives
 * level at `in` and leaves level at `out`, turning on `track` between them.
 * A pass that stays level has track -1. A pass that doglegs first turns on
 * the half track `jog_track` and runs level at `jog_y` to its own track. */
typedef struct ar_pass
{
    int gap;
    float in;
    float out;
    int track;
    bool jog;
    float jog_track;
    float jog_y;
} ar_pass;

/* arrange.c */
int ar_key_compare(const void *a, const void *b);
int ar_find(int *parent, int i);
void ar_union(int *parent, int a, int b);
void ar_ctx_free(ar_ctx *c);
void ar_lanes_init(ar_lanes *w, int box_count);
void ar_lanes_free(ar_lanes *w);
int ar_lane_count(const ar_lanes *w, int wire);
void ar_mark_flow(ar_ctx *c, int first, bool flow);
bool ar_same_level(float a, float b);
void ar_ctx_link(ar_ctx *c, ar_lanes *w, int src, int dst, float src_anchor, float dst_anchor, bool back);
void ar_ctx_solve(ar_ctx *c);

/* arrange_route.c */
arrange_point ar_link_start(const arrange_graph *g, const arrange_point *pos, const arrange_link *l);
arrange_point ar_link_end(const arrange_point *pos, const arrange_link *l);
void ar_assign_tracks(ar_pass *passes, int count, int *gap_tracks);
float ar_gap_width(arrange_route_style style, int tracks);

/* arrange_path.c */
bool ar_tracked(arrange_route_style style);

#endif
