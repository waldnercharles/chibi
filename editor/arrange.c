/* editor/arrange.c -- layered auto-arrange (ADR 0015).
 *
 * A Sugiyama-style pipeline, run once per weakly connected component:
 *
 *   1. break cycles      depth-first search; back edges are laid out reversed
 *   2. rank              longest path, then pull nodes right onto their
 *                        earliest consumer so pure data nodes sit beside it
 *   3. corridors         a wire spanning several columns gets one placeholder
 *                        per intermediate column, so it reserves vertical
 *                        space instead of running through a node
 *   4. order             median sweeps plus adjacent transposition, keeping
 *                        the best port-aware crossing count seen
 *   5. coordinates       priority method: align each node's pin with its
 *                        neighbour's pin, corridors first so long wires
 *                        stay straight
 *   6. refine            bounded local search on the real cubic curves, which
 *                        is the only stage that sees the drawn geometry
 *
 * Stages 1 to 5 optimise a proxy (crossings between column orders). Stage 6
 * optimises what the user sees. Components are stacked with a gap, so wires
 * from one component cannot tangle with another. */

#include "arrange.h"

#include "../core/array.h" // IWYU pragma: keep (apush/acount/asetlen/afree)
#include "../core/math.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

/* Separation. */
#define AR_RANK_GAP 90.0f
#define AR_NODE_GAP 36.0f
#define AR_COMPONENT_GAP 120.0f
#define AR_WIRE_LANE 22.0f /* vertical space a through-wire corridor claims */

/* Clearance kept around a node that has left its column for the gutter. */
#define AR_GUTTER_MARGIN 24.0f

/* Wire geometry. Mirrors the canvas renderer's control points at zoom 1. */
#define AR_BEND_MIN 40.0f
#define AR_CURVE_SAMPLES 12

/* Cost weights. A wire crossing a node body reads worse than two wires
 * crossing each other, and both dominate wire length, which only breaks ties
 * between otherwise equally readable placements. */
#define AR_COST_CROSS 1.0f
#define AR_COST_HIT 4.0f
#define AR_COST_OVERLAP 40.0f
#define AR_COST_LENGTH 0.0008f

/* Two wires that leave one node, or arrive at one node, are ordered by that
 * node's pin stack. A crossing between them is therefore a pure ordering
 * mistake, and it is drawn right beside the shared node where the curves are
 * still near-parallel, so it reads as two wires running over each other. A
 * crossing between unrelated wires meets at an angle mid-span and reads as a
 * crossing. Weighting the first kind higher makes the ordering stage keep a
 * node's fan-out in pin order rather than treating the two as equivalent.
 * Two is the smallest weight that breaks the tie; higher values start trading
 * unrelated crossings for wire-node collisions without reading better. */
#define AR_FAN_CROSS_WEIGHT 2

/* Iteration bounds. Every stage is bounded so a dense graph costs a slow
 * frame, never a frozen one. */
#define AR_ORDER_SWEEPS 8
#define AR_TRANSPOSE_PASSES 4
#define AR_COORD_SWEEPS 8
#define AR_REFINE_PASSES 3
#define AR_TIGHTEN_PASSES 8
#define AR_GUTTER_STEPS 4      /* probes each way, per axis */
#define AR_GUTTER_Y_STEP 24.0f /* vertical probe pitch when leaving a column */

/* Refinement measures real curves, so one probe costs roughly a wire count
 * plus a node count. Graphs up to a few hundred nodes refine fully; past that
 * the budget runs out and the proxy stages' result stands. That keeps a huge
 * graph to a slow frame instead of a frozen one. */
#define AR_REFINE_BUDGET 1500000

/* The gutter search is an order of magnitude dearer per box than the vertical
 * one, because it probes two axes. It gets its own budget and runs after the
 * vertical passes have settled, so the two cannot starve each other. */
#define AR_GUTTER_BUDGET 900000

#define AR_FREE 1.0e9f /* stands for "no obstacle in this direction" */
#define AR_CORRIDOR_PRIORITY 1000000

/* --- wire geometry -------------------------------------------------------- */

typedef struct ar_curve
{
    arrange_point p0;
    arrange_point p1;
    arrange_point p2;
    arrange_point p3;
} ar_curve;

static float ar_pin_y(const arrange_box *b, int row)
{
    return b->title_height + b->pad + ((float)row + 0.5f) * b->row_height;
}

static ar_curve ar_curve_of(const arrange_graph *g, const arrange_point *pos, const arrange_link *l)
{
    const arrange_box *a = &g->boxes[l->from];
    const arrange_box *b = &g->boxes[l->to];
    ar_curve c;
    c.p0 = (arrange_point){ pos[l->from].x + a->width,
                            pos[l->from].y + ar_pin_y(a, l->from_pin) };
    c.p3 = (arrange_point){ pos[l->to].x, pos[l->to].y + ar_pin_y(b, l->to_pin) };
    float bend = math_max(AR_BEND_MIN, math_abs(c.p3.x - c.p0.x) * 0.5f);
    c.p1 = (arrange_point){ c.p0.x + bend, c.p0.y };
    c.p2 = (arrange_point){ c.p3.x - bend, c.p3.y };
    return c;
}

static arrange_point ar_curve_at(const ar_curve *c, float t)
{
    float u = 1.0f - t;
    float a = u * u * u;
    float b = 3.0f * u * u * t;
    float d = 3.0f * u * t * t;
    float e = t * t * t;
    return (arrange_point){ a * c->p0.x + b * c->p1.x + d * c->p2.x + e * c->p3.x,
                            a * c->p0.y + b * c->p1.y + d * c->p2.y + e * c->p3.y };
}

/* The control points bound the curve, so this rejects distant pairs cheaply. */
static void ar_curve_bounds(const ar_curve *c, arrange_point *min, arrange_point *max)
{
    *min = vec2_min(vec2_min(c->p0, c->p1), vec2_min(c->p2, c->p3));
    *max = vec2_max(vec2_max(c->p0, c->p1), vec2_max(c->p2, c->p3));
}

static bool ar_boxes_overlap(arrange_point a_min, arrange_point a_max, arrange_point b_min, arrange_point b_max)
{
    return a_min.x <= b_max.x && a_max.x >= b_min.x && a_min.y <= b_max.y &&
           a_max.y >= b_min.y;
}

static float ar_cross2(arrange_point a, arrange_point b, arrange_point c)
{
    return vec2_cross(vec2_sub(b, a), vec2_sub(c, a));
}

