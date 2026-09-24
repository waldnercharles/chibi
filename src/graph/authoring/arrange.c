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

/* Orthogonal and subway wires turn on tracks this far apart, with the outer
 * tracks this far from the columns. Vertical runs closer than the clearance
 * need tracks of their own. */
#define AR_TRACK_GAP 12.0f
#define AR_TRACK_MARGIN 24.0f
#define AR_TRACK_CLEAR 1.0f
/* A track order that lays one wire's run over another's costs as much as
 * this many crossings. */
#define AR_TRACK_OVERLAP_COST 64
#define AR_CORNER_RADIUS 10.0f
#define AR_LOOP_RADIUS 24.0f /* corners of a curved backward wire */
#define AR_KAPPA                                                               \
    0.5523f             /* quarter-circle handle, as a fraction of the radius */
#define AR_PIN_RUN 8.0f /* shortest level run at a pin before a corner */
#define AR_STUB 16.0f   /* level run at the pins of straight wires and loops */
#define AR_TRUNK 24.0f  /* level run curved and straight fan-outs share */
/* Subway diagonals reach no further along a run than this, which keeps them
 * inside the track margin of their gap. */
#define AR_SUBWAY_REACH 24.0f
#define AR_SUBWAY_KINK 8.0f
#define AR_LOOP_CLEAR 18.0f
/* A drawn pin this close to the height its route was planned for slides the
 * vertical run beside it rather than adding a jog. */
#define AR_PIN_SNAP 12.0f
#define AR_LEVEL_SNAP 0.5f /* heights closer than this are one level */

#define AR_BEND_MIN 40.0f
#define AR_CURVE_SAMPLES 12
#define AR_COLLINEAR 0.5f
#define AR_OVERLAP_MIN 2.0f
#define AR_COST_CROSS 1.0f
#define AR_COST_HIT 4.0f
#define AR_COST_OVERLAP 40.0f
#define AR_COST_WIRE_OVERLAP 2.0f
#define AR_COST_BEND 0.25f
#define AR_COST_LENGTH 0.0008f

#define AR_FAN_CROSS_WEIGHT 2

#define AR_ORDER_SWEEPS 8
#define AR_TRANSPOSE_PASSES 4
#define AR_COORD_SWEEPS 8
#define AR_TIGHTEN_PASSES 8
#define AR_STRAIGHTEN_PASSES 4

#define AR_FREE 1.0e9f
#define AR_CORRIDOR_PRIORITY 1000000

static arrange_point ar_link_start(const arrange_graph *g, const arrange_point *pos, const arrange_link *l)
{
    return (arrange_point){ pos[l->from].x + g->boxes[l->from].width,
                            pos[l->from].y + l->from_y };
}

static arrange_point ar_link_end(const arrange_point *pos, const arrange_link *l)
{
    return (arrange_point){ pos[l->to].x, pos[l->to].y + l->to_y };
}

static bool ar_tracked(arrange_route_style style)
{
    return style == ARRANGE_ROUTE_ORTHOGONAL || style == ARRANGE_ROUTE_SUBWAY;
}

static bool ar_straight(const arrange_piece *p)
{
    return p->p1.x == p->p0.x && p->p1.y == p->p0.y && p->p2.x == p->p3.x &&
           p->p2.y == p->p3.y;
}

static void ar_push_line(arrange_piece **pieces, arrange_point a, arrange_point b, int segment)
{
    if (a.x != b.x || a.y != b.y) {
        apush(*pieces, ((arrange_piece){ a, a, b, b, segment }));
    }
}

/* A wire without waypoints draws as one cubic whose handles grow with its
 * horizontal span. */
static arrange_piece ar_curve_of(arrange_point from, arrange_point to)
{
    float bend = math_max(AR_BEND_MIN, math_abs(to.x - from.x) * 0.5f);
    return (arrange_piece){ from, { from.x + bend, from.y }, { to.x - bend, to.y }, to, 0 };
}

