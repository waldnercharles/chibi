/* <line-chart> (controls.h): live line charts after Liveline, with Recharts'
 * axes. C publishes series by name (line_chart_publish), as the editor names
 * its textures "kit:...", and a chart draws the series its `series`
 * attribute names; publishing redraws the charts that name it. One element
 * draws every series as geometry (no element per point). The value range
 * follows the visible data with a 12% margin and eases toward it frame-rate
 * independently (factor 1 - (1 - 0.15)^(dt / 16.67 ms)); curves are
 * monotone cubic (Fritsch-Carlson), so they never overshoot a sample. Five
 * "nice" ticks label the grid, a badge carries the newest value, the newest
 * point pulses every 1.5 s, and hovering scrubs a crosshair with the value
 * under it. Labels move by transform and change their glyphs in place, so
 * new samples lay nothing out.
 *
 * math_min and math_max return their second argument on a tie or NaN, so the
 * value to keep in that case comes second. */
#include "support/array.h"
#include "support/math.h"
#include "support/xalloc.h"
#include "ui/controls/controls_internal.h"
#include "ui/motion.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MARGIN 0.12f
#define RANGE_SPEED 0.15
/* The share of the span within which the eased range snaps to its target. */
#define RANGE_SETTLED 1e-4
#define TICKS 5
#define DISC_SIDES 24
#define VALUE_TEXT 64
#define SERIES_MAX 8
#define NAME_TEXT 64
#define UNIT_TEXT 16

/* A published series: the registry's copy of what C gave. */
typedef struct published
{
    char *name;
    float *times, *values; /* support/array.h */
    uint32_t color;
    double now;
    uint64_t revision;
} published;

typedef struct line_chart
{
    rml_element *el, *badge, *hover;
    rml_element **ticks; /* tick labels, top to bottom */
    rml_geometry *geometry;
    /* From the attributes. */
    char names[SERIES_MAX][NAME_TEXT];
    int count;
    char unit[UNIT_TEXT];
    int precision;
    double window, reference, min, max;
    bool fill, live, dirty;
    /* What the last update showed. */
    uint64_t revision; /* the sum of its series' revisions */
    double *values;    /* tick values, top to bottom */
    double now, lo, hi, time, hover_value;
    float dp, pointer, hover_x; /* dp from the left; -1 off the chart */
    bool ranged, reduced;
    /* The geometry: built for these colors, or to build again. */
    bool built;
    uint32_t line_color, grid_color;
} line_chart;

typedef struct mesh
{
    rml_vertex *vertices;
    int *indices;
} mesh;

static published *registry;
static uint64_t revisions;
static line_chart **charts; /* every live chart, to redraw on publish */
static rml_listener *listener;

/* ---------- The registry ---------- */

static published *find_series(const char *name)
{
    for (int i = 0; i < acount(registry); i++) {
        if (!strcmp(registry[i].name, name)) {
            return &registry[i];
        }
    }
    return NULL;
}

static bool same_samples(const published *p, const line_chart_series *s)
{
    return p->color == s->color && p->now == s->now && acount(p->times) == s->count &&
           (!s->count ||
            !memcmp(p->times, s->times, (size_t)s->count * sizeof *s->times)) &&
           (!s->count ||
            !memcmp(p->values, s->values, (size_t)s->count * sizeof *s->values));
}

static void copy_floats(float **out, const float *in, int count)
{
    asetlen(*out, count);
    if (count) {
        memcpy(*out, in, (size_t)count * sizeof *in);
    }
}

static bool names_series(const line_chart *c, const char *name)
{
    for (int i = 0; i < c->count; i++) {
        if (!strcmp(c->names[i], name)) {
            return true;
        }
    }
    return false;
}

static void forget(published *p)
{
    free(p->name);
    afree(p->times);
    afree(p->values);
    adel(registry, (int)(p - registry));
}