static bool ar_segments_cross(arrange_point a, arrange_point b, arrange_point c, arrange_point d)
{
    float ab_c = ar_cross2(a, b, c);
    float ab_d = ar_cross2(a, b, d);
    float cd_a = ar_cross2(c, d, a);
    float cd_b = ar_cross2(c, d, b);
    return ((ab_c > 0.0f && ab_d < 0.0f) || (ab_c < 0.0f && ab_d > 0.0f)) &&
           ((cd_a > 0.0f && cd_b < 0.0f) || (cd_a < 0.0f && cd_b > 0.0f));
}

static bool ar_curves_cross(const ar_curve *a, const ar_curve *b)
{
    arrange_point a_min, a_max, b_min, b_max;
    ar_curve_bounds(a, &a_min, &a_max);
    ar_curve_bounds(b, &b_min, &b_max);
    if (!ar_boxes_overlap(a_min, a_max, b_min, b_max)) {
        return false;
    }
    arrange_point pa = a->p0;
    for (int i = 1; i <= AR_CURVE_SAMPLES; i++) {
        arrange_point na = ar_curve_at(a, (float)i / AR_CURVE_SAMPLES);
        arrange_point pb = b->p0;
        for (int j = 1; j <= AR_CURVE_SAMPLES; j++) {
            arrange_point nb = ar_curve_at(b, (float)j / AR_CURVE_SAMPLES);
            if (ar_segments_cross(pa, na, pb, nb)) {
                return true;
            }
            pb = nb;
        }
        pa = na;
    }
    return false;
}

/* Clips the parameter interval against one axis slab. */
static bool ar_clip_slab(float origin, float delta, float lo, float hi, float *t0, float *t1)
{
    if (math_abs(delta) < 1.0e-6f) {
        return origin >= lo && origin <= hi;
    }
    float a = (lo - origin) / delta;
    float b = (hi - origin) / delta;
    if (a > b) {
        float swap = a;
        a = b;
        b = swap;
    }
    if (a > *t0) {
        *t0 = a;
    }
    if (b < *t1) {
        *t1 = b;
    }
    return *t0 <= *t1;
}

static bool ar_segment_hits_box(arrange_point a, arrange_point b, arrange_point min, arrange_point max)
{
    float t0 = 0.0f;
    float t1 = 1.0f;
    return ar_clip_slab(a.x, b.x - a.x, min.x, max.x, &t0, &t1) &&
           ar_clip_slab(a.y, b.y - a.y, min.y, max.y, &t0, &t1);
}

static bool ar_curve_hits_box(const ar_curve *c, arrange_point min, arrange_point max)
{
    arrange_point c_min, c_max;
    ar_curve_bounds(c, &c_min, &c_max);
    if (!ar_boxes_overlap(c_min, c_max, min, max)) {
        return false;
    }
    arrange_point prev = c->p0;
    for (int i = 1; i <= AR_CURVE_SAMPLES; i++) {
        arrange_point next = ar_curve_at(c, (float)i / AR_CURVE_SAMPLES);
        if (ar_segment_hits_box(prev, next, min, max)) {
            return true;
        }
        prev = next;
    }
    return false;
}

static float ar_curve_length(const ar_curve *c)
{
    float length = 0.0f;
    arrange_point prev = c->p0;
    for (int i = 1; i <= AR_CURVE_SAMPLES; i++) {
        arrange_point next = ar_curve_at(c, (float)i / AR_CURVE_SAMPLES);
        float dx = next.x - prev.x;
        float dy = next.y - prev.y;
        length += sqrtf(dx * dx + dy * dy);
        prev = next;
    }
    return length;
}

/* Two wires leaving the same pin, or arriving at the same pin, meet there
 * by construction. That shared point is not a crossing to remove. */
static bool ar_share_pin(const arrange_link *a, const arrange_link *b)
{
    if (a->from == b->from && a->from_pin == b->from_pin) {
        return true;
    }
    return a->to == b->to && a->to_pin == b->to_pin;
}

static bool ar_link_touches(const arrange_link *l, int box)
{
    return l->from == box || l->to == box;
}

static void ar_box_bounds(
    const arrange_graph *g,
    const arrange_point *pos,
    int box,
    arrange_point *min,
    arrange_point *max
)
{
    *min = pos[box];
    *max = (arrange_point){ pos[box].x + g->boxes[box].width,
                            pos[box].y + g->boxes[box].height };
}

/* --- scoring ------------------------------------------------------------- */

float arrange_cost(arrange_score score)
{
    return AR_COST_CROSS * (float)score.wire_crossings +
           AR_COST_HIT * (float)score.wire_node_hits +
           AR_COST_OVERLAP * (float)score.node_overlaps +
           AR_COST_LENGTH * score.wire_length;
}

arrange_score arrange_measure(const arrange_graph *g, const arrange_point *pos)
{
    arrange_score score = { 0, 0, 0, 0.0f };
    ar_curve *curves = NULL;
    asetlen(curves, g->link_count);
    for (int i = 0; i < g->link_count; i++) {
        curves[i] = ar_curve_of(g, pos, &g->links[i]);
        score.wire_length += ar_curve_length(&curves[i]);
    }
    for (int i = 0; i < g->link_count; i++) {
        for (int j = i + 1; j < g->link_count; j++) {
            if (ar_share_pin(&g->links[i], &g->links[j])) {
                continue;
            }
            if (ar_curves_cross(&curves[i], &curves[j])) {
                score.wire_crossings++;
            }
        }
        for (int b = 0; b < g->box_count; b++) {
            if (ar_link_touches(&g->links[i], b)) {
                continue;
            }
            arrange_point min, max;
            ar_box_bounds(g, pos, b, &min, &max);
            if (ar_curve_hits_box(&curves[i], min, max)) {
                score.wire_node_hits++;
            }
        }
    }
    for (int a = 0; a < g->box_count; a++) {
        arrange_point a_min, a_max;
        ar_box_bounds(g, pos, a, &a_min, &a_max);
        for (int b = a + 1; b < g->box_count; b++) {
            arrange_point b_min, b_max;
            ar_box_bounds(g, pos, b, &b_min, &b_max);
            if (ar_boxes_overlap(a_min, a_max, b_min, b_max)) {
                score.node_overlaps++;
            }
        }
    }
    afree(curves);
    return score;
}

/* The defect terms one box's position can change: its own wires' crossings,
 * wires that could run through it, and its own overlaps. Comparing this
 * before and after a candidate move is exact, because the term set depends
 * on the box rather than on the geometry. Wire length is deliberately
 * excluded: a move that only shortens a wire is not worth disturbing the
 * column grid for, so the horizontal search compares these terms alone. */