static arrange_piece ar_handles_of(const arrange_link *l, arrange_point from, arrange_point to)
{
    return (arrange_piece){ from,
                            { from.x + l->from_handle_x, from.y + l->from_handle_y },
                            { to.x + l->to_handle_x, to.y + l->to_handle_y },
                            to,
                            0 };
}

/* A curved wire crosses every knot horizontally, with handles no longer than
 * half the shorter neighbouring stretch. Level stretches draw straight, so
 * they do not bound their neighbours' handles. */
static float ar_route_tangent(const arrange_point *knot, int count, int i, float dir)
{
    float span = AR_FREE;
    if (i > 0 && knot[i - 1].y != knot[i].y) {
        span = math_abs(knot[i].x - knot[i - 1].x);
    }
    if (i + 1 < count && knot[i + 1].y != knot[i].y) {
        span = math_min(span, math_abs(knot[i + 1].x - knot[i].x));
    }
    return dir * math_min(AR_BEND_MIN, span * 0.5f);
}

static void ar_path_curved(const arrange_point *knot, int count, arrange_piece **pieces)
{
    if (count == 2 && (knot[0].y != knot[1].y || knot[1].x < knot[0].x)) {
        apush(*pieces, ar_curve_of(knot[0], knot[1]));
        return;
    }
    float dir = knot[count - 1].x >= knot[0].x ? 1.0f : -1.0f;
    for (int k = 0; k + 1 < count; k++) {
        arrange_point a = knot[k];
        arrange_point b = knot[k + 1];
        if (a.y == b.y) {
            ar_push_line(pieces, a, b, k);
            continue;
        }
        apush(
            *pieces,
            ((arrange_piece){ a,
                              { a.x + ar_route_tangent(knot, count, k, dir), a.y },
                              { b.x - ar_route_tangent(knot, count, k + 1, dir), b.y },
                              b,
                              k })
        );
    }
}

/* A straight wire leaves and enters its pins level for a short stub. */
static void ar_path_straight(const arrange_point *knot, int count, arrange_piece **pieces)
{
    for (int k = 0; k + 1 < count; k++) {
        arrange_point a = knot[k];
        arrange_point b = knot[k + 1];
        float stub = math_min(AR_STUB, math_abs(b.x - a.x) * 0.25f);
        if (a.y != b.y && k == 0) {
            arrange_point out = { a.x + stub, a.y };
            ar_push_line(pieces, a, out, k);
            a = out;
        }
        if (a.y != b.y && k + 2 == count) {
            arrange_point in = { b.x - stub, b.y };
            ar_push_line(pieces, a, in, k);
            a = in;
        }
        ar_push_line(pieces, a, b, k);
    }
}

/* Appends a corner whose outgoing run lies on `segment`, folding repeats and
 * straight continuations into the previous corner. */
static void ar_corner_push(arrange_point **corner, int **segment, arrange_point p, int s)
{
    int n = acount(*corner);
    arrange_point last = (*corner)[n - 1];
    if (p.x == last.x && p.y == last.y) {
        (*segment)[n - 1] = s;
        return;
    }
    if (n >= 2) {
        arrange_point prev = (*corner)[n - 2];
        arrange_point in = vec2_sub(last, prev);
        arrange_point out = vec2_sub(p, last);
        if (vec2_cross(in, out) == 0.0f && vec2_dot(in, out) > 0.0f) {
            (*corner)[n - 1] = p;
            (*segment)[n - 1] = s;
            return;
        }
    }
    apush(*corner, p);
    apush(*segment, s);
}

/* The corners of a path of level and vertical runs through the knots. A
 * stretch that is neither turns once at mid-width. */
static void ar_corners(const arrange_point *knot, int count, arrange_point **corner, int **segment)
{
    apush(*corner, knot[0]);
    apush(*segment, 0);
    for (int k = 0; k + 1 < count; k++) {
        arrange_point a = knot[k];
        arrange_point b = knot[k + 1];
        if (a.x != b.x && a.y != b.y) {
            float mid = (a.x + b.x) * 0.5f;
            ar_corner_push(corner, segment, (arrange_point){ mid, a.y }, k);
            ar_corner_push(corner, segment, (arrange_point){ mid, b.y }, k);
        }
        ar_corner_push(corner, segment, b, k + 1);
    }
}