void line_chart_publish(const char *name, const line_chart_series *series)
{
    published *p = find_series(name);
    if (!series) {
        if (p) {
            forget(p);
        }
    } else if (!p || !same_samples(p, series)) {
        if (!p) {
            apush(registry, ((published){ .name = xstrdup(name) }));
            p = &alast(registry);
        }
        copy_floats(&p->times, series->times, series->count);
        copy_floats(&p->values, series->values, series->count);
        p->color = series->color;
        p->now = series->now;
        p->revision = ++revisions;
    } else {
        return;
    }
    for (int i = 0; i < acount(charts); i++) {
        if (names_series(charts[i], name)) {
            control_redraw(charts[i]->el);
        }
    }
}

/* ---------- Geometry ---------- */

static uint8_t channel(uint32_t rgba, int shift, float a)
{
    return (uint8_t)lroundf((float)((rgba >> shift) & 255) * a);
}

static rml_color premultiply(uint32_t rgba, float alpha)
{
    float a = ((float)(rgba & 255) / 255.0f) * alpha;
    return (rml_color){ channel(rgba, 24, a),
                        channel(rgba, 16, a),
                        channel(rgba, 8, a),
                        (uint8_t)lroundf(a * 255) };
}

static uint32_t pack(rml_color c)
{
    return (uint32_t)c.r << 24 | (uint32_t)c.g << 16 | (uint32_t)c.b << 8 | c.a;
}

static void vertex(mesh *m, vec2 p, rml_color color)
{
    apush(m->vertices, (rml_vertex){ { p.x, p.y }, color, { 0, 0 } });
}

static void quad(mesh *m, vec2 a, vec2 b, vec2 c, vec2 d, rml_color ca, rml_color cb)
{
    static const int order[] = { 0, 1, 2, 0, 2, 3 };
    int base = acount(m->vertices);
    vertex(m, a, ca);
    vertex(m, b, ca);
    vertex(m, c, cb);
    vertex(m, d, cb);
    for (int i = 0; i < 6; i++) {
        apush(m->indices, base + order[i]);
    }
}

static void segment(mesh *m, vec2 p, vec2 q, float width, rml_color color)
{
    vec2 d = vec2_sub(q, p);
    float length = sqrtf(d.x * d.x + d.y * d.y);
    if (length < 1e-4f) {
        return;
    }
    vec2 n = { -d.y / length * width / 2, d.x / length * width / 2 };
    /* Extend by half the width so consecutive segments overlap at joins. */
    vec2 e = { d.x / length * width / 2, d.y / length * width / 2 };
    quad(
        m,
        vec2_add(vec2_sub(p, e), n),
        vec2_add(vec2_add(q, e), n),
        vec2_sub(vec2_add(q, e), n),
        vec2_sub(vec2_sub(p, e), n),
        color,
        color
    );
}

static void disc(mesh *m, vec2 c, float inner, float outer, rml_color color)
{
    for (int i = 0; i < DISC_SIDES; i++) {
        float a0 = 2 * MATH_PI_F * i / DISC_SIDES,
              a1 = 2 * MATH_PI_F * (i + 1) / DISC_SIDES;
        vec2 u0 = { cosf(a0), sinf(a0) }, u1 = { cosf(a1), sinf(a1) };
        quad(
            m,
            vec2_add(c, vec2_scale(u0, inner)),
            vec2_add(c, vec2_scale(u1, inner)),
            vec2_add(c, vec2_scale(u1, outer)),
            vec2_add(c, vec2_scale(u0, outer)),
            color,
            color
        );
    }
}

/* Monotone cubic tangents (Fritsch-Carlson) for two or more x-ascending
 * points. */