static float ar_box_defects(const arrange_graph *g, const arrange_point *pos, int box)
{
    float cost = 0.0f;
    arrange_point box_min, box_max;
    ar_box_bounds(g, pos, box, &box_min, &box_max);
    for (int b = 0; b < g->box_count; b++) {
        if (b == box) {
            continue;
        }
        arrange_point min, max;
        ar_box_bounds(g, pos, b, &min, &max);
        if (ar_boxes_overlap(box_min, box_max, min, max)) {
            cost += AR_COST_OVERLAP;
        }
    }
    for (int i = 0; i < g->link_count; i++) {
        const arrange_link *a = &g->links[i];
        ar_curve ca = ar_curve_of(g, pos, a);
        if (!ar_link_touches(a, box)) {
            if (ar_curve_hits_box(&ca, box_min, box_max)) {
                cost += AR_COST_HIT;
            }
            continue;
        }
        for (int b = 0; b < g->box_count; b++) {
            if (ar_link_touches(a, b)) {
                continue;
            }
            arrange_point min, max;
            ar_box_bounds(g, pos, b, &min, &max);
            if (ar_curve_hits_box(&ca, min, max)) {
                cost += AR_COST_HIT;
            }
        }
        for (int j = 0; j < g->link_count; j++) {
            if (j == i) {
                continue;
            }
            const arrange_link *b = &g->links[j];
            /* A pair of incident wires belongs to this box twice; keep one. */
            if (ar_link_touches(b, box) && j < i) {
                continue;
            }
            if (ar_share_pin(a, b)) {
                continue;
            }
            ar_curve cb = ar_curve_of(g, pos, b);
            if (ar_curves_cross(&ca, &cb)) {
                cost += AR_COST_CROSS;
            }
        }
    }
    return cost;
}

/* Total length of the wires this box can move. */
static float ar_box_wire_length(const arrange_graph *g, const arrange_point *pos, int box)
{
    float length = 0.0f;
    for (int i = 0; i < g->link_count; i++) {
        if (!ar_link_touches(&g->links[i], box)) {
            continue;
        }
        ar_curve c = ar_curve_of(g, pos, &g->links[i]);
        length += ar_curve_length(&c);
    }
    return length;
}

static float ar_box_cost(const arrange_graph *g, const arrange_point *pos, int box)
{
    return ar_box_defects(g, pos, box) +
           AR_COST_LENGTH * ar_box_wire_length(g, pos, box);
}

/* --- layout state -------------------------------------------------------- */

/* One layout node: either a real box or a corridor standing in for a wire
 * passing through a column. */
typedef struct ar_lnode
{
    int box; /* -1 for a corridor */
    int rank;
    int slot; /* index into ctx.flat, so the column order is readable */
    float y;
    float height;
    float seed;
    int priority;
    bool fixed;
} ar_lnode;

/* One column-to-column wire segment. The anchors are y offsets inside the
 * endpoints, so aligning them aligns the drawn pins rather than the boxes. */
typedef struct ar_ledge
{
    int from;
    int to;
    float from_anchor;
    float to_anchor;
} ar_ledge;

typedef struct ar_ctx
{
    ar_lnode *nodes;
    ar_ledge *edges;
    int *flat;       /* node indices grouped by rank */
    int *rank_start; /* rank_count + 1 offsets into flat */
    int rank_count;
    int *out_start; /* per-node edge ranges (edges never move) */
    int *out_edge;
    int *in_start;
    int *in_edge;
    int *rank_edge_start; /* per-rank edge ranges, for crossing counts */
    int *rank_edge;
    int *scratch;    /* reused index list */
    float *values;   /* reused float list */
    int *best_order; /* best column order seen */
} ar_ctx;

static void ar_ctx_free(ar_ctx *c)
{
    afree(c->nodes);
    afree(c->edges);
    afree(c->flat);
    afree(c->rank_start);
    afree(c->out_start);
    afree(c->out_edge);
    afree(c->in_start);
    afree(c->in_edge);
    afree(c->rank_edge_start);
    afree(c->rank_edge);
    afree(c->scratch);
    afree(c->values);
    afree(c->best_order);
}

/* Fills a compressed adjacency: start[i]..start[i+1] indexes `list`. */
static void ar_build_adjacency(int node_count, const int *key, int key_count, int **start, int **list)
{
    asetlen(*start, node_count + 1);
    for (int i = 0; i <= node_count; i++) {
        (*start)[i] = 0;
    }
    for (int i = 0; i < key_count; i++) {
        (*start)[key[i] + 1]++;
    }
    for (int i = 0; i < node_count; i++) {
        (*start)[i + 1] += (*start)[i];
    }
    asetlen(*list, key_count);
    int *at = NULL;
    asetlen(at, node_count);
    for (int i = 0; i < node_count; i++) {
        at[i] = (*start)[i];
    }
    for (int i = 0; i < key_count; i++) {
        (*list)[at[key[i]]++] = i;
    }
    afree(at);
}

static void ar_index_edges(ar_ctx *c)
{
    int node_count = acount(c->nodes);
    int edge_count = acount(c->edges);
    int *from_key = NULL;
    int *to_key = NULL;
    int *rank_key = NULL;
    asetlen(from_key, edge_count);
    asetlen(to_key, edge_count);
    asetlen(rank_key, edge_count);
    for (int i = 0; i < edge_count; i++) {
        from_key[i] = c->edges[i].from;
        to_key[i] = c->edges[i].to;
        rank_key[i] = c->nodes[c->edges[i].from].rank;
    }
    ar_build_adjacency(node_count, from_key, edge_count, &c->out_start, &c->out_edge);
    ar_build_adjacency(node_count, to_key, edge_count, &c->in_start, &c->in_edge);
    ar_build_adjacency(c->rank_count, rank_key, edge_count, &c->rank_edge_start, &c->rank_edge);
    afree(from_key);
    afree(to_key);
    afree(rank_key);
}

/* --- ordering ------------------------------------------------------------ */

/* A node's order key, offset by the port fraction so two wires into different
 * rows of the same node are counted in the order they are drawn. */
static float ar_port_key(const ar_ctx *c, int node, float anchor)
{
    const ar_lnode *n = &c->nodes[node];
    float height = n->height > 0.0f ? n->height : 1.0f;
    return (float)n->slot + 0.05f + 0.9f * (anchor / height);
}

static int ar_cross_weight(const ar_ledge *a, const ar_ledge *b)
{
    return a->from == b->from || a->to == b->to ? AR_FAN_CROSS_WEIGHT : 1;
}

