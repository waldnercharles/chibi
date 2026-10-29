#include "graph/authoring/arrange_score.h"

#include "graph/authoring/arrange_internal.h"
#include "support/array.h"
#include "support/math.h"

#include <stdlib.h>

#define AR_CURVE_SAMPLES 12
#define AR_COLLINEAR 0.5f
#define AR_OVERLAP_MIN 2.0f
#define AR_COST_CROSS 1.0f
#define AR_COST_HIT 4.0f
#define AR_COST_OVERLAP 40.0f
#define AR_COST_WIRE_OVERLAP 2.0f
#define AR_COST_BEND 0.25f
#define AR_COST_LENGTH 0.0008f

static bool ar_straight(const arrange_piece *p)
{
    return p->p1.x == p->p0.x && p->p1.y == p->p0.y && p->p2.x == p->p3.x &&
           p->p2.y == p->p3.y;
}

static arrange_piece ar_handles_of(const arrange_link *l, arrange_point from, arrange_point to)
{
    return (arrange_piece){ from,
                            { from.x + l->from_handle_x, from.y + l->from_handle_y },
                            { to.x + l->to_handle_x, to.y + l->to_handle_y },
                            to,
                            0 };
}

static arrange_point ar_curve_at(const arrange_piece *c, float t)
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

/* Where straight piece `b` lies along straight piece `a`, in distance from
 * a's start, when the two are collinear. */
static bool ar_collinear_span(const arrange_piece *a, const arrange_piece *b, float *lo, float *hi)
{
    arrange_point d = vec2_sub(a->p3, a->p0);
    float length = vec2_length(d);
    if (length < AR_COLLINEAR ||
        math_abs(ar_cross2(a->p0, a->p3, b->p0)) > AR_COLLINEAR * length ||
        math_abs(ar_cross2(a->p0, a->p3, b->p3)) > AR_COLLINEAR * length) {
        return false;
    }
    float t0 = vec2_dot(vec2_sub(b->p0, a->p0), d) / length;
    float t1 = vec2_dot(vec2_sub(b->p3, a->p0), d) / length;
    *lo = math_max(0.0f, math_min(t0, t1));
    *hi = math_min(length, math_max(t0, t1));
    return *hi > *lo;
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

/* Every wire as its style draws it: its pieces, and the polyline they
 * flatten to. */
typedef struct ar_drawing
{
    arrange_routes routes;
    arrange_piece *pieces;
    int *piece_start; /* per link, plus one */
    float *piece_length;
    arrange_point *points;
    ar_wire *wires;
} ar_drawing;

static void ar_drawing_free(ar_drawing *d)
{
    arrange_routes_free(&d->routes);
    afree(d->pieces);
    afree(d->piece_start);
    afree(d->piece_length);
    afree(d->points);
    afree(d->wires);
}

static void ar_sample(arrange_point **points, const arrange_piece *p)
{
    if (ar_straight(p)) {
        apush(*points, p->p3);
        return;
    }
    for (int i = 1; i <= AR_CURVE_SAMPLES; i++) {
        apush(*points, ar_curve_at(p, (float)i / AR_CURVE_SAMPLES));
    }
}

static void ar_draw(const arrange_graph *g, const arrange_point *pos, arrange_route_style style, ar_drawing *d)
{
    arrange_route(g, pos, style, &d->routes);
    for (int i = 0; i < g->link_count; i++) {
        const arrange_link *l = &g->links[i];
        arrange_point from = ar_link_start(g, pos, l);
        arrange_point to = ar_link_end(pos, l);
        int first = d->routes.start[i];
        apush(d->piece_start, acount(d->pieces));
        if (l->custom_handles) {
            apush(d->pieces, ar_handles_of(l, from, to));
        } else {
            int knot_count = d->routes.start[i + 1] - first;
            const arrange_point *knots = knot_count ? d->routes.points + first : NULL;
            arrange_path(style, from, to, knots, knot_count, &d->pieces);
        }
        ar_wire w = { .first = acount(d->points), .min = from, .max = from, .monotone = true };
        apush(d->points, from);
        for (int p = d->piece_start[i]; p < acount(d->pieces); p++) {
            int at = acount(d->points);
            ar_sample(&d->points, &d->pieces[p]);
            float length = 0.0f;
            for (int k = at; k < acount(d->points); k++) {
                arrange_point a = d->points[k - 1];
                arrange_point b = d->points[k];
                length += vec2_distance(a, b);
                w.min = vec2_min(w.min, b);
                w.max = vec2_max(w.max, b);
                w.monotone &= b.x >= a.x;
            }
            apush(d->piece_length, length);
            w.length += length;
        }
        w.count = acount(d->points) - w.first;
        apush(d->wires, w);
    }
    apush(d->piece_start, acount(d->pieces));
}

static arrange_point ar_intersection(arrange_point a0, arrange_point a1, arrange_point b0, arrange_point b1)
{
    arrange_point r = vec2_sub(a1, a0);
    arrange_point d = vec2_sub(b1, b0);
    return vec2_add(a0, vec2_scale(r, vec2_cross(vec2_sub(b0, a0), d) / vec2_cross(r, d)));
}

/* Counts the points where two wires cross, appending each to `at`; without
 * `at`, stops at the first. */
static int ar_wires_cross(
    const arrange_point *points,
    const ar_wire *a,
    const ar_wire *b,
    arrange_point **at
)
{
    if (!ar_boxes_overlap(a->min, a->max, b->min, b->max)) {
        return 0;
    }
    const arrange_point *pa = points + a->first;
    const arrange_point *pb = points + b->first;
    bool sweep = a->monotone && b->monotone;
    int first = 0;
    int count = 0;
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
            if (!ar_boxes_overlap(lo, hi, vec2_min(b0, b1), vec2_max(b0, b1)) ||
                !ar_segments_cross(a0, a1, b0, b1)) {
                continue;
            }
            if (!at) {
                return 1;
            }
            apush(*at, ar_intersection(a0, a1, b0, b1));
            count++;
        }
    }
    return count;
}