/* How far corner k's rounding or diagonal reaches along both of its runs. A
 * run between two corners gives each half; a run from a pin keeps a short
 * level stretch. Reversals stay sharp. */
static float ar_corner_cut(arrange_route_style style, float radius, const arrange_point *p, int n, int k)
{
    arrange_point in = vec2_sub(p[k], p[k - 1]);
    arrange_point out = vec2_sub(p[k + 1], p[k]);
    if (vec2_dot(in, out) != 0.0f) {
        return 0.0f;
    }
    float in_len = vec2_length(in);
    float out_len = vec2_length(out);
    float in_room = k == 1 ? in_len - math_min(AR_PIN_RUN, in_len * 0.5f)
                           : in_len * 0.5f;
    float out_room = k + 2 == n ? out_len - math_min(AR_PIN_RUN, out_len * 0.5f)
                                : out_len * 0.5f;
    float reach = radius;
    if (style == ARRANGE_ROUTE_SUBWAY) {
        /* A vertical run barely longer than two diagonals becomes one. */
        float vertical = in.x == 0.0f ? in_room : out_room;
        reach = vertical - AR_SUBWAY_REACH < AR_SUBWAY_KINK ? vertical : AR_SUBWAY_REACH;
    }
    return math_min(reach, math_min(in_room, out_room));
}

/* Orthogonal corners round into quarter circles; subway corners cut across
 * at 45 degrees, so a short vertical run becomes a single diagonal. */
static void ar_path_corners(
    arrange_route_style style,
    float radius,
    const arrange_point *p,
    const int *segment,
    int n,
    arrange_piece **pieces
)
{
    float cut_in = 0.0f;
    for (int k = 0; k + 1 < n; k++) {
        arrange_point dir = vec2_safe_norm(vec2_sub(p[k + 1], p[k]));
        float cut = k + 2 < n ? ar_corner_cut(style, radius, p, n, k + 1) : 0.0f;
        arrange_point a = vec2_add(p[k], vec2_scale(dir, cut_in));
        arrange_point b = vec2_sub(p[k + 1], vec2_scale(dir, cut));
        ar_push_line(pieces, a, b, segment[k]);
        cut_in = cut;
        if (cut <= 0.0f) {
            continue;
        }
        arrange_point out = vec2_safe_norm(vec2_sub(p[k + 2], p[k + 1]));
        arrange_point c = vec2_add(p[k + 1], vec2_scale(out, cut));
        if (style == ARRANGE_ROUTE_SUBWAY) {
            ar_push_line(pieces, b, c, segment[k]);
            continue;
        }
        apush(
            *pieces,
            ((arrange_piece){ b,
                              vec2_add(b, vec2_scale(dir, cut * AR_KAPPA)),
                              vec2_sub(c, vec2_scale(out, cut * AR_KAPPA)),
                              c,
                              segment[k] })
        );
    }
}

