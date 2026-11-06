#include "graph/authoring/arrange_path.h"

#include "graph/authoring/arrange_internal.h"
#include "support/array.h"
#include "support/math.h"

#include <assert.h>

#define AR_CORNER_RADIUS 10.0f
#define AR_LOOP_RADIUS 24.0f /* corners of a curved backward wire */
#define AR_KAPPA                                                               \
    0.5523f             /* quarter-circle handle, as a fraction of the radius */
#define AR_PIN_RUN 8.0f /* shortest level run at a pin before a corner */
#define AR_STUB 16.0f   /* level run at the pins of straight wires and loops */
/* Subway diagonals reach no further along a run than this, which keeps them
 * inside the track margin of their gap. */
#define AR_SUBWAY_REACH 24.0f
#define AR_SUBWAY_KINK 8.0f
/* A drawn pin this close to the height its route was planned for slides the
 * vertical run beside it rather than adding a jog. */
#define AR_PIN_SNAP 12.0f
#define AR_BEND_MIN 40.0f

bool ar_tracked(arrange_route_style style)
{
    return style == ARRANGE_ROUTE_ORTHOGONAL || style == ARRANGE_ROUTE_SUBWAY;
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