static int ar_rank_crossings(const ar_ctx *c, int rank)
{
    if (rank < 0 || rank + 1 >= c->rank_count) {
        return 0;
    }
    int first = c->rank_edge_start[rank];
    int last = c->rank_edge_start[rank + 1];
    int score = 0;
    for (int i = first; i < last; i++) {
        const ar_ledge *a = &c->edges[c->rank_edge[i]];
        float a_up = ar_port_key(c, a->from, a->from_anchor);
        float a_down = ar_port_key(c, a->to, a->to_anchor);
        for (int j = i + 1; j < last; j++) {
            const ar_ledge *b = &c->edges[c->rank_edge[j]];
            float b_up = ar_port_key(c, b->from, b->from_anchor);
            float b_down = ar_port_key(c, b->to, b->to_anchor);
            if ((a_up - b_up) * (a_down - b_down) < 0.0f) {
                score += ar_cross_weight(a, b);
            }
        }
    }
    return score;
}

static int ar_total_crossings(const ar_ctx *c)
{
    int score = 0;
    for (int rank = 0; rank + 1 < c->rank_count; rank++) {
        score += ar_rank_crossings(c, rank);
    }
    return score;
}

static void ar_renumber_slots(ar_ctx *c)
{
    for (int i = 0; i < acount(c->flat); i++) {
        c->nodes[c->flat[i]].slot = i;
    }
}

/* Sorts one column by `values`, indexed by node. Insertion sort keeps equal
 * keys in their current order, which is what makes repeated sweeps stable. */
static void ar_sort_rank(ar_ctx *c, int rank, const float *values)
{
    int first = c->rank_start[rank];
    int last = c->rank_start[rank + 1];
    for (int i = first + 1; i < last; i++) {
        int node = c->flat[i];
        int j = i;
        while (j > first && values[c->flat[j - 1]] > values[node]) {
            c->flat[j] = c->flat[j - 1];
            j--;
        }
        c->flat[j] = node;
    }
    ar_renumber_slots(c);
}

static float ar_median(float *values, int count)
{
    for (int i = 1; i < count; i++) {
        float v = values[i];
        int j = i;
        while (j > 0 && values[j - 1] > v) {
            values[j] = values[j - 1];
            j--;
        }
        values[j] = v;
    }
    if (count & 1) {
        return values[count / 2];
    }
    return (values[count / 2 - 1] + values[count / 2]) * 0.5f;
}

/* Median of the neighbour port keys on the fixed side of the sweep. */
static bool ar_neighbor_median(ar_ctx *c, int node, bool forward, float *out)
{
    aclear(c->values);
    if (forward) {
        for (int i = c->in_start[node]; i < c->in_start[node + 1]; i++) {
            const ar_ledge *e = &c->edges[c->in_edge[i]];
            apush(c->values, ar_port_key(c, e->from, e->from_anchor));
        }
    } else {
        for (int i = c->out_start[node]; i < c->out_start[node + 1]; i++) {
            const ar_ledge *e = &c->edges[c->out_edge[i]];
            apush(c->values, ar_port_key(c, e->to, e->to_anchor));
        }
    }
    if (!acount(c->values)) {
        return false;
    }
    *out = ar_median(c->values, acount(c->values));
    return true;
}

static void ar_order_sweep(ar_ctx *c, bool forward, float *keys)
{
    int first = forward ? 1 : c->rank_count - 2;
    int last = forward ? c->rank_count : -1;
    int step = forward ? 1 : -1;
    for (int rank = first; rank != last; rank += step) {
        for (int i = c->rank_start[rank]; i < c->rank_start[rank + 1]; i++) {
            int node = c->flat[i];
            float median = 0.0f;
            keys[node] = ar_neighbor_median(c, node, forward, &median)
                           ? median
                           : (float)c->nodes[node].slot;
        }
        ar_sort_rank(c, rank, keys);
    }
}

/* Crossings contributed by two same-column nodes when `upper` sits above
 * `lower`. Only these pairs change when the two swap, so a candidate swap
 * costs their two degrees instead of a whole-column recount. */
static int ar_pair_crossings(const ar_ctx *c, int upper, int lower)
{
    int score = 0;
    for (int i = c->in_start[upper]; i < c->in_start[upper + 1]; i++) {
        const ar_ledge *a = &c->edges[c->in_edge[i]];
        float key = ar_port_key(c, a->from, a->from_anchor);
        for (int j = c->in_start[lower]; j < c->in_start[lower + 1]; j++) {
            const ar_ledge *b = &c->edges[c->in_edge[j]];
            if (key > ar_port_key(c, b->from, b->from_anchor)) {
                score += ar_cross_weight(a, b);
            }
        }
    }
    for (int i = c->out_start[upper]; i < c->out_start[upper + 1]; i++) {
        const ar_ledge *a = &c->edges[c->out_edge[i]];
        float key = ar_port_key(c, a->to, a->to_anchor);
        for (int j = c->out_start[lower]; j < c->out_start[lower + 1]; j++) {
            const ar_ledge *b = &c->edges[c->out_edge[j]];
            if (key > ar_port_key(c, b->to, b->to_anchor)) {
                score += ar_cross_weight(a, b);
            }
        }
    }
    return score;
}

/* Swaps adjacent pairs while that removes crossings. Median sweeps cannot
 * see a swap that only helps locally; this can. */
static void ar_transpose(ar_ctx *c)
{
    for (int pass = 0; pass < AR_TRANSPOSE_PASSES; pass++) {
        bool improved = false;
        for (int rank = 0; rank < c->rank_count; rank++) {
            int last = c->rank_start[rank + 1];
            for (int i = c->rank_start[rank]; i + 1 < last; i++) {
                int a = c->flat[i];
                int b = c->flat[i + 1];
                if (ar_pair_crossings(c, b, a) >= ar_pair_crossings(c, a, b)) {
                    continue;
                }
                c->flat[i] = b;
                c->flat[i + 1] = a;
                c->nodes[b].slot = i;
                c->nodes[a].slot = i + 1;
                improved = true;
            }
        }
        if (!improved) {
            break;
        }
    }
}

static void ar_order_ranks(ar_ctx *c)
{
    int node_count = acount(c->nodes);
    float *keys = NULL;
    asetlen(keys, node_count);
    for (int rank = 0; rank < c->rank_count; rank++) {
        for (int i = c->rank_start[rank]; i < c->rank_start[rank + 1]; i++) {
            keys[c->flat[i]] = c->nodes[c->flat[i]].seed;
        }
        ar_sort_rank(c, rank, keys);
    }
    asetlen(c->best_order, node_count);
    for (int i = 0; i < node_count; i++) {
        c->best_order[i] = c->flat[i];
    }
    int best = ar_total_crossings(c);
    for (int sweep = 0; sweep < AR_ORDER_SWEEPS && best > 0; sweep++) {
        ar_order_sweep(c, !(sweep & 1), keys);
        ar_transpose(c);
        int crossings = ar_total_crossings(c);
        if (crossings >= best) {
            continue;
        }
        best = crossings;
        for (int i = 0; i < node_count; i++) {
            c->best_order[i] = c->flat[i];
        }
    }
    for (int i = 0; i < node_count; i++) {
        c->flat[i] = c->best_order[i];
    }
    ar_renumber_slots(c);
    afree(keys);
}

