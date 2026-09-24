#include "graph/authoring/arrange.h"

#include "support/array.h"
#include "support/math.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

#define AR_RANK_GAP 90.0f
#define AR_NODE_GAP 36.0f
#define AR_COMPONENT_GAP 120.0f
#define AR_WIRE_LANE 22.0f
#define AR_LANE_GAP 6.0f

#define AR_BEND_MIN 40.0f
#define AR_CURVE_SAMPLES 12
#define AR_COST_CROSS 1.0f
#define AR_COST_HIT 4.0f
#define AR_COST_OVERLAP 40.0f
#define AR_COST_LENGTH 0.0008f

#define AR_FAN_CROSS_WEIGHT 2

#define AR_ORDER_SWEEPS 8
#define AR_TRANSPOSE_PASSES 4
#define AR_COORD_SWEEPS 8
#define AR_TIGHTEN_PASSES 8

#define AR_FREE 1.0e9f
#define AR_CORRIDOR_PRIORITY 1000000

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

static arrange_point ar_link_start(const arrange_graph *g, const arrange_point *pos, const arrange_link *l)
{
    const arrange_box *b = &g->boxes[l->from];
    return (arrange_point){ pos[l->from].x + b->width,
                            pos[l->from].y + ar_pin_y(b, l->from_pin) };
}

static arrange_point ar_link_end(const arrange_graph *g, const arrange_point *pos, const arrange_link *l)
{
    return (arrange_point){ pos[l->to].x,
                            pos[l->to].y + ar_pin_y(&g->boxes[l->to], l->to_pin) };
}

/* The canvas draws a wire without waypoints as one cubic whose handles grow
 * with its horizontal span. */
static ar_curve ar_curve_of(const arrange_graph *g, const arrange_point *pos, const arrange_link *l)
{
    ar_curve c;
    c.p0 = ar_link_start(g, pos, l);
    c.p3 = ar_link_end(g, pos, l);
    float bend = math_max(AR_BEND_MIN, math_abs(c.p3.x - c.p0.x) * 0.5f);
    c.p1 = l->custom_handles
             ? (arrange_point){ c.p0.x + l->from_handle_x, c.p0.y + l->from_handle_y }
             : (arrange_point){ c.p0.x + bend, c.p0.y };
    c.p2 = l->custom_handles
             ? (arrange_point){ c.p3.x + l->to_handle_x, c.p3.y + l->to_handle_y }
             : (arrange_point){ c.p3.x - bend, c.p3.y };
    return c;
}

/* A routed wire is one cubic per stretch between waypoints. It crosses every
 * waypoint horizontally, with handles no longer than half the shorter
 * neighbouring stretch. */