static float *monotone_tangents(const vec2 *p)
{
    int n = acount(p);
    assert(n >= 2);
    float *m = NULL, *slope = NULL;
    asetlen(m, n);
    asetlen(slope, n - 1);
    for (int i = 0; i + 1 < n; i++) {
        float dx = p[i + 1].x - p[i].x;
        slope[i] = dx > 0 ? (p[i + 1].y - p[i].y) / dx : 0;
    }
    m[0] = slope[0];
    m[n - 1] = slope[n - 2];
    for (int i = 1; i + 1 < n; i++) {
        m[i] = slope[i - 1] * slope[i] <= 0 ? 0 : (slope[i - 1] + slope[i]) / 2;
    }
    for (int i = 0; i + 1 < n; i++) {
        if (slope[i] == 0) {
            m[i] = m[i + 1] = 0;
            continue;
        }
        float a = m[i] / slope[i], b = m[i + 1] / slope[i];
        float h = a * a + b * b;
        if (h > 9) {
            float t = 3 / sqrtf(h);
            m[i] = t * a * slope[i];
            m[i + 1] = t * b * slope[i];
        }
    }
    afree(slope);
    return m;
}

static float y_of(const line_chart *c, float height, double v)
{
    return (float)(height - (v - c->lo) / (c->hi - c->lo) * height);
}

static float x_of(const line_chart *c, float width, double t)
{
    return (float)((t - (c->now - c->window)) / c->window * width);
}

/* At most two points per pixel column: keep each column's peak. */
static vec2 *series_points(const line_chart *c, const published *series, vec2 size)
{
    float s = c->dp;
    vec2 *points = NULL;
    float column = -1;
    for (int i = 0; i < acount(series->times); i++) {
        float x = x_of(c, size.x, series->times[i]);
        if (x < -2 * s) {
            continue;
        }
        float y = y_of(c, size.y, series->values[i]);
        if (acount(points) && x - column < 0.5f * s) {
            alast(points).y = math_min(y, alast(points).y);
            continue;
        }
        column = x;
        apush(points, (vec2){ x, y });
    }
    return points;
}

static vec2 *monotone_curve(const vec2 *points, float s)
{
    float *m = monotone_tangents(points);
    vec2 *curve = NULL;
    for (int i = 0; i + 1 < acount(points); i++) {
        vec2 a = points[i], b = points[i + 1];
        float dx = b.x - a.x;
        int steps = math_clamp((int)(dx / (3 * s)), 1, 8);
        for (int k = 0; k < steps; k++) {
            float t = (float)k / steps, t2 = t * t, t3 = t2 * t;
            float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t,
                  h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
            apush(
                curve,
                (vec2){ a.x + dx * t,
                        h00 * a.y + h10 * dx * m[i] + h01 * b.y + h11 * dx * m[i + 1] }
            );
        }
    }
    apush(curve, alast(points));
    afree(m);
    return curve;
}

static void pulse(mesh *m, const line_chart *c, vec2 end, uint32_t color)
{
    float s = c->dp;
    double phase = fmod(c->now, 1.5);
    if (phase < 0.9 && !c->reduced) {
        double t = phase / 0.9;
        float radius = (float)(9 + 12 * t) * s * 0.5f;
        disc(m, end, radius - 1.2f * s, radius, premultiply(color, (float)(0.35 * (1 - t))));
    }
    disc(m, end, 0, 3.25f * s, premultiply(0xffffffff, 1));
    disc(m, end, 0, 1.75f * s, premultiply(color, 1));
}

static void draw_series(mesh *m, const line_chart *c, int si, vec2 size, uint32_t line)
{
    const published *series = find_series(c->names[si]);
    float s = c->dp;
    if (!series) {
        return;
    }
    uint32_t color = series->color ? series->color : line;
    vec2 *points = series_points(c, series, size);
    if (acount(points) < 2) {
        afree(points);
        return;
    }
    vec2 *curve = monotone_curve(points, s);
    int n = acount(curve);
    if (c->fill && si == 0) {
        for (int i = 0; i + 1 < n; i++) {
            quad(
                m,
                curve[i],
                curve[i + 1],
                (vec2){ curve[i + 1].x, size.y },
                (vec2){ curve[i].x, size.y },
                premultiply(color, 0.22f),
                premultiply(color, 0)
            );
        }
    }
    for (int i = 0; i + 1 < n; i++) {
        segment(m, curve[i], curve[i + 1], 1.5f * s, premultiply(color, 1));
    }
    if (c->live && si == 0) {
        pulse(m, c, alast(points), color);
    }
    afree(curve);
    afree(points);
}

