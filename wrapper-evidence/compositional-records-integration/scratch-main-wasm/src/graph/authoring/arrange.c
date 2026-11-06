#include "graph/authoring/arrange.h"

#include "graph/authoring/arrange_internal.h"
#include "support/array.h"
#include "support/math.h"

#include <assert.h>
#include <stdlib.h>

#define AR_NODE_GAP 36.0f
#define AR_COMPONENT_GAP 120.0f
#define AR_LANE_GAP 6.0f
#define AR_LEVEL_SNAP 0.5f /* heights closer than this are one level */

#define AR_FAN_CROSS_WEIGHT 2
/* An execution wire outweighs this many data wires when a node lines up
 * with its neighbours. Heavier weights straighten more chains but bend the
 * data curves around them into more crossings. */
#define AR_FLOW_WEIGHT 3

#define AR_ORDER_SWEEPS 8
#define AR_TRANSPOSE_PASSES 4
#define AR_COORD_SWEEPS 8
#define AR_TIGHTEN_PASSES 8
#define AR_STRAIGHTEN_PASSES 4

#define AR_CORRIDOR_PRIORITY 1000000

void ar_ctx_free(ar_ctx *c)
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
    afree(c->pulls);
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

/* Where a node's neighbours on one side would put it: the median of their
 * pulls, with execution wires counted AR_FLOW_WEIGHT times so execution
 * chains run straight. Between two middle pulls it follows the one on its
 * higher pin. */