static float ar_route_tangent(const arrange_point *knot, int count, int i, float dir)
{
    float span;
    if (i == 0) {
        span = math_abs(knot[1].x - knot[0].x);
    } else if (i == count - 1) {
        span = math_abs(knot[i].x - knot[i - 1].x);
    } else {
        span = math_min(
            math_abs(knot[i].x - knot[i - 1].x),
            math_abs(knot[i + 1].x - knot[i].x)
        );
    }
    return dir * math_min(AR_BEND_MIN, span * 0.5f);
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

/* A wire flattened to the polyline every defect test runs on. Most wires run
 * left to right, and two such polylines only need their segments with
 * overlapping x ranges compared. */
typedef struct ar_wire
{
    int first; /* into the shared point list */
    int count;
    arrange_point min;
    arrange_point max;
    float length;
    bool monotone;
} ar_wire;

static void ar_sample(arrange_point **points, const ar_curve *c)
{
    for (int i = 1; i <= AR_CURVE_SAMPLES; i++) {
        apush(*points, ar_curve_at(c, (float)i / AR_CURVE_SAMPLES));
    }
}

static void ar_sample_route(arrange_point **points, const arrange_point *knot, int count)
{
    float dir = knot[count - 1].x >= knot[0].x ? 1.0f : -1.0f;
    for (int k = 0; k + 1 < count; k++) {
        ar_curve c = { knot[k],
                       { knot[k].x + ar_route_tangent(knot, count, k, dir),
                         knot[k].y },
                       { knot[k + 1].x - ar_route_tangent(knot, count, k + 1, dir),
                         knot[k + 1].y },
                       knot[k + 1] };
        ar_sample(points, &c);
    }
}

static ar_wire *ar_wires_make(
    const arrange_graph *g,
    const arrange_point *pos,
    const arrange_routes *routes,
    arrange_point **points
)
{
    ar_wire *wires = NULL;
    arrange_point *knot = NULL;
    for (int i = 0; i < g->link_count; i++) {
        ar_curve c = ar_curve_of(g, pos, &g->links[i]);
        ar_wire w = { .first = acount(*points) };
        apush(*points, c.p0);
        aclear(knot);
        apush(knot, c.p0);
        for (int k = routes->start[i]; k < routes->start[i + 1]; k++) {
            apush(knot, routes->points[k]);
        }
        apush(knot, c.p3);
        if (acount(knot) > 2) {
            ar_sample_route(points, knot, acount(knot));
        } else {
            ar_sample(points, &c);
        }
        w.count = acount(*points) - w.first;
        const arrange_point *pt = *points + w.first;
        w.min = pt[0];
        w.max = pt[0];
        w.monotone = true;
        for (int k = 1; k < w.count; k++) {
            float dx = pt[k].x - pt[k - 1].x;
            float dy = pt[k].y - pt[k - 1].y;
            w.length += sqrtf(dx * dx + dy * dy);
            w.min = vec2_min(w.min, pt[k]);
            w.max = vec2_max(w.max, pt[k]);
            w.monotone &= dx >= 0.0f;
        }
        apush(wires, w);
    }
    afree(knot);
    return wires;
}

static bool ar_wires_cross(const arrange_point *points, const ar_wire *a, const ar_wire *b)
{
    if (!ar_boxes_overlap(a->min, a->max, b->min, b->max)) {
        return false;
    }
    const arrange_point *pa = points + a->first;
    const arrange_point *pb = points + b->first;
    bool sweep = a->monotone && b->monotone;
    int first = 0;
    for (int i = 0; i + 1 < a->count; i++) {
        arrange_point a0 = pa[i];
        arrange_point a1 = pa[i + 1];
        arrange_point lo = vec2_min(a0, a1);
        arrange_point hi = vec2_max(a0, a1);
        if (!ar_boxes_overlap(lo, hi, b->min, b->max)) {
            continue;
        }
        while (sweep && first + 1 < b->count && pb[first + 1].x < a0.x) {
            first++;
        }
        for (int j = sweep ? first : 0; j + 1 < b->count; j++) {
            arrange_point b0 = pb[j];
            arrange_point b1 = pb[j + 1];
            if (sweep && b0.x > a1.x) {
                break;
            }
            if (ar_boxes_overlap(lo, hi, vec2_min(b0, b1), vec2_max(b0, b1)) &&
                ar_segments_cross(a0, a1, b0, b1)) {
                return true;
            }
        }
    }
    return false;
}

static bool ar_wire_hits_box(const arrange_point *points, const ar_wire *w, arrange_point min, arrange_point max)
{
    if (!ar_boxes_overlap(w->min, w->max, min, max)) {
        return false;
    }
    const arrange_point *pt = points + w->first;
    for (int i = 0; i + 1 < w->count; i++) {
        if (ar_boxes_overlap(vec2_min(pt[i], pt[i + 1]), vec2_max(pt[i], pt[i + 1]), min, max) &&
            ar_segment_hits_box(pt[i], pt[i + 1], min, max)) {
            return true;
        }
    }
    return false;
}

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
    arrange_routes routes = { 0 };
    arrange_route(g, pos, &routes);
    arrange_point *points = NULL;
    ar_wire *wires = ar_wires_make(g, pos, &routes, &points);
    for (int i = 0; i < g->link_count; i++) {
        score.wire_length += wires[i].length;
    }
    for (int i = 0; i < g->link_count; i++) {
        for (int j = i + 1; j < g->link_count; j++) {
            if (ar_share_pin(&g->links[i], &g->links[j])) {
                continue;
            }
            if (ar_wires_cross(points, &wires[i], &wires[j])) {
                score.wire_crossings++;
            }
        }
        for (int b = 0; b < g->box_count; b++) {
            if (ar_link_touches(&g->links[i], b)) {
                continue;
            }
            arrange_point min, max;
            ar_box_bounds(g, pos, b, &min, &max);
            if (ar_wire_hits_box(points, &wires[i], min, max)) {
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
    afree(wires);
    afree(points);
    arrange_routes_free(&routes);
    return score;
}

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
    int *best_order;
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

static void ar_sort_rank(ar_ctx *c, int rank, const float *values)
{
    int first = c->rank_start[rank];
    int last = c->rank_start[rank + 1];
    for (int i = first + 1; i < last; i++) {
        int node = c->flat[i];
        int j = i;
        while (j > first && values[c->flat[j - 1]] > values[node] &&
               !(c->nodes[node].pinned && c->nodes[c->flat[j - 1]].pinned)) {
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

static void ar_transpose(ar_ctx *c)
{
    for (int pass = 0; pass < AR_TRANSPOSE_PASSES; pass++) {
        bool improved = false;
        for (int rank = 0; rank < c->rank_count; rank++) {
            int last = c->rank_start[rank + 1];
            for (int i = c->rank_start[rank]; i + 1 < last; i++) {
                int a = c->flat[i];
                int b = c->flat[i + 1];
                if ((c->nodes[a].pinned && c->nodes[b].pinned) ||
                    ar_pair_crossings(c, b, a) >= ar_pair_crossings(c, a, b)) {
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

/* Parallel lanes sit closer together than boxes do. */
static float ar_gap(const ar_lnode *a, const ar_lnode *b)
{
    return a->box < 0 && b->box < 0 ? AR_LANE_GAP : AR_NODE_GAP;
}

/* How many corridors fit between two pinned boxes; a column's ends fit any
 * number. */
static int ar_gap_room(const ar_lnode *above, const ar_lnode *below, int any)
{
    if (!above || !below) {
        return any;
    }
    float room = below->y - (above->y + above->height) - 2.0f * AR_NODE_GAP + AR_LANE_GAP;
    return room > 0.0f ? (int)(room / (AR_WIRE_LANE + AR_LANE_GAP)) : 0;
}

/* The order can put more corridors between two pinned boxes than fit there.
 * Moves the excess past the boxes, one at a time, toward the nearest gap with
 * room. */
static void ar_fit_corridors(ar_ctx *c, int rank)
{
    int first = c->rank_start[rank];
    int last = c->rank_start[rank + 1];
    int *gap_start = NULL;
    int *gap_count = NULL;
    int *gap_room = NULL;
    for (int guard = (last - first) * (last - first); guard > 0; guard--) {
        aclear(gap_start);
        aclear(gap_count);
        aclear(gap_room);
        int start = first;
        for (int i = first; i <= last; i++) {
            if (i < last && !c->nodes[c->flat[i]].pinned) {
                continue;
            }
            const ar_lnode *above = start > first ? &c->nodes[c->flat[start - 1]]
                                                  : NULL;
            const ar_lnode *below = i < last ? &c->nodes[c->flat[i]] : NULL;
            apush(gap_start, start);
            apush(gap_count, i - start);
            apush(gap_room, ar_gap_room(above, below, last - first));
            start = i + 1;
        }
        int over = 0;
        while (over < acount(gap_count) && gap_count[over] <= gap_room[over]) {
            over++;
        }
        if (over == acount(gap_count)) {
            break;
        }
        int up = over - 1;
        while (up > 0 && gap_count[up] >= gap_room[up]) {
            up--;
        }
        int down = over + 1;
        while (down + 1 < acount(gap_count) && gap_count[down] >= gap_room[down]) {
            down++;
        }
        bool upward = up >= 0 &&
                      (down >= acount(gap_count) || over - up <= down - over);
        int from = upward ? gap_start[over] : gap_start[over] + gap_count[over] - 1;
        int to = upward ? from - 1 : from + 1;
        int corridor = c->flat[from];
        c->flat[from] = c->flat[to];
        c->flat[to] = corridor;
        c->nodes[c->flat[from]].slot = from;
        c->nodes[corridor].slot = to;
    }
    afree(gap_start);
    afree(gap_count);
    afree(gap_room);
}

/* Stacks one run of free nodes: under the pinned box above it, over the one
 * below it, centred between both, or centred on zero when nothing is pinned.
 * A run too tall for its gap shares the space evenly. */
static void ar_pack_run(ar_ctx *c, int first, int last, const ar_lnode *above, const ar_lnode *below)
{
    const ar_lnode *top = &c->nodes[c->flat[first]];
    const ar_lnode *bottom = &c->nodes[c->flat[last - 1]];
    float heights = 0.0f;
    float gaps = 0.0f;
    for (int i = first; i < last; i++) {
        heights += c->nodes[c->flat[i]].height;
        if (i + 1 < last) {
            gaps += ar_gap(&c->nodes[c->flat[i]], &c->nodes[c->flat[i + 1]]);
        }
    }
    float scale = 1.0f;
    float y = -(heights + gaps) * 0.5f;
    if (above && below) {
        float room = below->y - (above->y + above->height) - heights;
        float margins = ar_gap(above, top) + ar_gap(bottom, below);
        scale = math_min(1.0f, math_max(room, 0.0f) / (gaps + margins));
        y = above->y + above->height + ar_gap(above, top) * scale +
            math_max(room - (gaps + margins) * scale, 0.0f) * 0.5f;
    } else if (above) {
        y = above->y + above->height + ar_gap(above, top);
    } else if (below) {
        y = below->y - ar_gap(bottom, below) - heights - gaps;
    }
    for (int i = first; i < last; i++) {
        ar_lnode *n = &c->nodes[c->flat[i]];
        n->y = y;
        y += n->height;
        if (i + 1 < last) {
            y += ar_gap(n, &c->nodes[c->flat[i + 1]]) * scale;
        }
    }
}

static void ar_pack_rank(ar_ctx *c, int rank)
{
    ar_fit_corridors(c, rank);
    int first = c->rank_start[rank];
    int last = c->rank_start[rank + 1];
    for (int i = first; i < last;) {
        if (c->nodes[c->flat[i]].pinned) {
            i++;
            continue;
        }
        int end = i;
        while (end < last && !c->nodes[c->flat[end]].pinned) {
            end++;
        }
        const ar_lnode *above = i > first ? &c->nodes[c->flat[i - 1]] : NULL;
        const ar_lnode *below = end < last ? &c->nodes[c->flat[end]] : NULL;
        ar_pack_run(c, i, end, above, below);
        i = end;
    }
}

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
        slack += gap - ar_gap(a, b);
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
        if (moved->pinned) {
            break;
        }
        float limit = dir > 0 ? prev->y + prev->height + ar_gap(prev, moved)
                              : prev->y - ar_gap(prev, moved) - moved->height;
        if (dir > 0 ? moved->y >= limit : moved->y <= limit) {
            break;
        }
        moved->y = limit;
    }
}

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

static void ar_align_rank(ar_ctx *c, int rank, bool forward)
{
    int first = c->rank_start[rank];
    int last = c->rank_start[rank + 1];
    aclear(c->scratch);
    for (int i = first; i < last; i++) {
        c->nodes[c->flat[i]].fixed = c->nodes[c->flat[i]].pinned;
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
        if (c->nodes[node].pinned) {
            continue;
        }
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

/* Adds a wire between two layered nodes, with one corridor node for each
 * rank it passes. Returns the first corridor, or -1 for adjacent ranks. */
static int ar_ctx_link(ar_ctx *c, int src, int dst, float src_anchor, float dst_anchor)
{
    int src_rank = c->nodes[src].rank;
    int dst_rank = c->nodes[dst].rank;
    float src_seed = c->nodes[src].seed;
    float dst_seed = c->nodes[dst].seed;
    int span = dst_rank - src_rank;
    assert(span >= 1);
    int first = span > 1 ? acount(c->nodes) : -1;
    int prev = src;
    float prev_anchor = src_anchor;
    for (int r = src_rank + 1; r < dst_rank; r++) {
        float t = (float)(r - src_rank) / (float)span;
        int corridor = acount(c->nodes);
        apush(
            c->nodes,
            (ar_lnode){ .box = -1,
                        .rank = r,
                        .height = AR_WIRE_LANE,
                        .seed = src_seed + (dst_seed - src_seed) * t,
                        .priority = AR_CORRIDOR_PRIORITY }
        );
        apush(c->edges, (ar_ledge){ prev, corridor, prev_anchor, AR_WIRE_LANE * 0.5f });
        prev = corridor;
        prev_anchor = AR_WIRE_LANE * 0.5f;
    }
    apush(c->edges, (ar_ledge){ prev, dst, prev_anchor, dst_anchor });
    return first;
}

/* Orders every rank to reduce crossings, then places each node vertically. */
static void ar_ctx_solve(ar_ctx *c)
{
    int node_count = acount(c->nodes);
    c->rank_count = 0;
    for (int i = 0; i < node_count; i++) {
        c->rank_count = math_max(c->rank_count, c->nodes[i].rank + 1);
    }
    asetlen(c->rank_start, c->rank_count + 1);
    for (int r = 0; r <= c->rank_count; r++) {
        c->rank_start[r] = 0;
    }
    for (int i = 0; i < node_count; i++) {
        c->rank_start[c->nodes[i].rank + 1]++;
    }
    for (int r = 0; r < c->rank_count; r++) {
        c->rank_start[r + 1] += c->rank_start[r];
    }
    asetlen(c->flat, node_count);
    int *at = NULL;
    asetlen(at, c->rank_count);
    for (int r = 0; r < c->rank_count; r++) {
        at[r] = c->rank_start[r];
    }
    for (int i = 0; i < node_count; i++) {
        c->flat[at[c->nodes[i].rank]++] = i;
    }
    afree(at);
    ar_renumber_slots(c);
    ar_index_edges(c);
    for (int i = 0; i < node_count; i++) {
        if (c->nodes[i].box < 0) {
            continue;
        }
        c->nodes[i].priority = c->in_start[i + 1] - c->in_start[i] +
                               c->out_start[i + 1] - c->out_start[i];
    }
    ar_order_ranks(c);
    ar_assign_y(c);
}

enum
{
    AR_WHITE,
    AR_GRAY,
    AR_BLACK
};

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
    assert(acount(topo) == count);

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
        ar_ctx_link(&c, src, dst, src_anchor, dst_anchor);
    }
    ar_ctx_solve(&c);

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

    /* Lanes count toward the extent, so the next component leaves them room. */
    float min_y = AR_FREE;
    float max_y = -AR_FREE;
    for (int i = 0; i < acount(c.nodes); i++) {
        min_y = math_min(min_y, c.nodes[i].y);
        max_y = math_max(max_y, c.nodes[i].y + c.nodes[i].height);
    }
    for (int i = 0; i < count; i++) {
        pos[boxes[i]] = (arrange_point){ rank_x[rank[i]],
                                         c.nodes[i].y - min_y + *stack_y };
    }
    *stack_y += max_y - min_y + AR_COMPONENT_GAP;

    afree(rank_x);
    afree(rank);
    afree(edge_link);
    afree(edge_dst);
    afree(edge_src);
    afree(dropped);
    afree(reversed);
    ar_ctx_free(&c);
}

static int ar_find(int *parent, int i)
{
    while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

typedef struct ar_key
{
    float key;
    int index;
} ar_key;

static int ar_key_compare(const void *a, const void *b)
{
    const ar_key *x = a;
    const ar_key *y = b;
    if (x->key != y->key) {
        return x->key < y->key ? -1 : 1;
    }
    return x->index - y->index;
}

static ar_key *ar_sorted_boxes(const arrange_graph *g, const arrange_point *pos, bool by_x)
{
    ar_key *order = NULL;
    asetlen(order, g->box_count);
    for (int i = 0; i < g->box_count; i++) {
        order[i] = (ar_key){ by_x ? pos[i].x : pos[i].y, i };
    }
    qsort(order, (size_t)g->box_count, sizeof *order, ar_key_compare);
    return order;
}

/* Groups boxes whose horizontal extents overlap into columns, left to right. */
static void ar_columns(
    const arrange_graph *g,
    const arrange_point *pos,
    int *column,
    float **left,
    float **right
)
{
    ar_key *order = ar_sorted_boxes(g, pos, true);
    for (int k = 0; k < g->box_count; k++) {
        int box = order[k].index;
        int last = acount(*left) - 1;
        float edge = pos[box].x + g->boxes[box].width;
        if (last < 0 || pos[box].x >= (*right)[last]) {
            apush(*left, pos[box].x);
            apush(*right, edge);
        } else {
            (*right)[last] = math_max((*right)[last], edge);
        }
        column[box] = acount(*left) - 1;
    }
    afree(order);
}

/* Drops waypoints inside a level run, which add nothing to the drawn wire. */
static void ar_emit_route(arrange_routes *routes, const arrange_point *knot, int count)
{
    arrange_point kept = knot[0];
    for (int k = 1; k + 1 < count; k++) {
        if (knot[k].y == kept.y && knot[k + 1].y == kept.y) {
            continue;
        }
        apush(routes->points, knot[k]);
        kept = knot[k];
    }
}

/* Re-runs the layered layout with every box pinned where it is: columns stand
 * in for ranks, and only the corridors that carry long wires are ordered and
 * placed. On an arranged graph this finds the lanes the layout reserved. */
void arrange_route(const arrange_graph *g, const arrange_point *pos, arrange_routes *routes)
{
    aclear(routes->start);
    aclear(routes->points);
    /* Without boxes there are no columns, and no links to route. */
    if (!g->box_count) {
        apush(routes->start, 0);
        return;
    }
    int *column = NULL;
    float *left = NULL;
    float *right = NULL;
    asetlen(column, g->box_count);
    ar_columns(g, pos, column, &left, &right);

    ar_ctx c = { 0 };
    int *node_of = NULL;
    asetlen(node_of, g->box_count);
    ar_key *order = ar_sorted_boxes(g, pos, false);
    for (int k = 0; k < g->box_count; k++) {
        int box = order[k].index;
        node_of[box] = acount(c.nodes);
        apush(
            c.nodes,
            (ar_lnode){ .box = box,
                        .rank = column[box],
                        .y = pos[box].y,
                        .height = g->boxes[box].height,
                        .seed = pos[box].y + g->boxes[box].height * 0.5f,
                        .pinned = true }
        );
    }
    afree(order);
    int *corridor = NULL;
    asetlen(corridor, g->link_count);
    for (int i = 0; i < g->link_count; i++) {
        const arrange_link *l = &g->links[i];
        corridor[i] = -1;
        if (column[l->from] == column[l->to]) {
            continue;
        }
        float from_anchor = ar_pin_y(&g->boxes[l->from], l->from_pin);
        float to_anchor = ar_pin_y(&g->boxes[l->to], l->to_pin);
        corridor[i] = column[l->from] < column[l->to]
                        ? ar_ctx_link(&c, node_of[l->from], node_of[l->to], from_anchor, to_anchor)
                        : ar_ctx_link(&c, node_of[l->to], node_of[l->from], to_anchor, from_anchor);
    }
    ar_ctx_solve(&c);

    /* Backward wires and wires with authored handles keep their own curve. */
    arrange_point *knot = NULL;
    for (int i = 0; i < g->link_count; i++) {
        apush(routes->start, acount(routes->points));
        const arrange_link *l = &g->links[i];
        int from = column[l->from];
        int to = column[l->to];
        if (from >= to || l->custom_handles) {
            continue;
        }
        arrange_point start = ar_link_start(g, pos, l);
        arrange_point end = ar_link_end(g, pos, l);
        aclear(knot);
        apush(knot, start);
        if (start.x < right[from]) {
            apush(knot, ((arrange_point){ right[from], start.y }));
        }
        for (int k = 0; k < to - from - 1; k++) {
            const ar_lnode *lane = &c.nodes[corridor[i] + k];
            float y = lane->y + AR_WIRE_LANE * 0.5f;
            apush(knot, ((arrange_point){ left[lane->rank], y }));
            apush(knot, ((arrange_point){ right[lane->rank], y }));
        }
        if (end.x > left[to]) {
            apush(knot, ((arrange_point){ left[to], end.y }));
        }
        apush(knot, end);
        ar_emit_route(routes, knot, acount(knot));
    }
    apush(routes->start, acount(routes->points));
    afree(knot);
    afree(corridor);
    afree(node_of);
    afree(left);
    afree(right);
    afree(column);
    ar_ctx_free(&c);
}

void arrange_routes_free(arrange_routes *routes)
{
    afree(routes->start);
    afree(routes->points);
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