static void draw_chart(mesh *m, const line_chart *c, vec2 size)
{
    if (size.x <= 1 || size.y <= 1 || c->hi <= c->lo) {
        return;
    }
    float s = c->dp;
    for (int i = 0; i < acount(c->values); i++) {
        float y = roundf(y_of(c, size.y, c->values[i])) + 0.5f;
        quad(
            m,
            (vec2){ 0, y - s * 0.5f },
            (vec2){ size.x, y - s * 0.5f },
            (vec2){ size.x, y + s * 0.5f },
            (vec2){ 0, y + s * 0.5f },
            premultiply(c->grid_color, 1),
            premultiply(c->grid_color, 1)
        );
    }
    if (c->reference != 0 && c->reference > c->lo && c->reference < c->hi) {
        float y = y_of(c, size.y, c->reference);
        for (float x = 0; x < size.x; x += 6 * s) {
            segment(
                m,
                (vec2){ x, y },
                (vec2){ math_min(size.x, x + 3 * s), y },
                s,
                premultiply(c->line_color, 0.45f)
            );
        }
    }
    for (int i = 0; i < c->count; i++) {
        draw_series(m, c, i, size, c->line_color);
    }
    if (c->hover_x < 0) {
        return;
    }
    float x = c->hover_x * s;
    quad(
        m,
        (vec2){ x - s * 0.5f, 0 },
        (vec2){ x + s * 0.5f, 0 },
        (vec2){ x + s * 0.5f, size.y },
        (vec2){ x - s * 0.5f, size.y },
        premultiply(c->line_color, 0.5f),
        premultiply(c->line_color, 0.5f)
    );
    if (!isnan(c->hover_value)) {
        disc(m, (vec2){ x, y_of(c, size.y, c->hover_value) }, 0, 3 * s, premultiply(c->line_color, 1));
    }
}

static void chart_render(rml_element *el, void *user)
{
    line_chart *c = user;
    rml_render_manager *manager = rml_element_render_manager(el);
    if (!c || !manager) {
        return;
    }
    uint32_t line = pack(rml_element_color(el));
    uint32_t grid = pack(rml_element_border_top_color(el));
    c->built &= c->line_color == line && c->grid_color == grid;
    if (!c->built) {
        c->built = true;
        c->line_color = line;
        c->grid_color = grid;
        mesh m = { 0 };
        rml_vec2 box = rml_element_box_size(el, RML_BOX_CONTENT);
        draw_chart(&m, c, (vec2){ box.x, box.y });
        rml_geometry_destroy(c->geometry);
        c->geometry = rml_geometry_create(
            manager,
            (rml_mesh){ m.vertices,
                        (size_t)acount(m.vertices),
                        m.indices,
                        (size_t)acount(m.indices) }
        );
        afree(m.vertices);
        afree(m.indices);
    }
    rml_geometry_render(c->geometry, rml_element_absolute_offset(el, RML_BOX_CONTENT));
}

static void chart_resize(rml_element *, void *user)
{
    line_chart *c = user;
    if (c) {
        c->built = false;
    }
}

/* ---------- Range and labels ---------- */

/* d3's nice step: 1, 2 or 5 times a power of ten. */
static double nice_step(double span)
{
    double rough = span / (TICKS - 1);
    double power = pow(10, floor(log10(rough)));
    double f = rough / power;
    return (f < 1.5 ? 1 : f < 3 ? 2 : f < 7 ? 5 : 10) * power;
}

static void format_value(char out[static VALUE_TEXT], double v, int precision, const char *unit)
{
    snprintf(out, VALUE_TEXT, "%.*f%s%s", precision, v, unit[0] ? " " : "", unit);
}

/* Positioning a live readout is paint work: top and left would lay out its
 * document. */