/* --- vertical coordinates ------------------------------------------------ */

static void ar_pack_rank(ar_ctx *c, int rank)
{
    int first = c->rank_start[rank];
    int last = c->rank_start[rank + 1];
    float total = 0.0f;
    for (int i = first; i < last; i++) {
        total += c->nodes[c->flat[i]].height + AR_NODE_GAP;
    }
    float y = -(total - AR_NODE_GAP) * 0.5f;
    for (int i = first; i < last; i++) {
        ar_lnode *n = &c->nodes[c->flat[i]];
        n->y = y;
        y += n->height + AR_NODE_GAP;
    }
}

/* How far `node` can travel before an already placed node blocks it. Nodes
 * in between are movable and are pushed along, so their slack counts. */
static float ar_slack(const ar_ctx *c, int node, int dir)
{
    int rank = c->nodes[node].rank;
    int first = c->rank_start[rank];
    int last = c->rank_start[rank + 1];
    float slack = 0.0f;
    for (int i = c->nodes[node].slot;; i += dir) {
        int next = i + dir;
        if (next < first || next >= last) {
            return AR_FREE;
        }
        const ar_lnode *a = &c->nodes[c->flat[i]];
        const ar_lnode *b = &c->nodes[c->flat[next]];
        float gap = dir > 0 ? b->y - (a->y + a->height) : a->y - (b->y + b->height);
        slack += gap - AR_NODE_GAP;
        if (b->fixed) {
            return math_max(slack, 0.0f);
        }
    }
}

static void ar_move(ar_ctx *c, int node, float delta)
{
    if (math_abs(delta) < 0.5f) {
        return;
    }
    int rank = c->nodes[node].rank;
    int first = c->rank_start[rank];
    int last = c->rank_start[rank + 1];
    int dir = delta > 0.0f ? 1 : -1;
    c->nodes[node].y += delta;
    for (int i = c->nodes[node].slot + dir; i >= first && i < last; i += dir) {
        ar_lnode *moved = &c->nodes[c->flat[i]];
        const ar_lnode *prev = &c->nodes[c->flat[i - dir]];
        float limit = dir > 0 ? prev->y + prev->height + AR_NODE_GAP
                              : prev->y - AR_NODE_GAP - moved->height;
        if (dir > 0 ? moved->y >= limit : moved->y <= limit) {
            break;
        }
        moved->y = limit;
    }
}

/* Median of the box positions that would put this node's pin level with its
 * neighbours' pins on the fixed side of the sweep. Aligning pins rather than
 * box centres is what makes a wire between two connected pins run flat. */
static bool ar_neighbor_median_y(ar_ctx *c, int node, bool forward, float *out)
{
    aclear(c->values);
    if (forward) {
        for (int i = c->in_start[node]; i < c->in_start[node + 1]; i++) {
            const ar_ledge *e = &c->edges[c->in_edge[i]];
            apush(c->values, c->nodes[e->from].y + e->from_anchor - e->to_anchor);
        }
    } else {
        for (int i = c->out_start[node]; i < c->out_start[node + 1]; i++) {
            const ar_ledge *e = &c->edges[c->out_edge[i]];
            apush(c->values, c->nodes[e->to].y + e->to_anchor - e->from_anchor);
        }
    }
    if (!acount(c->values)) {
        return false;
    }
    *out = ar_median(c->values, acount(c->values));
    return true;
}

/* Places one column against the already placed neighbouring column.
 * Corridors go first, so a long wire is straightened before real nodes
 * claim the space it needs. */
static void ar_align_rank(ar_ctx *c, int rank, bool forward)
{
    int first = c->rank_start[rank];
    int last = c->rank_start[rank + 1];
    aclear(c->scratch);
    for (int i = first; i < last; i++) {
        c->nodes[c->flat[i]].fixed = false;
        apush(c->scratch, c->flat[i]);
    }
    int count = acount(c->scratch);
    for (int i = 1; i < count; i++) {
        int node = c->scratch[i];
        int j = i;
        while (j > 0 && c->nodes[c->scratch[j - 1]].priority < c->nodes[node].priority) {
            c->scratch[j] = c->scratch[j - 1];
            j--;
        }
        c->scratch[j] = node;
    }
    for (int i = 0; i < count; i++) {
        int node = c->scratch[i];
        float want = 0.0f;
        if (ar_neighbor_median_y(c, node, forward, &want)) {
            float delta = want - c->nodes[node].y;
            float slack = ar_slack(c, node, delta > 0.0f ? 1 : -1);
            if (math_abs(delta) > slack) {
                delta = delta > 0.0f ? slack : -slack;
            }
            ar_move(c, node, delta);
        }
        c->nodes[node].fixed = true;
    }
}

static void ar_assign_y(ar_ctx *c)
{
    for (int rank = 0; rank < c->rank_count; rank++) {
        ar_pack_rank(c, rank);
    }
    for (int sweep = 0; sweep < AR_COORD_SWEEPS; sweep++) {
        bool forward = !(sweep & 1);
        int first = forward ? 1 : c->rank_count - 2;
        int last = forward ? c->rank_count : -1;
        int step = forward ? 1 : -1;
        for (int rank = first; rank != last; rank += step) {
            ar_align_rank(c, rank, forward);
        }
    }
}

/* --- bounded refinement on the drawn curves ------------------------------ */

/* Horizontal room a box has without turning any of its own wires backwards:
 * it must stay right of every producer and left of every consumer. A node with
 * no consumers (a leaf) keeps its column as the right limit, and one with no
 * producers keeps it as the left limit, so neither drifts off into space.
 *
 * Columns exist to make the graph readable, not because a node's x has to
 * equal its rank's x. A leaf with slack can sit in the gutter between two
 * columns, which vacates the column band for wires that need to pass through
 * it - the case a strict column grid cannot express. */