/* Whether straight runs of two wires lie on each other for more than a touch. */
static bool ar_wires_overlap(const ar_drawing *d, int i, int j)
{
    if (!ar_boxes_overlap(
            d->wires[i].min,
            d->wires[i].max,
            d->wires[j].min,
            d->wires[j].max
        )) {
        return false;
    }
    for (int p = d->piece_start[i]; p < d->piece_start[i + 1]; p++) {
        const arrange_piece *a = &d->pieces[p];
        if (!ar_straight(a)) {
            continue;
        }
        for (int q = d->piece_start[j]; q < d->piece_start[j + 1]; q++) {
            const arrange_piece *b = &d->pieces[q];
            float lo, hi;
            if (ar_straight(b) && ar_collinear_span(a, b, &lo, &hi) &&
                hi - lo > AR_OVERLAP_MIN) {
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

static bool ar_same_piece(const arrange_piece *a, const arrange_piece *b)
{
    return a->p0.x == b->p0.x && a->p0.y == b->p0.y && a->p1.x == b->p1.x &&
           a->p1.y == b->p1.y && a->p2.x == b->p2.x && a->p2.y == b->p2.y &&
           a->p3.x == b->p3.x && a->p3.y == b->p3.y;
}

/* Total length covered by spans, each stored as (start, end). */
static float ar_span_union(arrange_point *spans, int count)
{
    for (int i = 1; i < count; i++) {
        arrange_point span = spans[i];
        int j = i;
        while (j > 0 && spans[j - 1].x > span.x) {
            spans[j] = spans[j - 1];
            j--;
        }
        spans[j] = span;
    }
    float total = 0.0f;
    float lo = spans[0].x;
    float hi = spans[0].y;
    for (int i = 1; i < count; i++) {
        if (spans[i].x > hi) {
            total += hi - lo;
            lo = spans[i].x;
        }
        hi = math_max(hi, spans[i].y);
    }
    return total + hi - lo;
}

/* The drawn length of wire `i` that no earlier wire in `earlier` already
 * draws: identical curved pieces and collinear straight runs count once. */
static float ar_wire_ink(const ar_drawing *d, int i, const int *earlier, int earlier_count, arrange_point **spans)
{
    float ink = 0.0f;
    for (int p = d->piece_start[i]; p < d->piece_start[i + 1]; p++) {
        const arrange_piece *a = &d->pieces[p];
        bool straight = ar_straight(a);
        bool shared = false;
        aclear(*spans);
        for (int e = 0; e < earlier_count && !shared; e++) {
            int j = earlier[e];
            for (int q = d->piece_start[j]; q < d->piece_start[j + 1] && !shared; q++) {
                const arrange_piece *b = &d->pieces[q];
                float lo, hi;
                shared = !straight && ar_same_piece(a, b);
                if (straight && ar_straight(b) && ar_collinear_span(a, b, &lo, &hi)) {
                    apush(*spans, ((arrange_point){ lo, hi }));
                }
            }
        }
        float covered = acount(*spans) ? ar_span_union(*spans, acount(*spans)) : 0.0f;
        ink += shared ? 0.0f : math_max(d->piece_length[p] - covered, 0.0f);
    }
    return ink;
}

static bool ar_level(const arrange_piece *p)
{
    return p->p0.y == p->p3.y && p->p1.y == p->p0.y && p->p2.y == p->p0.y;
}

/* Every run of pieces that changes height turns twice. */
static int ar_bends(const ar_drawing *d, int i)
{
    int runs = 0;
    bool climbing = false;
    for (int p = d->piece_start[i]; p < d->piece_start[i + 1]; p++) {
        bool level = ar_level(&d->pieces[p]);
        runs += !level && !climbing;
        climbing = !level;
    }
    return 2 * runs;
}

static bool ar_share_pin(const arrange_link *a, const arrange_link *b)
{
    if (a->from == b->from && a->from_y == b->from_y) {
        return true;
    }
    return a->to == b->to && a->to_y == b->to_y;
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

/* Links that share a pin never cross or overlap; they join one group, whose
 * shared runs count once toward the ink. */
static float ar_ink(const ar_drawing *d, int *group, int count)
{
    if (!count) {
        return 0.0f;
    }
    ar_key *order = NULL;
    asetlen(order, count);
    for (int i = 0; i < count; i++) {
        order[i] = (ar_key){ (float)ar_find(group, i), i };
    }
    qsort(order, (size_t)count, sizeof *order, ar_key_compare);
    int *earlier = NULL;
    arrange_point *spans = NULL;
    float ink = 0.0f;
    for (int k = 0; k < count; k++) {
        if (k && order[k].key != order[k - 1].key) {
            aclear(earlier);
        }
        ink += ar_wire_ink(d, order[k].index, earlier, acount(earlier), &spans);
        apush(earlier, order[k].index);
    }
    afree(order);
    afree(earlier);
    afree(spans);
    return ink;
}

/* A point where wires, or once grouped their groups, `a` and `b` cross. */
typedef struct ar_crossing
{
    int a;
    int b;
    arrange_point at;
} ar_crossing;

static int ar_crossing_compare(const void *l, const void *r)
{
    const ar_crossing *x = l;
    const ar_crossing *y = r;
    if (x->a != y->a) {
        return x->a < y->a ? -1 : 1;
    }
    if (x->b != y->b) {
        return x->b < y->b ? -1 : 1;
    }
    if (x->at.x != y->at.x) {
        return x->at.x < y->at.x ? -1 : 1;
    }
    return (x->at.y > y->at.y) - (x->at.y < y->at.y);
}

/* Crossings as drawn: wires that share a pin run together, so two groups
 * crossing at one point count once however many wires each carries. */
static int ar_distinct_crossings(ar_crossing *crossings, int count, int *group)
{
    for (int i = 0; i < count; i++) {
        int a = ar_find(group, crossings[i].a);
        int b = ar_find(group, crossings[i].b);
        crossings[i].a = math_min(a, b);
        crossings[i].b = math_max(a, b);
    }
    if (count) {
        qsort(crossings, (size_t)count, sizeof *crossings, ar_crossing_compare);
    }
    int distinct = 0;
    const ar_crossing *kept = NULL;
    for (int i = 0; i < count; i++) {
        const ar_crossing *c = &crossings[i];
        if (kept && kept->a == c->a && kept->b == c->b &&
            math_abs(kept->at.x - c->at.x) < AR_COLLINEAR &&
            math_abs(kept->at.y - c->at.y) < AR_COLLINEAR) {
            continue;
        }
        kept = c;
        distinct++;
    }
    return distinct;
}

arrange_score arrange_measure(const arrange_graph *g, const arrange_point *pos, arrange_route_style style)
{
    arrange_score score = { 0 };
    ar_drawing d = { 0 };
    ar_draw(g, pos, style, &d);
    int *group = NULL;
    asetlen(group, g->link_count);
    for (int i = 0; i < g->link_count; i++) {
        group[i] = i;
    }
    arrange_point *at = NULL;
    ar_crossing *crossings = NULL;
    for (int i = 0; i < g->link_count; i++) {
        score.wire_length += d.wires[i].length;
        int bends = ar_bends(&d, i);
        score.wire_bends += bends;
        score.flow_bends += g->links[i].flow ? bends : 0;
        for (int j = i + 1; j < g->link_count; j++) {
            if (ar_share_pin(&g->links[i], &g->links[j])) {
                ar_union(group, i, j);
                continue;
            }
            aclear(at);
            int count = ar_wires_cross(d.points, &d.wires[i], &d.wires[j], &at);
            for (int k = 0; k < count; k++) {
                apush(crossings, ((ar_crossing){ i, j, at[k] }));
            }
            score.wire_overlaps += ar_wires_overlap(&d, i, j);
        }
        for (int b = 0; b < g->box_count; b++) {
            if (ar_link_touches(&g->links[i], b)) {
                continue;
            }
            arrange_point min, max;
            ar_box_bounds(g, pos, b, &min, &max);
            if (ar_wire_hits_box(d.points, &d.wires[i], min, max)) {
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
    score.wire_crossings = ar_distinct_crossings(crossings, acount(crossings), group);
    score.ink_length = ar_ink(&d, group, g->link_count);
    afree(at);
    afree(crossings);
    afree(group);
    ar_drawing_free(&d);
    return score;
}