static void translate_label(rml_element *el, const char *axis, double v)
{
    char value[48];
    snprintf(value, sizeof value, "translate%s(%.1fdp)", axis, v);
    rml_element_set_property(el, "transform", value);
}

/* Of the series it names that are published: the newest time among them
 * as `now`, and the sum of their revisions. lo and hi get the range of the
 * values shown, empty (lo > hi) when none is. */
static uint64_t gather(line_chart *c, double *lo, double *hi)
{
    uint64_t revision = 0;
    c->now = 0;
    for (int i = 0; i < c->count; i++) {
        const published *p = find_series(c->names[i]);
        if (p) {
            revision += p->revision;
            c->now = math_max(p->now, c->now);
        }
    }
    *lo = INFINITY;
    *hi = -INFINITY;
    for (int i = 0; i < c->count; i++) {
        const published *p = find_series(c->names[i]);
        for (int j = 0; p && j < acount(p->times); j++) {
            if (p->times[j] >= c->now - c->window) {
                *lo = math_min((double)p->values[j], *lo);
                *hi = math_max((double)p->values[j], *hi);
            }
        }
    }
    return revision;
}

/* The range to show: the given bounds, or the data's with a margin. */
static void target_range(const line_chart *c, double *lo, double *hi)
{
    if (c->min != 0 || c->max != 0) {
        *lo = c->min;
        *hi = c->max;
        return;
    }
    if (*lo > *hi) {
        *lo = 0;
        *hi = 1;
        return;
    }
    if (c->reference != 0) {
        *lo = math_min(c->reference, *lo);
        *hi = math_max(c->reference, *hi);
    }
    double span = math_max(math_max(0.4, fabs(*hi) * 0.1), *hi - *lo);
    double mid = (*hi + *lo) / 2;
    *lo = math_max(mid - span / 2 - span * MARGIN, 0.0);
    *hi = mid + span / 2 + span * MARGIN;
}

/* Eases the shown range toward the data, faster when the gap is small;
 * false once it arrived. */
static bool ease_range(line_chart *c, double lo, double hi, double dt)
{
    double gap = fabs(hi - c->hi) + fabs(lo - c->lo);
    if (!c->ranged || c->reduced || gap <= (hi - lo) * RANGE_SETTLED) {
        c->lo = lo;
        c->hi = hi;
        c->ranged = true;
        return false;
    }
    double speed = RANGE_SPEED +
                   (1 - math_min(gap / math_max(1e-6, hi - lo), 1.0)) * 0.2;
    double factor = 1 - pow(1 - speed, dt / (1.0 / 60));
    c->lo += (lo - c->lo) * factor;
    c->hi += (hi - c->hi) * factor;
    return true;
}

/* Tick labels: reuse spans top to bottom. */
static void label_ticks(line_chart *c, float height, double step)
{
    while (acount(c->ticks) < acount(c->values)) {
        apush(c->ticks, control_part(c->el, "span", "chart-tick"));
    }
    for (int i = 0; i < acount(c->ticks); i++) {
        rml_element *label = c->ticks[i];
        bool shown = i < acount(c->values) && height > 0;
        rml_element_set_property(label, "display", shown ? "block" : "none");
        if (!shown) {
            continue;
        }
        char text[VALUE_TEXT];
        format_value(text, c->values[i], step < 1 ? 1 : 0, "");
        line_text_set(label, text);
        rml_element_set_property(label, "top", "0dp");
        translate_label(label, "Y", height - (c->values[i] - c->lo) / (c->hi - c->lo) * height - 7);
    }
}

/* The first series when it has values, or NULL. */
static const published *first_series(const line_chart *c)
{
    const published *first = c->count ? find_series(c->names[0]) : NULL;
    return first && acount(first->values) ? first : NULL;
}

