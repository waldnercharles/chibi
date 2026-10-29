#include "graph/authoring/arrange_internal.h"
#include "support/array.h"
#include "support/math.h"

#include <stdlib.h>

#define AR_RANK_GAP 90.0f

/* Orthogonal and subway wires turn on tracks this far apart, with the outer
 * tracks this far from the columns. Vertical runs closer than the clearance
 * need tracks of their own. */
#define AR_TRACK_GAP 12.0f
#define AR_TRACK_MARGIN 24.0f
#define AR_TRACK_CLEAR 1.0f
/* A track order that lays one wire's run over another's costs as much as
 * this many crossings. */
#define AR_TRACK_OVERLAP_COST 64
#define AR_TRUNK 24.0f /* level run curved and straight fan-outs share */
#define AR_LOOP_CLEAR 18.0f

arrange_point ar_link_start(const arrange_graph *g, const arrange_point *pos, const arrange_link *l)
{
    return (arrange_point){ pos[l->from].x + g->boxes[l->from].width,
                            pos[l->from].y + l->from_y };
}

arrange_point ar_link_end(const arrange_point *pos, const arrange_link *l)
{
    return (arrange_point){ pos[l->to].x, pos[l->to].y + l->to_y };
}

/* Whether more than one forward wire leaves the output pin at `out` on `src`. */
static bool ar_fan_out(const ar_lanes *w, int src, float out)
{
    int count = 0;
    for (int k = w->head[src]; k >= 0 && count < 2; k = w->next[k]) {
        count += w->out[k] == out;
    }
    return count > 1;
}

/* Records a wire with no lanes. */
static void ar_lanes_skip(ar_lanes *w)
{
    apush(w->start, acount(w->lanes));
    apush(w->next, -1);
    apush(w->out, 0.0f);
}

/* Passes that share an end turn on one track: a fan-out leaves one point, a
 * fan-in enters one. `ys` holds each run's distinct in ends, then its
 * distinct out ends. */
typedef struct ar_run
{
    float lo;
    float hi;
    int first;
    int ins;
    int outs;
    int track;
    bool jog; /* its passes arriving at `jog_at` dogleg */
    float jog_at;
    float jog_track;
    float jog_y;
} ar_run;

static bool ar_runs_conflict(const ar_run *a, const ar_run *b)
{
    return a->lo <= b->hi + AR_TRACK_CLEAR && b->lo <= a->hi + AR_TRACK_CLEAR;
}

/* Crossings, and runs laid over each other, with `left` on the track left of
 * `right`: right's incoming legs cross left's vertical run, left's outgoing
 * legs cross right's, and one of left's outgoing legs lies over one of
 * right's incoming legs at the same height. */
static int ar_order_cost(const ar_run *left, const ar_run *right, const float *ys)
{
    int cost = 0;
    const float *right_in = ys + right->first;
    const float *left_out = ys + left->first + left->ins;
    for (int i = 0; i < right->ins; i++) {
        cost += right_in[i] > left->lo && right_in[i] < left->hi;
    }
    for (int i = 0; i < left->outs; i++) {
        cost += left_out[i] > right->lo && left_out[i] < right->hi;
        for (int j = 0; j < right->ins; j++) {
            cost += left_out[i] == right_in[j] ? AR_TRACK_OVERLAP_COST : 0;
        }
    }
    return cost;
}

static void ar_push_unique(float **ys, int first, float y, int *count)
{
    for (int i = first; i < acount(*ys); i++) {
        if ((*ys)[i] == y) {
            return;
        }
    }
    apush(*ys, y);
    (*count)++;
}

/* Orders runs left to right so that as much preference as possible points
 * rightward: sinks go last, sources first, and otherwise the run with the
 * most net preference to sit left (Eades, Lin and Smyth). `weight[i * n + j]`
 * is how much run i prefers to sit left of run j. */