void arrange_path(
    arrange_route_style style,
    arrange_point from,
    arrange_point to,
    const arrange_point *knots,
    int count,
    arrange_piece **pieces
)
{
    arrange_point *knot = NULL;
    apush(knot, from);
    for (int i = 0; i < count; i++) {
        apush(knot, knots[i]);
    }
    /* Without waypoints, a backward wire in the angular styles leaves its
     * output, doubles back at mid-height, and enters its input. */
    if (style != ARRANGE_ROUTE_CURVED && !count && to.x < from.x) {
        float mid = (from.y + to.y) * 0.5f;
        apush(knot, ((arrange_point){ from.x + AR_STUB, from.y }));
        apush(knot, ((arrange_point){ from.x + AR_STUB, mid }));
        apush(knot, ((arrange_point){ to.x - AR_STUB, mid }));
        apush(knot, ((arrange_point){ to.x - AR_STUB, to.y }));
    }
    apush(knot, to);
    int n = acount(knot);
    if (ar_tracked(style) && count >= 2) {
        if (knot[1].x == knot[2].x && math_abs(knot[1].y - from.y) <= AR_PIN_SNAP) {
            knot[1].y = from.y;
        }
        if (knot[n - 2].x == knot[n - 3].x &&
            math_abs(knot[n - 2].y - to.y) <= AR_PIN_SNAP) {
            knot[n - 2].y = to.y;
        }
    }
    /* A curved backward wire with waypoints loops through them on round
     * corners. */
    arrange_route_style shape = style == ARRANGE_ROUTE_CURVED && count && to.x < from.x
                                  ? ARRANGE_ROUTE_ORTHOGONAL
                                  : style;
    float radius = style == ARRANGE_ROUTE_CURVED ? AR_LOOP_RADIUS : AR_CORNER_RADIUS;
    switch (shape) {
        case ARRANGE_ROUTE_CURVED: ar_path_curved(knot, n, pieces); break;
        case ARRANGE_ROUTE_STRAIGHT: ar_path_straight(knot, n, pieces); break;
        case ARRANGE_ROUTE_ORTHOGONAL:
        case ARRANGE_ROUTE_SUBWAY: {
            arrange_point *corner = NULL;
            int *segment = NULL;
            ar_corners(knot, n, &corner, &segment);
            ar_path_corners(shape, radius, corner, segment, acount(corner), pieces);
            afree(corner);
            afree(segment);
            break;
        }
        case ARRANGE_ROUTE_STYLE_COUNT: assert(!"invalid route style"); break;
    }
    afree(knot);
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
            arrange_path(
                style,
                from,
                to,
                d->routes.points + first,
                d->routes.start[i + 1] - first,
                &d->pieces
            );
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

float arrange_cost(arrange_score score)
{
    return AR_COST_CROSS * (float)score.wire_crossings +
           AR_COST_HIT * (float)score.wire_node_hits +
           AR_COST_OVERLAP * (float)score.node_overlaps +
           AR_COST_WIRE_OVERLAP * (float)score.wire_overlaps +
           AR_COST_BEND * (float)score.wire_bends + AR_COST_LENGTH * score.ink_length;
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
    bool back; /* part of a wire drawn right to left */
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
 * pulls. Between two middle pulls it follows the one on its higher pin, so
 * execution chains, whose pins come first, run straight. */
static bool ar_neighbor_median_y(ar_ctx *c, int node, bool forward, float *out)
{
    aclear(c->pulls);
    if (forward) {
        for (int i = c->in_start[node]; i < c->in_start[node + 1]; i++) {
            const ar_ledge *e = &c->edges[c->in_edge[i]];
            apush(
                c->pulls,
                ((arrange_point){ c->nodes[e->from].y + e->from_anchor - e->to_anchor,
                                  e->to_anchor })
            );
        }
    } else {
        for (int i = c->out_start[node]; i < c->out_start[node + 1]; i++) {
            const ar_ledge *e = &c->edges[c->out_edge[i]];
            apush(
                c->pulls,
                ((arrange_point){ c->nodes[e->to].y + e->to_anchor - e->from_anchor,
                                  e->from_anchor })
            );
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

static void ar_lanes_init(ar_lanes *w, int box_count)
{
    asetlen(w->head, box_count);
    for (int i = 0; i < box_count; i++) {
        w->head[i] = -1;
    }
}

static void ar_lanes_free(ar_lanes *w)
{
    afree(w->start);
    afree(w->lanes);
    afree(w->head);
    afree(w->next);
    afree(w->out);
}

static int ar_lane_count(const ar_lanes *w, int wire)
{
    int end = wire + 1 < acount(w->start) ? w->start[wire + 1] : acount(w->lanes);
    return end - w->start[wire];
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

static bool ar_same_level(float a, float b)
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
static void ar_ctx_link(ar_ctx *c, ar_lanes *w, int src, int dst, float src_anchor, float dst_anchor, bool back)
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
        apush(c->edges, (ar_ledge){ prev, corridor, prev_anchor, AR_WIRE_LANE * 0.5f, back });
        apush(w->lanes, corridor);
        prev = corridor;
        prev_anchor = AR_WIRE_LANE * 0.5f;
    }
    apush(c->edges, (ar_ledge){ prev, dst, prev_anchor, dst_anchor, back });
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

static int ar_find(int *parent, int i)
{
    while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

static void ar_union(int *parent, int a, int b)
{
    a = ar_find(parent, a);
    b = ar_find(parent, b);
    if (a != b) {
        parent[a] = b;
    }
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
static void ar_assign_tracks(ar_pass *passes, int count, int *gap_tracks)
{
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
static float ar_gap_width(arrange_route_style style, int tracks)
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
        ar_ctx_link(
            &c,
            &lanes,
            edge_src[i],
            edge_dst[i],
            back ? l->to_y : l->from_y,
            back ? l->from_y : l->to_y,
            back
        );
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
                    ((ar_pass){ c.nodes[e->from].rank,
                                c.nodes[e->from].y + e->from_anchor,
                                c.nodes[e->to].y + e->to_anchor })
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
    const int *lane = lanes->lanes + lanes->start[link];
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
        if (column[l->from] < column[l->to]) {
            ar_ctx_link(&c, &lanes, node_of[l->from], node_of[l->to], l->from_y, l->to_y, false);
        } else {
            ar_ctx_link(&c, &lanes, node_of[l->to], node_of[l->from], l->to_y, l->from_y, true);
        }
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
                apush(passes, ((ar_pass){ column[l->from] + k, levels[k], levels[k + 1] }));
            }
        }
        apush(pass_start, acount(passes));
        ar_assign_tracks(passes, acount(passes), gap_tracks);
    }

    /* Wires with authored handles, and curved backward wires, keep their own
     * curve. */
    arrange_point *knot = NULL;
    for (int i = 0; i < g->link_count; i++) {
        apush(routes->start, acount(routes->points));
        const arrange_link *l = &g->links[i];
        int from = column[l->from];
        int to = column[l->to];
        if (l->custom_handles) {
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
    qsort(components, (size_t)acount(components), sizeof *components, ar_key_compare);

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

/* Links that share a pin never cross or overlap; they join one group, whose
 * shared runs count once toward the ink. */
static float ar_ink(const ar_drawing *d, int *group, int count)
{
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
        score.wire_bends += ar_bends(&d, i);
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

void arrange_long_links(
    const arrange_graph *g,
    const arrange_point *pos,
    arrange_route_style style,
    int min_crossings,
    int **links
)
{
    if (!g->box_count) {
        return;
    }
    int *column = NULL;
    float *left = NULL;
    float *right = NULL;
    asetlen(column, g->box_count);
    ar_columns(g, pos, column, &left, &right);
    ar_drawing d = { 0 };
    ar_draw(g, pos, style, &d);
    int *crossings = NULL;
    asetlen(crossings, g->link_count);
    for (int i = 0; i < g->link_count; i++) {
        crossings[i] = 0;
    }
    for (int i = 0; i < g->link_count; i++) {
        for (int j = i + 1; j < g->link_count; j++) {
            if (!ar_share_pin(&g->links[i], &g->links[j]) &&
                ar_wires_cross(d.points, &d.wires[i], &d.wires[j], NULL)) {
                crossings[i]++;
                crossings[j]++;
            }
        }
    }
    for (int i = 0; i < g->link_count; i++) {
        const arrange_link *l = &g->links[i];
        if (abs(column[l->to] - column[l->from]) >= ARRANGE_LONG_COLUMNS ||
            crossings[i] >= min_crossings) {
            apush(*links, i);
        }
    }
    afree(crossings);
    ar_drawing_free(&d);
    afree(column);
    afree(left);
    afree(right);
}