/* Badge: the newest value of the first series, at its height. */
static void place_badge(line_chart *c, float height)
{
    const published *first = first_series(c);
    rml_element_set_property(c->badge, "display", first && height > 0 ? "block" : "none");
    if (!first) {
        return;
    }
    double v = alast(first->values);
    char text[VALUE_TEXT];
    format_value(text, v, c->precision, c->unit);
    line_text_set(c->badge, text);
    rml_element_set_property(c->badge, "top", "0dp");
    translate_label(
        c->badge,
        "Y",
        math_clamp(
            (float)(height - (v - c->lo) / (c->hi - c->lo) * height) - 9,
            0.0f,
            math_max(height - 18, 0.0f)
        )
    );
}

/* The first index whose time is not below t, bisecting as std::lower_bound
 * does. */
static int lower_bound(const float *times, int count, double t)
{
    int first = 0;
    while (count > 0) {
        int half = count / 2;
        if (times[first + half] < t) {
            first += half + 1;
            count -= half + 1;
            continue;
        }
        count = half;
    }
    return first;
}

/* Value of a series at time t, linearly between samples. */
static double value_at(const published *s, double t)
{
    int count = acount(s->times);
    int i = lower_bound(s->times, count, t);
    if (i == 0 || i == count) {
        return NAN;
    }
    double t0 = s->times[i - 1], t1 = s->times[i];
    double u = t1 > t0 ? (t - t0) / (t1 - t0) : 0;
    return s->values[i - 1] + (s->values[i] - s->values[i - 1]) * u;
}

/* Scrub: a crosshair and the value under the pointer. */
static void scrub(line_chart *c, float width)
{
    const published *first = first_series(c);
    c->hover_x = -1;
    c->hover_value = NAN;
    if (c->pointer >= 0 && first) {
        c->hover_x = c->pointer;
        double t = c->now - c->window + c->hover_x / math_max(1.0f, width) * c->window;
        c->hover_value = value_at(first, t);
    }
    bool shown = !isnan(c->hover_value);
    rml_element_set_property(c->hover, "display", shown ? "block" : "none");
    if (!shown) {
        return;
    }
    char text[VALUE_TEXT];
    format_value(text, c->hover_value, c->precision, c->unit);
    line_text_set(c->hover, text);
    rml_element_set_property(c->hover, "left", "0dp");
    translate_label(c->hover, "X", math_clamp(c->hover_x + 8, 0.0f, math_max(width - 64, 0.0f)));
}

/* ---------- The element ---------- */

/* The names `series` lists, separated by spaces. */
static void read_names(line_chart *c)
{
    char names[SERIES_MAX * NAME_TEXT];
    control_string(c->el, "series", names, sizeof names);
    c->count = 0;
    for (const char *at = names; *at && c->count < SERIES_MAX;) {
        size_t length = strcspn(at, " ");
        if (length) {
            snprintf(c->names[c->count++], NAME_TEXT, "%.*s", (int)length, at);
        }
        at += length + (at[length] == ' ');
    }
}

static void read_attributes(line_chart *c)
{
    read_names(c);
    control_string(c->el, "unit", c->unit, sizeof c->unit);
    c->precision = (int)control_number(c->el, "precision", 0);
    c->window = control_number(c->el, "window", 30);
    c->window = c->window > 0 ? c->window : 30;
    c->reference = control_number(c->el, "reference", 0);
    c->min = control_number(c->el, "min", 0);
    c->max = control_number(c->el, "max", 0);
    c->fill = control_bool(c->el, "fill");
    c->live = control_bool(c->el, "live");
    c->dirty = false;
}

static void on_event(void *, const rml_event *e)
{
    line_chart *c = rml_element_user(e->current);
    if (!c) {
        return;
    }
    float pointer = -1;
    if (e->id == RML_EVENT_MOUSEMOVE || e->id == RML_EVENT_MOUSEOVER) {
        rml_vec2 at = rml_element_absolute_offset(c->el, RML_BOX_CONTENT);
        rml_vec2 size = rml_element_box_size(c->el, RML_BOX_CONTENT);
        float x = e->mouse_x - at.x, y = e->mouse_y - at.y;
        bool inside = x >= 0 && y >= 0 && x <= size.x && y <= size.y;
        pointer = inside ? x / control_dp(c->el) : -1;
    }
    if (pointer != c->pointer) {
        c->pointer = pointer;
        control_redraw(c->el);
    }
}