static void ar_order_runs(const int *weight, int n, int *order)
{
    int *net = NULL;
    int *outs = NULL;
    int *ins = NULL;
    bool *placed = NULL;
    asetlen(net, n);
    asetlen(outs, n);
    asetlen(ins, n);
    asetlen(placed, n);
    for (int i = 0; i < n; i++) {
        net[i] = outs[i] = ins[i] = 0;
        placed[i] = false;
    }
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            int w = weight[i * n + j];
            if (w > 0) {
                outs[i]++;
                ins[j]++;
                net[i] += w;
                net[j] -= w;
            }
        }
    }
    int left = 0;
    int right = n - 1;
    while (left <= right) {
        int pick = -1;
        bool sink = false;
        for (int i = n - 1; i >= 0 && pick < 0; i--) {
            if (!placed[i] && !outs[i]) {
                pick = i;
                sink = true;
            }
        }
        for (int i = 0; i < n && !sink && pick < 0; i++) {
            if (!placed[i] && !ins[i]) {
                pick = i;
            }
        }
        for (int i = 0; i < n && !sink; i++) {
            if (!placed[i] && (pick < 0 || (ins[pick] && net[i] > net[pick]))) {
                pick = i;
            }
        }
        placed[pick] = true;
        order[sink ? right-- : left++] = pick;
        for (int j = 0; j < n; j++) {
            if (placed[j]) {
                continue;
            }
            int to = weight[pick * n + j];
            int from = weight[j * n + pick];
            if (to > 0) {
                ins[j]--;
                net[j] += to;
            }
            if (from > 0) {
                outs[j]--;
                net[j] -= from;
            }
        }
    }
    afree(net);
    afree(outs);
    afree(ins);
    afree(placed);
}

/* A run arriving at the height another run to its left leaves at would lie on
 * that run's outgoing leg. Such constraints can form a cycle no order meets,
 * as in a staircase of wires each ending where the next begins. The later
 * run then doglegs: it turns on a half track left of the other, runs level
 * midway to the next leg, and crosses to its own track. */
static void ar_dogleg_runs(ar_run *runs, int n, const float *ys)
{
    int total = 0;
    for (int i = 0; i < n; i++) {
        total = math_max(total, runs[i].first + runs[i].ins + runs[i].outs);
    }
    for (int r = 0; r < n; r++) {
        ar_run *right = &runs[r];
        for (int l = 0; l < n; l++) {
            const ar_run *left = &runs[l];
            if (left->track >= right->track || !ar_runs_conflict(left, right)) {
                continue;
            }
            for (int i = 0; i < left->outs; i++) {
                float y = ys[left->first + left->ins + i];
                for (int j = 0; j < right->ins; j++) {
                    if (ys[right->first + j] != y ||
                        (right->jog && right->jog_track <= (float)left->track - 0.5f)) {
                        continue;
                    }
                    right->jog = true;
                    right->jog_at = y;
                    right->jog_track = (float)left->track - 0.5f;
                }
            }
        }
        if (!right->jog) {
            continue;
        }
        float dir = right->lo < right->jog_at ? -1.0f : 1.0f;
        float room = AR_TRACK_GAP * 2.0f;
        for (int k = 0; k < total; k++) {
            float d = (ys[k] - right->jog_at) * dir;
            if (d > 0.0f) {
                room = math_min(room, d);
            }
        }
        right->jog_y = right->jog_at + dir * room * 0.5f;
    }
}

/* Assigns the tracks of one gap's runs: ordered for fewest crossings, each run
 * takes the leftmost track right of every conflicting run it should follow
 * and clear of every conflicting run already placed. Returns the track
 * count. */