static void ar_x_limits(const arrange_graph *g, const arrange_point *pos, int box, float *left, float *right)
{
    *left = -AR_FREE;
    *right = AR_FREE;
    for (int i = 0; i < g->link_count; i++) {
        const arrange_link *l = &g->links[i];
        if (l->to == box && l->from != box) {
            float limit = pos[l->from].x + g->boxes[l->from].width + AR_GUTTER_MARGIN;
            if (limit > *left) {
                *left = limit;
            }
        }
        if (l->from == box && l->to != box) {
            float limit = pos[l->to].x - g->boxes[box].width - AR_GUTTER_MARGIN;
            if (limit < *right) {
                *right = limit;
            }
        }
    }
    if (*left <= -AR_FREE) {
        *left = pos[box].x;
    }
    if (*right >= AR_FREE) {
        *right = pos[box].x;
    }
    if (*left > pos[box].x) {
        *left = pos[box].x;
    }
    if (*right < pos[box].x) {
        *right = pos[box].x;
    }
}

/* Lets one box leave its column for the gutter between columns, moving in both
 * axes at once, when that strictly removes defects.
 *
 * Both axes have to move together: pulling a leaf left often lands it on a
 * wide neighbour from the previous column, and the vertical room to clear that
 * neighbour only exists off-column. Searching one axis then the other finds
 * neither move, because each is blocked while the other is held still.
 *
 * The bar is a strict reduction in crossings and collisions, so the column
 * grid stands everywhere it is not buying something. Only boxes that currently
 * have a defect are searched, which keeps this off the hot path. */
static bool ar_try_gutter(const arrange_graph *g, arrange_point *pos, int box, long *budget)
{
    long probe = g->link_count + g->box_count;
    float base = ar_box_defects(g, pos, box);
    *budget -= probe;
    if (base <= 0.0f) {
        return false;
    }
    float left = 0.0f;
    float right = 0.0f;
    ar_x_limits(g, pos, box, &left, &right);
    arrange_point start = pos[box];
    arrange_point best = start;
    float best_defects = base;
    float best_length = ar_box_wire_length(g, pos, box);
    for (int xs = 0; xs <= AR_GUTTER_STEPS; xs++) {
        for (int dir = 0; dir < 2; dir++) {
            float t = (float)xs / AR_GUTTER_STEPS;
            float target = dir ? right : left;
            float x = start.x + (target - start.x) * t;
            for (int ys = -AR_GUTTER_STEPS; ys <= AR_GUTTER_STEPS; ys++) {
                float y = start.y + (float)ys * AR_GUTTER_Y_STEP;
                pos[box] = (arrange_point){ x, y };
                float defects = ar_box_defects(g, pos, box);
                *budget -= probe;
                bool fewer = defects < best_defects - 0.001f;
                /* Among positions that fix as much, take the shortest wires. */
                float length = ar_box_wire_length(g, pos, box);
                bool tied_shorter = defects < best_defects + 0.001f &&
                                    length < best_length - 1.0f;
                if (!fewer && !tied_shorter) {
                    continue;
                }
                best_defects = defects;
                best_length = length;
                best = (arrange_point){ x, y };
            }
        }
    }
    /* Leaving the column has to buy a defect, not merely a shorter wire. */
    if (best_defects >= base - 0.001f) {
        pos[box] = start;
        return false;
    }
    pos[box] = best;
    return best.x != start.x || best.y != start.y;
}

/* The proxy stages above optimise column orders. This stage is the only one
 * that reads the real cubic geometry: each node is nudged inside the slack
 * its column neighbours leave, and a nudge is kept only when the measured
 * cost of the wires and collisions it can affect drops. */
static void ar_refine(const arrange_graph *g, arrange_point *pos, const int *rank_of, int rank_count)
{
    static const float steps[] = { -64.0f, -32.0f, -16.0f, -8.0f,
                                   8.0f,   16.0f,  32.0f,  64.0f };
    long budget = AR_REFINE_BUDGET;
    long probe = g->link_count + g->box_count;
    int *column = NULL;
    for (int pass = 0; pass < AR_REFINE_PASSES && budget > 0; pass++) {
        bool improved = false;
        for (int rank = 0; rank < rank_count; rank++) {
            aclear(column);
            for (int b = 0; b < g->box_count; b++) {
                if (rank_of[b] == rank) {
                    apush(column, b);
                }
            }
            for (int i = 1; i < acount(column); i++) {
                int box = column[i];
                int j = i;
                while (j > 0 && pos[column[j - 1]].y > pos[box].y) {
                    column[j] = column[j - 1];
                    j--;
                }
                column[j] = box;
            }
            for (int i = 0; i < acount(column); i++) {
                int box = column[i];
                float up = AR_FREE;
                float down = AR_FREE;
                if (i > 0) {
                    int above = column[i - 1];
                    up = pos[box].y -
                         (pos[above].y + g->boxes[above].height + AR_NODE_GAP);
                }
                if (i + 1 < acount(column)) {
                    int below = column[i + 1];
                    down = pos[below].y -
                           (pos[box].y + g->boxes[box].height + AR_NODE_GAP);
                }
                if (budget <= 0) {
                    break;
                }
                float start = pos[box].y;
                float best = ar_box_cost(g, pos, box);
                budget -= probe;
                float chosen = 0.0f;
                for (int s = 0; s < (int)(sizeof steps / sizeof *steps); s++) {
                    float step = steps[s];
                    if (step < 0.0f && -step > up) {
                        continue;
                    }
                    if (step > 0.0f && step > down) {
                        continue;
                    }
                    pos[box].y = start + step;
                    float cost = ar_box_cost(g, pos, box);
                    budget -= probe;
                    if (cost >= best - 0.001f) {
                        continue;
                    }
                    best = cost;
                    chosen = step;
                }
                pos[box].y = start + chosen;
                if (chosen != 0.0f) {
                    improved = true;
                }
            }
        }
        if (!improved) {
            break;
        }
    }
    afree(column);

    /* Now let boxes escape their column where that removes a defect the
     * column grid cannot. */
    long gutter = AR_GUTTER_BUDGET;
    for (int pass = 0; pass < AR_REFINE_PASSES && gutter > 0; pass++) {
        bool improved = false;
        for (int box = 0; box < g->box_count && gutter > 0; box++) {
            if (ar_try_gutter(g, pos, box, &gutter)) {
                improved = true;
            }
        }
        if (!improved) {
            break;
        }
    }
}

/* --- ranking ------------------------------------------------------------- */

enum
{
    AR_WHITE,
    AR_GRAY,
    AR_BLACK
};

/* Marks the depth-first back edges. Removing them leaves a DAG, and the
 * wire is still drawn between its authored pins; only the column assignment
 * treats it as running the other way. */