static line_chart *chart_new(rml_element *el)
{
    line_chart *c = xcalloc(1, sizeof *c);
    *c = (line_chart){ .el = el, .hi = 1, .dirty = true, .pointer = -1, .hover_x = -1 };
    c->hover_value = NAN;
    rml_element_set_user(el, c);
    c->badge = control_part(el, "span", "chart-badge");
    c->hover = control_part(el, "span", "chart-hover");
    rml_element_add_event_listener(el, RML_EVENT_MOUSEMOVE, listener, false);
    rml_element_add_event_listener(el, RML_EVENT_MOUSEOVER, listener, false);
    rml_element_add_event_listener(el, RML_EVENT_MOUSEOUT, listener, false);
    apush(charts, c);
    return c;
}

/* Shows what its series hold: the range eased toward them, the ticks, the
 * badge and the scrub, and builds the geometry again when any moved. */
static void chart_update(rml_element *el, void *user)
{
    line_chart *c = user ? user : chart_new(el);
    if (c->dirty) {
        read_attributes(c);
        c->built = false;
    }
    double previous_now = c->now, previous_lo = c->lo, previous_hi = c->hi,
           previous_hover = c->hover_value;
    float previous_x = c->hover_x;
    double lo, hi, time = rml_elapsed_time();
    uint64_t revision = gather(c, &lo, &hi);
    target_range(c, &lo, &hi);
    c->reduced = motion_reduced(el);
    bool easing = ease_range(c, lo, hi, math_clamp(time - c->time, 0.0, 0.05));
    c->time = time;
    float dp = control_dp(el);
    rml_vec2 box = rml_element_box_size(el, RML_BOX_CONTENT);
    float width = box.x / dp, height = box.y / dp;
    asetlen(c->values, 0);
    double step = nice_step(c->hi - c->lo);
    for (double t = ceil(c->lo / step) * step; t <= c->hi; t += step) {
        apush(c->values, t);
    }
    label_ticks(c, height, step);
    place_badge(c, height);
    scrub(c, width);
    bool hover_same = previous_hover == c->hover_value ||
                      (isnan(previous_hover) && isnan(c->hover_value));
    c->built &= revision == c->revision && previous_now == c->now &&
                previous_lo == c->lo && previous_hi == c->hi && c->dp == dp &&
                previous_x == c->hover_x && hover_same;
    c->revision = revision;
    c->dp = dp;
    if (easing) {
        control_redraw(el);
    }
}

static void chart_attribute(rml_element *, void *user, const char *name)
{
    line_chart *c = user;
    static const char *const NAMES[] = { "series",    "unit", "precision",
                                         "window",    "min",  "max",
                                         "reference", "fill", "live" };
    for (size_t i = 0; c && i < sizeof NAMES / sizeof *NAMES; i++) {
        c->dirty |= !strcmp(name, NAMES[i]);
    }
}

static void chart_release(void *user)
{
    line_chart *c = user;
    if (!c) {
        return;
    }
    for (int i = 0; i < acount(charts); i++) {
        if (charts[i] == c) {
            adel(charts, i);
            break;
        }
    }
    if (!acount(charts)) {
        afree(charts);
    }
    afree(c->ticks);
    afree(c->values);
    rml_geometry_destroy(c->geometry);
    free(c);
}

static const rml_element_hooks HOOKS = { .render = chart_render,
                                         .resize = chart_resize,
                                         .release = chart_release,
                                         .update = chart_update,
                                         .attribute = chart_attribute };

void line_chart_register()
{
    static rml_instancer *instancer;
    if (!instancer) {
        instancer = rml_instancer_create(&HOOKS);
        listener = rml_listener_create(on_event, NULL);
    }
    rml_register_element_instancer("line-chart", instancer);
}