static int ar_place_runs(ar_run *runs, int n, const float *ys)
{
    int *weight = NULL;
    int *order = NULL;
    bool *used = NULL;
    asetlen(weight, n * n);
    asetlen(order, n);
    asetlen(used, n);
    for (int i = 0; i < n; i++) {
        weight[i * n + i] = 0;
        for (int j = i + 1; j < n; j++) {
            int before = 0;
            int after = 0;
            if (ar_runs_conflict(&runs[i], &runs[j])) {
                before = ar_order_cost(&runs[i], &runs[j], ys);
                after = ar_order_cost(&runs[j], &runs[i], ys);
            }
            weight[i * n + j] = after - before;
            weight[j * n + i] = before - after;
        }
    }
    ar_order_runs(weight, n, order);
    int tracks = 0;
    for (int k = 0; k < n; k++) {
        ar_run *run = &runs[order[k]];
        int floor = 0;
        for (int t = 0; t < n; t++) {
            used[t] = false;
        }
        for (int m = 0; m < k; m++) {
            const ar_run *placed = &runs[order[m]];
            if (!ar_runs_conflict(run, placed)) {
                continue;
            }
            used[placed->track] = true;
            if (weight[order[m] * n + order[k]] > 0) {
                floor = math_max(floor, placed->track + 1);
            }
        }
        run->track = floor;
        while (used[run->track]) {
            run->track++;
        }
        tracks = math_max(tracks, run->track + 1);
    }
    ar_dogleg_runs(runs, n, ys);
    afree(weight);
    afree(order);
    afree(used);
    return tracks;
}

/* Scratch for routing the passes of one gap at a time. */
typedef struct ar_channel
{
    ar_pass **gap; /* the gap's passes */
    int *parent;
    ar_key *keys;
    ar_run *runs;
    int *run_of; /* per pass in the gap: its run, or -1 when it stays level */
    float *ys;
} ar_channel;

static void ar_channel_free(ar_channel *ch)
{
    afree(ch->gap);
    afree(ch->parent);
    afree(ch->keys);
    afree(ch->runs);
    afree(ch->run_of);
    afree(ch->ys);
}

/* Joins passes that share an end, first by where they arrive, then by where
 * they leave. */
static void ar_channel_join(ar_channel *ch)
{
    int n = acount(ch->gap);
    asetlen(ch->parent, n);
    for (int i = 0; i < n; i++) {
        ch->parent[i] = i;
    }
    for (int side = 0; side < 2; side++) {
        aclear(ch->keys);
        for (int i = 0; i < n; i++) {
            apush(ch->keys, ((ar_key){ side ? ch->gap[i]->out : ch->gap[i]->in, i }));
        }
        qsort(ch->keys, (size_t)n, sizeof *ch->keys, ar_key_compare);
        for (int i = 1; i < n; i++) {
            if (ch->keys[i].key == ch->keys[i - 1].key) {
                ar_union(ch->parent, ch->keys[i].index, ch->keys[i - 1].index);
            }
        }
    }
}

/* Collects each joined group that turns into a run with its distinct ends. */
static void ar_channel_runs(ar_channel *ch)
{
    int n = acount(ch->gap);
    aclear(ch->keys);
    for (int i = 0; i < n; i++) {
        apush(ch->keys, ((ar_key){ (float)ar_find(ch->parent, i), i }));
    }
    qsort(ch->keys, (size_t)n, sizeof *ch->keys, ar_key_compare);
    aclear(ch->runs);
    aclear(ch->ys);
    asetlen(ch->run_of, n);
    for (int i = 0; i < n;) {
        int end = i;
        while (end < n && ch->keys[end].key == ch->keys[i].key) {
            end++;
        }
        ar_run run = { .lo = AR_FREE, .hi = -AR_FREE, .first = acount(ch->ys), .track = -1 };
        for (int k = i; k < end; k++) {
            const ar_pass *p = ch->gap[ch->keys[k].index];
            run.lo = math_min(run.lo, math_min(p->in, p->out));
            run.hi = math_max(run.hi, math_max(p->in, p->out));
            ar_push_unique(&ch->ys, run.first, p->in, &run.ins);
        }
        for (int k = i; k < end; k++) {
            const ar_pass *p = ch->gap[ch->keys[k].index];
            ar_push_unique(&ch->ys, run.first + run.ins, p->out, &run.outs);
            ch->run_of[ch->keys[k].index] = run.lo < run.hi ? acount(ch->runs) : -1;
        }
        if (run.lo < run.hi) {
            apush(ch->runs, run);
        }
        i = end;
    }
}