static void ar_break_cycles(
    const arrange_graph *g,
    const int *links,
    const int *local,
    const int *start,
    const int *list,
    bool *reversed,
    bool *dropped,
    int count
)
{
    int *color = NULL;
    int *stack_node = NULL;
    int *stack_at = NULL;
    asetlen(color, count);
    for (int i = 0; i < count; i++) {
        color[i] = AR_WHITE;
    }
    for (int root = 0; root < count; root++) {
        if (color[root] != AR_WHITE) {
            continue;
        }
        color[root] = AR_GRAY;
        apush(stack_node, root);
        apush(stack_at, start[root]);
        while (acount(stack_node)) {
            int top = acount(stack_node) - 1;
            int node = stack_node[top];
            if (stack_at[top] >= start[node + 1]) {
                color[node] = AR_BLACK;
                (void)apop(stack_node);
                (void)apop(stack_at);
                continue;
            }
            int at = list[stack_at[top]++];
            const arrange_link *l = &g->links[links[at]];
            int to = local[l->to];
            if (local[l->from] == to) {
                dropped[at] = true;
                continue;
            }
            if (color[to] == AR_GRAY) {
                reversed[at] = true;
                continue;
            }
            if (color[to] == AR_BLACK) {
                continue;
            }
            color[to] = AR_GRAY;
            apush(stack_node, to);
            apush(stack_at, start[to]);
        }
    }
    afree(color);
    afree(stack_node);
    afree(stack_at);
}

/* Longest-path ranks, then one right-pulling pass so a node sits next to
 * its earliest consumer instead of in the leftmost column it is allowed.
 * That is what puts a pure data node beside the node it feeds. */
static void ar_rank_nodes(int count, const int *edge_src, const int *edge_dst, int edge_count, int *rank)
{
    int *key = NULL;
    int *start = NULL;
    int *list = NULL;
    asetlen(key, edge_count);
    for (int i = 0; i < edge_count; i++) {
        key[i] = edge_src[i];
    }
    ar_build_adjacency(count, key, edge_count, &start, &list);

    int *indegree = NULL;
    asetlen(indegree, count);
    for (int i = 0; i < count; i++) {
        indegree[i] = 0;
        rank[i] = 0;
    }
    for (int i = 0; i < edge_count; i++) {
        indegree[edge_dst[i]]++;
    }
    int *topo = NULL;
    for (int i = 0; i < count; i++) {
        if (!indegree[i]) {
            apush(topo, i);
        }
    }
    for (int at = 0; at < acount(topo); at++) {
        int node = topo[at];
        for (int i = start[node]; i < start[node + 1]; i++) {
            int to = edge_dst[list[i]];
            if (rank[to] < rank[node] + 1) {
                rank[to] = rank[node] + 1;
            }
            if (!--indegree[to]) {
                apush(topo, to);
            }
        }
    }
    assert(acount(topo) == count); /* back-edge removal left a DAG */

    for (int pass = 0; pass < AR_TIGHTEN_PASSES; pass++) {
        bool changed = false;
        for (int at = count - 1; at >= 0; at--) {
            int node = topo[at];
            if (start[node] == start[node + 1]) {
                continue;
            }
            int earliest = rank[edge_dst[list[start[node]]]];
            for (int i = start[node] + 1; i < start[node + 1]; i++) {
                int to_rank = rank[edge_dst[list[i]]];
                if (to_rank < earliest) {
                    earliest = to_rank;
                }
            }
            if (earliest - 1 <= rank[node]) {
                continue;
            }
            rank[node] = earliest - 1;
            changed = true;
        }
        if (!changed) {
            break;
        }
    }

    /* Compact away columns that ended up empty, so no gap is drawn for them. */
    int max_rank = 0;
    for (int i = 0; i < count; i++) {
        if (rank[i] > max_rank) {
            max_rank = rank[i];
        }
    }
    int *compact = NULL;
    asetlen(compact, max_rank + 1);
    for (int r = 0; r <= max_rank; r++) {
        compact[r] = 0;
    }
    for (int i = 0; i < count; i++) {
        compact[rank[i]] = 1;
    }
    int next = 0;
    for (int r = 0; r <= max_rank; r++) {
        int used = compact[r];
        compact[r] = next;
        next += used;
    }
    for (int i = 0; i < count; i++) {
        rank[i] = compact[rank[i]];
    }

    afree(compact);
    afree(topo);
    afree(indegree);
    afree(key);
    afree(start);
    afree(list);
}

/* --- one component ------------------------------------------------------- */