static bool ar_neighbor_median_y(ar_ctx *c, int node, bool forward, float *out)
{
    aclear(c->pulls);
    const int *start = forward ? c->in_start : c->out_start;
    const int *list = forward ? c->in_edge : c->out_edge;
    for (int i = start[node]; i < start[node + 1]; i++) {
        const ar_ledge *e = &c->edges[list[i]];
        arrange_point pull = forward
                               ? (arrange_point){ c->nodes[e->from].y +
                                                      e->from_anchor - e->to_anchor,
                                                  e->to_anchor }
                               : (arrange_point){ c->nodes[e->to].y + e->to_anchor -
                                                      e->from_anchor,
                                                  e->from_anchor };
        for (int w = 0; w < (e->flow ? AR_FLOW_WEIGHT : 1); w++) {
            apush(c->pulls, pull);
        }
    }
    int count = acount(c->pulls);
    if (!count) {
        return false;
    }
    arrange_point *pull = c->pulls;
    for (int i = 1; i < count; i++) {
        arrange_point v = pull[i];
        int j = i;
        while (j > 0 && pull[j - 1].x > v.x) {
            pull[j] = pull[j - 1];
            j--;
        }
        pull[j] = v;
    }
    const arrange_point *lower = &pull[count / 2];
    const arrange_point *upper = &pull[(count - 1) / 2];
    *out = upper->y <= lower->y ? upper->x : lower->x;
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

void ar_lanes_init(ar_lanes *w, int box_count)
{
    asetlen(w->head, box_count);
    for (int i = 0; i < box_count; i++) {
        w->head[i] = -1;
    }
}

void ar_lanes_free(ar_lanes *w)
{
    afree(w->start);
    afree(w->lanes);
    afree(w->head);
    afree(w->next);
    afree(w->out);
}

int ar_lane_count(const ar_lanes *w, int wire)
{
    int end = wire + 1 < acount(w->start) ? w->start[wire + 1] : acount(w->lanes);
    return end - w->start[wire];
}

/* Marks the edges a wire added from `first` on as execution flow. */
void ar_mark_flow(ar_ctx *c, int first, bool flow)
{
    for (int k = first; k < acount(c->edges); k++) {
        c->edges[k].flow = flow;
    }
}

/* Whether a node has room for its top at `y` between its neighbours in its
 * rank. */
static bool ar_fits(const ar_ctx *c, int node, float y)
{
    const ar_lnode *n = &c->nodes[node];
    int first = c->rank_start[n->rank];
    int last = c->rank_start[n->rank + 1];
    if (n->slot > first) {
        const ar_lnode *above = &c->nodes[c->flat[n->slot - 1]];
        if (y < above->y + above->height + ar_gap(above, n)) {
            return false;
        }
    }
    if (n->slot + 1 < last) {
        const ar_lnode *below = &c->nodes[c->flat[n->slot + 1]];
        if (y + n->height + ar_gap(n, below) > below->y) {
            return false;
        }
    }
    return true;
}

bool ar_same_level(float a, float b)
{
    return math_abs(a - b) < AR_LEVEL_SNAP;
}

/* A run of corridors starts after a box, or after a trunk that splits. */
static bool ar_run_starts(const ar_ctx *c, int node)
{
    if (c->nodes[node].box >= 0) {
        return false;
    }
    int from = c->edges[c->in_edge[c->in_start[node]]].from;
    return c->nodes[from].box >= 0 || c->out_start[from + 1] - c->out_start[from] > 1;
}

/* How often a run of corridors at `levels` changes height between the level
 * it comes from and the levels it leaves to. */
static int ar_run_steps(const ar_ctx *c, const int *run, int count, const float *levels, float in)
{
    int steps = !ar_same_level(in, levels[0]);
    for (int i = 1; i < count; i++) {
        steps += !ar_same_level(levels[i - 1], levels[i]);
    }
    int last = run[count - 1];
    for (int i = c->out_start[last]; i < c->out_start[last + 1]; i++) {
        const ar_ledge *e = &c->edges[c->out_edge[i]];
        steps += !ar_same_level(levels[count - 1], c->nodes[e->to].y + e->to_anchor);
    }
    return steps;
}

/* Alignment leaves a wire's lanes wherever each column had room, so a long
 * wire can step at every column. Moves each run of corridors to one height,
 * the level it comes from, leaves to, or already holds somewhere, when all
 * of it has room there and the wire turns fewer times. A lane may always
 * shift by a lane gap, which keeps wires apart but spares a small jog. */
static void ar_straighten(ar_ctx *c)
{
    int *run = NULL;
    float *levels = NULL;
    float *flat = NULL;
    for (int pass = 0; pass < AR_STRAIGHTEN_PASSES; pass++) {
        bool moved = false;
        for (int start = 0; start < acount(c->nodes); start++) {
            if (!ar_run_starts(c, start)) {
                continue;
            }
            aclear(run);
            aclear(levels);
            for (int node = start;;) {
                apush(run, node);
                apush(levels, c->nodes[node].y + AR_WIRE_LANE * 0.5f);
                int out = c->out_start[node];
                if (c->out_start[node + 1] - out != 1) {
                    break;
                }
                node = c->edges[c->out_edge[out]].to;
                if (c->nodes[node].box >= 0) {
                    break;
                }
            }
            int count = acount(run);
            const ar_ledge *in_edge = &c->edges[c->in_edge[c->in_start[start]]];
            float in = c->nodes[in_edge->from].y + in_edge->from_anchor;
            int best_steps = ar_run_steps(c, run, count, levels, in);
            float best = 0.0f;
            float best_move = AR_FREE;
            aclear(flat);
            apush(flat, in);
            for (int i = 0; i < count; i++) {
                apush(flat, levels[i]);
            }
            int last = run[count - 1];
            for (int i = c->out_start[last]; i < c->out_start[last + 1]; i++) {
                const ar_ledge *e = &c->edges[c->out_edge[i]];
                apush(flat, c->nodes[e->to].y + e->to_anchor);
            }
            for (int k = 0; k < acount(flat); k++) {
                float y = flat[k];
                float move = 0.0f;
                bool fits = true;
                for (int i = 0; i < count && fits; i++) {
                    float shift = math_abs(levels[i] - y);
                    fits = shift <= AR_LANE_GAP ||
                           ar_fits(c, run[i], y - AR_WIRE_LANE * 0.5f);
                    move += shift;
                }
                aclear(levels);
                for (int i = 0; i < count; i++) {
                    apush(levels, y);
                }
                int steps = ar_run_steps(c, run, count, levels, in);
                aclear(levels);
                for (int i = 0; i < count; i++) {
                    apush(levels, c->nodes[run[i]].y + AR_WIRE_LANE * 0.5f);
                }
                if (fits && (steps < best_steps ||
                             (steps == best_steps && best_move < AR_FREE &&
                              move < best_move))) {
                    best_steps = steps;
                    best = y;
                    best_move = move;
                }
            }
            if (best_move >= AR_FREE) {
                continue;
            }
            for (int i = 0; i < count; i++) {
                c->nodes[run[i]].y = best - AR_WIRE_LANE * 0.5f;
            }
            moved = true;
        }
        if (!moved) {
            break;
        }
    }
    afree(run);
    afree(levels);
    afree(flat);
}

/* Adds a wire between two layered nodes, with a corridor node for each rank
 * it passes and does not share. A forward wire shares by its output pin, at
 * `src_anchor` on `src`; a backward wire shares nothing. */
void ar_ctx_link(ar_ctx *c, ar_lanes *w, int src, int dst, float src_anchor, float dst_anchor, bool back)
{
    int src_rank = c->nodes[src].rank;
    int dst_rank = c->nodes[dst].rank;
    float src_seed = c->nodes[src].seed;
    float dst_seed = c->nodes[dst].seed;
    int span = dst_rank - src_rank;
    assert(span >= 1);
    int trunk = -1;
    for (int k = back ? -1 : w->head[src]; k >= 0; k = w->next[k]) {
        if (w->out[k] == src_anchor &&
            (trunk < 0 || ar_lane_count(w, k) > ar_lane_count(w, trunk))) {
            trunk = k;
        }
    }
    int shared = trunk < 0 ? 0 : math_min(ar_lane_count(w, trunk), span - 1);
    int wire = acount(w->start);
    apush(w->start, acount(w->lanes));
    apush(w->next, back ? -1 : w->head[src]);
    apush(w->out, src_anchor);
    if (!back) {
        w->head[src] = wire;
    }
    int prev = src;
    float prev_anchor = src_anchor;
    for (int k = 0; k < shared; k++) {
        prev = w->lanes[w->start[trunk] + k];
        prev_anchor = AR_WIRE_LANE * 0.5f;
        apush(w->lanes, prev);
    }
    for (int r = src_rank + 1 + shared; r < dst_rank; r++) {
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
        apush(
            c->edges,
            (ar_ledge){ .from = prev,
                        .to = corridor,
                        .from_anchor = prev_anchor,
                        .to_anchor = AR_WIRE_LANE * 0.5f,
                        .back = back }
        );
        apush(w->lanes, corridor);
        prev = corridor;
        prev_anchor = AR_WIRE_LANE * 0.5f;
    }
    apush(
        c->edges,
        (ar_ledge){ .from = prev,
                    .to = dst,
                    .from_anchor = prev_anchor,
                    .to_anchor = dst_anchor,
                    .back = back }
    );
}

/* Orders every rank to reduce crossings, then places each node vertically. */
void ar_ctx_solve(ar_ctx *c)
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
    ar_straighten(c);
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

int ar_find(int *parent, int i)
{
    while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

void ar_union(int *parent, int a, int b)
{
    a = ar_find(parent, a);
    b = ar_find(parent, b);
    if (a != b) {
        parent[a] = b;
    }
}

int ar_key_compare(const void *a, const void *b)
{
    const ar_key *x = a;
    const ar_key *y = b;
    if (x->key != y->key) {
        return x->key < y->key ? -1 : 1;
    }
    return x->index - y->index;
}

static void ar_layout_component(
    const arrange_graph *g,
    arrange_route_style style,
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
    ar_lanes lanes = { 0 };
    ar_lanes_init(&lanes, count);
    for (int i = 0; i < acount(edge_src); i++) {
        const arrange_link *l = &g->links[links[edge_link[i]]];
        bool back = reversed[edge_link[i]];
        int first = acount(c.edges);
        ar_ctx_link(
            &c,
            &lanes,
            edge_src[i],
            edge_dst[i],
            back ? l->to_y : l->from_y,
            back ? l->from_y : l->to_y,
            back
        );
        ar_mark_flow(&c, first, l->flow);
    }
    ar_ctx_solve(&c);

    /* Tracked styles widen each gap to fit the tracks its wires turn on. */
    int *gap_tracks = NULL;
    asetlen(gap_tracks, c.rank_count);
    for (int r = 0; r < c.rank_count; r++) {
        gap_tracks[r] = 0;
    }
    if (ar_tracked(style)) {
        ar_pass *passes = NULL;
        for (int i = 0; i < acount(c.edges); i++) {
            const ar_ledge *e = &c.edges[i];
            if (!e->back) {
                apush(
                    passes,
                    ((ar_pass){ .gap = c.nodes[e->from].rank,
                                .in = c.nodes[e->from].y + e->from_anchor,
                                .out = c.nodes[e->to].y + e->to_anchor })
                );
            }
        }
        ar_assign_tracks(passes, acount(passes), gap_tracks);
        afree(passes);
    }

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
        x += width + ar_gap_width(style, gap_tracks[r]);
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

    afree(gap_tracks);
    afree(rank_x);
    afree(rank);
    afree(edge_link);
    afree(edge_dst);
    afree(edge_src);
    afree(dropped);
    afree(reversed);
    ar_lanes_free(&lanes);
    ar_ctx_free(&c);
}

/* Lays connected boxes out as components, one horizontal band each, stacked
 * top to bottom by their lowest band and then their lowest box index. */
void arrange_layout(const arrange_graph *g, arrange_route_style style, arrange_point *pos)
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
        ar_union(parent, g->links[i].from, g->links[i].to);
    }
    int *band = NULL;
    int *first = NULL;
    asetlen(band, g->box_count);
    asetlen(first, g->box_count);
    for (int i = 0; i < g->box_count; i++) {
        first[i] = -1;
    }
    for (int i = 0; i < g->box_count; i++) {
        int root = ar_find(parent, i);
        if (first[root] < 0) {
            first[root] = i;
            band[root] = g->boxes[i].band;
        }
        band[root] = math_min(band[root], g->boxes[i].band);
    }
    /* Bands and box indexes are small, so one float key orders both. */
    ar_key *components = NULL;
    for (int i = 0; i < g->box_count; i++) {
        if (first[i] >= 0) {
            apush(components, ((ar_key){ (float)band[i], first[i] }));
        }
    }
    asort(components, ar_key_compare);

    int *local = NULL;
    asetlen(local, g->box_count);
    int *boxes = NULL;
    int *links = NULL;
    float stack_y = 0.0f;
    for (int k = 0; k < acount(components); k++) {
        int root = ar_find(parent, components[k].index);
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
        ar_layout_component(g, style, boxes, links, local, pos, &stack_y);
    }
    afree(components);
    afree(links);
    afree(boxes);
    afree(local);
    afree(first);
    afree(band);
    afree(parent);
}