/* Routes one gap and returns its track count. */
static int ar_channel_route(ar_channel *ch)
{
    ar_channel_join(ch);
    ar_channel_runs(ch);
    int tracks = acount(ch->runs) ? ar_place_runs(ch->runs, acount(ch->runs), ch->ys)
                                  : 0;
    for (int i = 0; i < acount(ch->gap); i++) {
        if (ch->run_of[i] < 0) {
            continue;
        }
        const ar_run *run = &ch->runs[ch->run_of[i]];
        ar_pass *pass = ch->gap[i];
        pass->track = run->track;
        pass->jog = run->jog && pass->in == run->jog_at;
        pass->jog_track = run->jog_track;
        pass->jog_y = run->jog_y;
    }
    return tracks;
}

/* Channel routing: gives every pass that turns a track in its gap, so that
 * vertical runs never overlap and cross as little as the gap allows, and
 * counts each gap's tracks into `gap_tracks`. */
void ar_assign_tracks(ar_pass *passes, int count, int *gap_tracks)
{
    if (!count) {
        return;
    }
    ar_key *order = NULL;
    asetlen(order, count);
    for (int i = 0; i < count; i++) {
        order[i] = (ar_key){ (float)passes[i].gap, i };
        passes[i].track = -1;
    }
    qsort(order, (size_t)count, sizeof *order, ar_key_compare);
    ar_channel ch = { 0 };
    for (int first = 0; first < count;) {
        int gap = passes[order[first].index].gap;
        aclear(ch.gap);
        for (; first < count && passes[order[first].index].gap == gap; first++) {
            apush(ch.gap, &passes[order[first].index]);
        }
        gap_tracks[gap] = ar_channel_route(&ch);
    }
    afree(order);
    ar_channel_free(&ch);
}

/* The gap a column needs on its right: room for its tracks, or the plain
 * rank gap. */
float ar_gap_width(arrange_route_style style, int tracks)
{
    if (!ar_tracked(style) || tracks < 2) {
        return AR_RANK_GAP;
    }
    return math_max(AR_RANK_GAP, (float)(tracks - 1) * AR_TRACK_GAP + 2.0f * AR_TRACK_MARGIN);
}

/* Tracks spread evenly around the middle of the gap between `left` and
 * `right`, squeezed when the gap is narrower than they need. */
static float ar_track_x(float left, float right, float track, int tracks)
{
    float width = right - left;
    float spread = math_max(width - 2.0f * AR_TRACK_MARGIN, width * 0.5f);
    float spacing = tracks > 1
                      ? math_min(AR_TRACK_GAP, spread / (float)(tracks - 1))
                      : 0.0f;
    return (left + right) * 0.5f + (track - (float)(tracks - 1) * 0.5f) * spacing;
}