static void ar_layout_component(
    const arrange_graph *g,
    const int *boxes,
    const int *links,
    const int *local,
    arrange_point *pos,
    float *stack_y
)
{
    int count = acount(boxes);
    int link_count = acount(links);

    bool *reversed = NULL;
    bool *dropped = NULL;
    asetlen(reversed, link_count);
    asetlen(dropped, link_count);
    for (int i = 0; i < link_count; i++) {
        reversed[i] = false;
        dropped[i] = false;
    }
    int *key = NULL;
    int *start = NULL;
    int *list = NULL;
    asetlen(key, link_count);
    for (int i = 0; i < link_count; i++) {
        key[i] = local[g->links[links[i]].from];
    }
    ar_build_adjacency(count, key, link_count, &start, &list);
    ar_break_cycles(g, links, local, start, list, reversed, dropped, count);
    afree(key);
    afree(start);
    afree(list);

    int *edge_src = NULL;
    int *edge_dst = NULL;
    int *edge_link = NULL;
    for (int i = 0; i < link_count; i++) {
        if (dropped[i]) {
            continue;
        }
        int from = local[g->links[links[i]].from];
        int to = local[g->links[links[i]].to];
        apush(edge_src, reversed[i] ? to : from);
        apush(edge_dst, reversed[i] ? from : to);
        apush(edge_link, i);
    }
    int *rank = NULL;
    asetlen(rank, count);
    ar_rank_nodes(count, edge_src, edge_dst, acount(edge_src), rank);

    ar_ctx c = { 0 };
    for (int i = 0; i < count; i++) {
        const arrange_box *b = &g->boxes[boxes[i]];
        apush(
            c.nodes,
            (ar_lnode){ .box = boxes[i],
                        .rank = rank[i],
                        .height = b->height,
                        .seed = pos[boxes[i]].y + b->height * 0.5f }
        );
    }
    for (int i = 0; i < acount(edge_src); i++) {
        const arrange_link *l = &g->links[links[edge_link[i]]];
        float from_anchor = ar_pin_y(&g->boxes[l->from], l->from_pin);
        float to_anchor = ar_pin_y(&g->boxes[l->to], l->to_pin);
        int src = edge_src[i];
        int dst = edge_dst[i];
        float src_anchor = local[l->from] == src ? from_anchor : to_anchor;
        float dst_anchor = local[l->to] == dst ? to_anchor : from_anchor;
        int span = rank[dst] - rank[src];
        assert(span >= 1);
        int prev = src;
        float prev_anchor = src_anchor;
        for (int r = rank[src] + 1; r < rank[dst]; r++) {
            float t = (float)(r - rank[src]) / (float)span;
            int corridor = acount(c.nodes);
            apush(
                c.nodes,
                (ar_lnode){ .box = -1,
                            .rank = r,
                            .height = AR_WIRE_LANE,
                            .seed = c.nodes[src].seed +
                                    (c.nodes[dst].seed - c.nodes[src].seed) * t,
                            .priority = AR_CORRIDOR_PRIORITY }
            );
            apush(c.edges, (ar_ledge){ prev, corridor, prev_anchor, AR_WIRE_LANE * 0.5f });
            prev = corridor;
            prev_anchor = AR_WIRE_LANE * 0.5f;
        }
        apush(c.edges, (ar_ledge){ prev, dst, prev_anchor, dst_anchor });
    }

    int node_count = acount(c.nodes);
    c.rank_count = 0;
    for (int i = 0; i < node_count; i++) {
        if (c.nodes[i].rank + 1 > c.rank_count) {
            c.rank_count = c.nodes[i].rank + 1;
        }
    }
    asetlen(c.rank_start, c.rank_count + 1);
    for (int r = 0; r <= c.rank_count; r++) {
        c.rank_start[r] = 0;
    }
    for (int i = 0; i < node_count; i++) {
        c.rank_start[c.nodes[i].rank + 1]++;
    }
    for (int r = 0; r < c.rank_count; r++) {
        c.rank_start[r + 1] += c.rank_start[r];
    }
    asetlen(c.flat, node_count);
    int *at = NULL;
    asetlen(at, c.rank_count);
    for (int r = 0; r < c.rank_count; r++) {
        at[r] = c.rank_start[r];
    }
    for (int i = 0; i < node_count; i++) {
        c.flat[at[c.nodes[i].rank]++] = i;
    }
    afree(at);
    ar_renumber_slots(&c);
    ar_index_edges(&c);
    for (int i = 0; i < node_count; i++) {
        if (c.nodes[i].box < 0) {
            continue;
        }
        c.nodes[i].priority = c.in_start[i + 1] - c.in_start[i] +
                              c.out_start[i + 1] - c.out_start[i];
    }

    ar_order_ranks(&c);
    ar_assign_y(&c);

    float *rank_x = NULL;
    asetlen(rank_x, c.rank_count);
    float x = 0.0f;
    for (int r = 0; r < c.rank_count; r++) {
        float width = 0.0f;
        for (int i = c.rank_start[r]; i < c.rank_start[r + 1]; i++) {
            const ar_lnode *n = &c.nodes[c.flat[i]];
            if (n->box >= 0 && g->boxes[n->box].width > width) {
                width = g->boxes[n->box].width;
            }
        }
        rank_x[r] = x;
        x += width + AR_RANK_GAP;
    }

    /* Refine against the component's own boxes and wires, in local indices. */
    arrange_box *sub_boxes = NULL;
    arrange_link *sub_links = NULL;
    arrange_point *sub_pos = NULL;
    int *rank_of = NULL;
    asetlen(sub_pos, count);
    asetlen(rank_of, count);
    for (int i = 0; i < count; i++) {
        apush(sub_boxes, g->boxes[boxes[i]]);
        sub_pos[i] = (arrange_point){ rank_x[rank[i]], c.nodes[i].y };
        rank_of[i] = rank[i];
    }
    for (int i = 0; i < link_count; i++) {
        const arrange_link *l = &g->links[links[i]];
        apush(sub_links, (arrange_link){ local[l->from], local[l->to], l->from_pin, l->to_pin });
    }
    arrange_graph sub = { sub_boxes, count, sub_links, acount(sub_links) };
    ar_refine(&sub, sub_pos, rank_of, c.rank_count);

    /* Stack the component below the ones already placed. */
    float min_x = sub_pos[0].x;
    float min_y = sub_pos[0].y;
    float max_y = sub_pos[0].y + sub_boxes[0].height;
    for (int i = 1; i < count; i++) {
        min_x = math_min(min_x, sub_pos[i].x);
        min_y = math_min(min_y, sub_pos[i].y);
        max_y = math_max(max_y, sub_pos[i].y + sub_boxes[i].height);
    }
    for (int i = 0; i < count; i++) {
        pos[boxes[i]] = (arrange_point){ sub_pos[i].x - min_x,
                                         sub_pos[i].y - min_y + *stack_y };
    }
    *stack_y += max_y - min_y + AR_COMPONENT_GAP;

    afree(rank_of);
    afree(sub_pos);
    afree(sub_links);
    afree(sub_boxes);
    afree(rank_x);
    afree(rank);
    afree(edge_link);
    afree(edge_dst);
    afree(edge_src);
    afree(dropped);
    afree(reversed);
    ar_ctx_free(&c);
}

/* --- driver -------------------------------------------------------------- */

static int ar_find(int *parent, int i)
{
    while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

void arrange_layout(const arrange_graph *g, arrange_point *pos)
{
    if (g->box_count <= 0) {
        return;
    }
    int *parent = NULL;
    asetlen(parent, g->box_count);
    for (int i = 0; i < g->box_count; i++) {
        parent[i] = i;
    }
    for (int i = 0; i < g->link_count; i++) {
        int a = ar_find(parent, g->links[i].from);
        int b = ar_find(parent, g->links[i].to);
        if (a != b) {
            parent[a] = b;
        }
    }
    int *local = NULL;
    asetlen(local, g->box_count);
    int *boxes = NULL;
    int *links = NULL;
    float stack_y = 0.0f;
    /* Ascending first-member order keeps the stacking deterministic. */
    for (int root = 0; root < g->box_count; root++) {
        if (ar_find(parent, root) != root) {
            continue;
        }
        aclear(boxes);
        aclear(links);
        for (int i = 0; i < g->box_count; i++) {
            if (ar_find(parent, i) != root) {
                continue;
            }
            local[i] = acount(boxes);
            apush(boxes, i);
        }
        for (int i = 0; i < g->link_count; i++) {
            if (ar_find(parent, g->links[i].from) == root) {
                apush(links, i);
            }
        }
        ar_layout_component(g, boxes, links, local, pos, &stack_y);
    }
    afree(links);
    afree(boxes);
    afree(local);
    afree(parent);
}