/* A lane's wire runs along its middle. */
static float ar_lane_y(const ar_ctx *c, int lane)
{
    return c->nodes[lane].y + AR_WIRE_LANE * 0.5f;
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

/* A level beside a box, clear of its column's other boxes and lanes: above
 * it when heading upward, below it otherwise. */
static float ar_beside(const arrange_graph *g, const arrange_point *pos, int box, float toward)
{
    float top = pos[box].y;
    float bottom = top + g->boxes[box].height;
    return toward < (top + bottom) * 0.5f ? top - AR_LOOP_CLEAR : bottom + AR_LOOP_CLEAR;
}

/* A backward wire leaves its output rightward and turns past its own box on
 * the side it heads for. It runs left through the lanes its columns reserve,
 * passes its target's box the same way, and enters its input from the left.
 * Its vertical runs sit just outside column edges, clear of the tracks. */
static void ar_emit_backward(
    arrange_routes *routes,
    const arrange_graph *g,
    const arrange_point *pos,
    const ar_ctx *c,
    const ar_lanes *lanes,
    int link,
    const int *column,
    const float *left,
    const float *right
)
{
    const arrange_link *l = &g->links[link];
    arrange_point start = ar_link_start(g, pos, l);
    arrange_point end = ar_link_end(pos, l);
    int from = column[l->from];
    int to = column[l->to];
    float edge = AR_TRACK_MARGIN * 0.5f;
    /* Lanes run from the target's side: lane k sits in column to + 1 + k. */
    int count = ar_lane_count(lanes, link);
    const int *lane = count ? lanes->lanes + lanes->start[link] : NULL;
    float y = ar_beside(g, pos, l->from, count ? ar_lane_y(c, lane[count - 1]) : end.y);
    float x = right[from] + edge;
    apush(routes->points, ((arrange_point){ x, start.y }));
    apush(routes->points, ((arrange_point){ x, y }));
    for (int k = count - 1; k >= 0; k--) {
        x = left[to + 2 + k] - edge;
        float level = ar_lane_y(c, lane[k]);
        apush(routes->points, ((arrange_point){ x, y }));
        apush(routes->points, ((arrange_point){ x, level }));
        y = level;
    }
    if (l->to != l->from) {
        float level = ar_beside(g, pos, l->to, y);
        x = left[from == to ? to : to + 1] - edge;
        apush(routes->points, ((arrange_point){ x, y }));
        apush(routes->points, ((arrange_point){ x, level }));
        y = level;
    }
    x = left[to] - edge;
    apush(routes->points, ((arrange_point){ x, y }));
    apush(routes->points, ((arrange_point){ x, end.y }));
}

/* The height a forward wire runs at in each column from its source's to its
 * target's: its pins at the ends, its lanes between. */
static void ar_levels(
    const arrange_graph *g,
    const arrange_point *pos,
    const ar_ctx *c,
    const ar_lanes *lanes,
    int link,
    float **levels
)
{
    const arrange_link *l = &g->links[link];
    aclear(*levels);
    apush(*levels, ar_link_start(g, pos, l).y);
    for (int k = 0; k < ar_lane_count(lanes, link); k++) {
        apush(*levels, ar_lane_y(c, lanes->lanes[lanes->start[link] + k]));
    }
    apush(*levels, ar_link_end(pos, l).y);
    /* Lanes are stored by their tops, so a level can come back a rounding
     * step off the pin it lines up with. */
    int n = acount(*levels);
    for (int k = 1; k < n - 1; k++) {
        if (ar_same_level((*levels)[k], (*levels)[k - 1])) {
            (*levels)[k] = (*levels)[k - 1];
        }
    }
    for (int k = n - 2; k > 0 && ar_same_level((*levels)[k], (*levels)[n - 1]); k--) {
        (*levels)[k] = (*levels)[n - 1];
    }
}

/* Curved and straight wires cross each column they skip along its lane, from
 * its left edge to its right. A fan-out's wires first share a short level
 * trunk into the gap. */
static void ar_emit_lanes(
    arrange_routes *routes,
    arrange_point start,
    arrange_point end,
    int from,
    bool trunk,
    const float *levels,
    const float *left,
    const float *right,
    arrange_point **knot
)
{
    int to = from + acount(levels) - 1;
    aclear(*knot);
    apush(*knot, start);
    float split = trunk ? math_min(AR_TRUNK, (left[from + 1] - right[from]) / 3.0f)
                        : 0.0f;
    if (start.x < right[from] + split) {
        apush(*knot, ((arrange_point){ right[from] + split, start.y }));
    }
    for (int k = from + 1; k < to; k++) {
        apush(*knot, ((arrange_point){ left[k], levels[k - from] }));
        apush(*knot, ((arrange_point){ right[k], levels[k - from] }));
    }
    if (end.x > left[to]) {
        apush(*knot, ((arrange_point){ left[to], end.y }));
    }
    apush(*knot, end);
    ar_emit_route(routes, *knot, acount(*knot));
}

/* An orthogonal or subway wire turns on its track in the gap between `left`
 * and `right`, after a dogleg when it has one. */
static void ar_emit_pass(arrange_routes *routes, const ar_pass *pass, float left, float right, int tracks)
{
    if (pass->track < 0) {
        return;
    }
    float in = pass->in;
    if (pass->jog) {
        float jog = ar_track_x(left, right, pass->jog_track, tracks);
        apush(routes->points, ((arrange_point){ jog, in }));
        apush(routes->points, ((arrange_point){ jog, pass->jog_y }));
        in = pass->jog_y;
    }
    float x = ar_track_x(left, right, (float)pass->track, tracks);
    apush(routes->points, ((arrange_point){ x, in }));
    apush(routes->points, ((arrange_point){ x, pass->out }));
}

/* Re-runs the layered layout with every box pinned where it is: columns stand
 * in for ranks, and only the corridors that carry long wires are ordered and
 * placed. On an arranged graph this finds the lanes the layout reserved. The
 * tracked styles then give every turn a track in its gap. */
void arrange_route(
    const arrange_graph *g,
    const arrange_point *pos,
    arrange_route_style style,
    arrange_routes *routes
)
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
    ar_lanes lanes = { 0 };
    ar_lanes_init(&lanes, g->box_count);
    for (int i = 0; i < g->link_count; i++) {
        const arrange_link *l = &g->links[i];
        if (column[l->from] == column[l->to]) {
            ar_lanes_skip(&lanes);
            continue;
        }
        int first = acount(c.edges);
        if (column[l->from] < column[l->to]) {
            ar_ctx_link(&c, &lanes, node_of[l->from], node_of[l->to], l->from_y, l->to_y, false);
        } else {
            ar_ctx_link(&c, &lanes, node_of[l->to], node_of[l->from], l->to_y, l->from_y, true);
        }
        ar_mark_flow(&c, first, l->flow);
    }
    ar_ctx_solve(&c);

    float *levels = NULL;
    ar_pass *passes = NULL;
    int *pass_start = NULL;
    int *gap_tracks = NULL;
    if (ar_tracked(style)) {
        asetlen(gap_tracks, acount(left));
        for (int i = 0; i < g->link_count; i++) {
            const arrange_link *l = &g->links[i];
            apush(pass_start, acount(passes));
            if (column[l->from] >= column[l->to] || l->custom_handles) {
                continue;
            }
            ar_levels(g, pos, &c, &lanes, i, &levels);
            for (int k = 0; k + 1 < acount(levels); k++) {
                apush(
                    passes,
                    ((ar_pass){ .gap = column[l->from] + k,
                                .in = levels[k],
                                .out = levels[k + 1] })
                );
            }
        }
        apush(pass_start, acount(passes));
        ar_assign_tracks(passes, acount(passes), gap_tracks);
    }

    /* Wires with authored handles, and forward wires within one column, keep
     * their own curve. Bands arrange apart, so one band's neighbouring ranks
     * can share a column where another band's wider box spans both. */
    arrange_point *knot = NULL;
    for (int i = 0; i < g->link_count; i++) {
        apush(routes->start, acount(routes->points));
        const arrange_link *l = &g->links[i];
        int from = column[l->from];
        int to = column[l->to];
        if (l->custom_handles) {
            continue;
        }
        if (from == to && ar_link_end(pos, l).x >= ar_link_start(g, pos, l).x) {
            continue;
        }
        if (from >= to) {
            ar_emit_backward(routes, g, pos, &c, &lanes, i, column, left, right);
            continue;
        }
        if (!ar_tracked(style)) {
            ar_levels(g, pos, &c, &lanes, i, &levels);
            bool trunk = ar_fan_out(&lanes, node_of[l->from], l->from_y);
            ar_emit_lanes(
                routes,
                ar_link_start(g, pos, l),
                ar_link_end(pos, l),
                from,
                trunk,
                levels,
                left,
                right,
                &knot
            );
            continue;
        }
        for (int p = pass_start[i]; p < pass_start[i + 1]; p++) {
            ar_emit_pass(
                routes,
                &passes[p],
                right[passes[p].gap],
                left[passes[p].gap + 1],
                gap_tracks[passes[p].gap]
            );
        }
    }
    apush(routes->start, acount(routes->points));
    afree(knot);
    afree(levels);
    afree(passes);
    afree(pass_start);
    afree(gap_tracks);
    afree(node_of);
    afree(left);
    afree(right);
    afree(column);
    ar_lanes_free(&lanes);
    ar_ctx_free(&c);
}

void arrange_routes_free(arrange_routes *routes)
{
    afree(routes->start);
    afree(routes->points);
}
