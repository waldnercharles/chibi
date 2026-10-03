/* Live line charts after Liveline, with Recharts' axes: one custom element draws
 * every series as geometry (no element per point). The value range follows the
 * visible data with a 12% margin and eases toward it frame-rate independently
 * (factor 1 - (1 - 0.15)^(dt / 16.67 ms)); curves are monotone cubic
 * (Fritsch-Carlson), so they never overshoot a sample. Five "nice" ticks label the
 * grid, a badge carries the newest value, the newest point pulses every 1.5 s,
 * and hovering scrubs a crosshair with the value under it.
 *
 * math_min and math_max return their second argument on a tie or NaN, so the
 * value to keep in that case comes second. */
#include "editor/rmlui/kit_internal.h"
#include "support/math.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define MARGIN 0.12f
#define RANGE_SPEED 0.15
#define TICKS 5
#define DISC_SIDES 24
#define VALUE_TEXT 64

/* The chart element's user data. */
typedef struct chart_element
{
    chart_frame frame;
    rml_geometry *geometry;
    bool built;
    uint32_t line_color, grid_color;
} chart_element;

typedef struct mesh
{
    rml_vertex *vertices;
    int *indices;
} mesh;

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

/* d3's nice step: 1, 2 or 5 times a power of ten. */
static double nice_step(double span)
{
    double rough = span / (TICKS - 1);
    double power = pow(10, floor(log10(rough)));
    double f = rough / power;
    return (f < 1.5 ? 1 : f < 3 ? 2 : f < 7 ? 5 : 10) * power;
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

static float y_of(const chart_frame *f, float height, double v)
{
    return (float)(height - (v - f->lo) / (f->hi - f->lo) * height);
}

static float x_of(const chart_frame *f, float width, double t)
{
    return (float)((t - (f->now - f->window)) / f->window * width);
}

/* At most two points per pixel column: keep each column's peak. */
static vec2 *series_points(const chart_frame *f, const chart_series *series, vec2 size)
{
    float s = f->dp;
    vec2 *points = NULL;
    float column = -1;
    for (int i = 0; i < acount(series->times); i++) {
        float x = x_of(f, size.x, series->times[i]);
        if (x < -2 * s) {
            continue;
        }
        float y = y_of(f, size.y, series->values[i]);
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

static void pulse(mesh *m, const chart_frame *f, vec2 end, uint32_t color)
{
    float s = f->dp;
    double phase = fmod(f->now, 1.5);
    if (phase < 0.9 && !f->reduced_motion) {
        double t = phase / 0.9;
        float radius = (float)(9 + 12 * t) * s * 0.5f;
        disc(m, end, radius - 1.2f * s, radius, premultiply(color, (float)(0.35 * (1 - t))));
    }
    disc(m, end, 0, 3.25f * s, premultiply(0xffffffff, 1));
    disc(m, end, 0, 1.75f * s, premultiply(color, 1));
}

static void draw_series(mesh *m, const chart_frame *f, int si, vec2 size, uint32_t line)
{
    const chart_series *series = &f->series[si];
    float s = f->dp;
    uint32_t color = series->color ? series->color : line;
    vec2 *points = series_points(f, series, size);
    if (acount(points) < 2) {
        afree(points);
        return;
    }
    vec2 *curve = monotone_curve(points, s);
    int n = acount(curve);
    if (f->fill && si == 0) {
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
    if (f->live && si == 0) {
        pulse(m, f, alast(points), color);
    }
    afree(curve);
    afree(points);
}

static void draw_chart(mesh *m, rml_element *el, const chart_frame *f)
{
    rml_vec2 box = rml_element_box_size(el, RML_BOX_CONTENT);
    vec2 size = { box.x, box.y };
    if (size.x <= 1 || size.y <= 1 || f->hi <= f->lo) {
        return;
    }
    float s = f->dp;
    uint32_t line = pack(rml_element_color(el));
    uint32_t grid = pack(rml_element_border_top_color(el));
    for (int i = 0; i < acount(f->ticks); i++) {
        float y = roundf(y_of(f, size.y, f->ticks[i])) + 0.5f;
        quad(
            m,
            (vec2){ 0, y - s * 0.5f },
            (vec2){ size.x, y - s * 0.5f },
            (vec2){ size.x, y + s * 0.5f },
            (vec2){ 0, y + s * 0.5f },
            premultiply(grid, 1),
            premultiply(grid, 1)
        );
    }
    if (f->reference != 0 && f->reference > f->lo && f->reference < f->hi) {
        float y = y_of(f, size.y, f->reference);
        for (float x = 0; x < size.x; x += 6 * s) {
            segment(m, (vec2){ x, y }, (vec2){ math_min(size.x, x + 3 * s), y }, s, premultiply(line, 0.45f));
        }
    }
    for (int i = 0; i < acount(f->series); i++) {
        draw_series(m, f, i, size, line);
    }
    if (f->hover_x < 0) {
        return;
    }
    float x = f->hover_x * s;
    quad(
        m,
        (vec2){ x - s * 0.5f, 0 },
        (vec2){ x + s * 0.5f, 0 },
        (vec2){ x + s * 0.5f, size.y },
        (vec2){ x - s * 0.5f, size.y },
        premultiply(line, 0.5f),
        premultiply(line, 0.5f)
    );
    if (!isnan(f->hover_value)) {
        disc(m, (vec2){ x, y_of(f, size.y, f->hover_value) }, 0, 3 * s, premultiply(line, 1));
    }
}

/* Replaces the geometry with the mesh and frees the mesh. */
static void upload(rml_geometry **geometry, rml_render_manager *manager, mesh *m)
{
    rml_geometry_destroy(*geometry);
    *geometry = rml_geometry_create(
        manager,
        (rml_mesh){ m->vertices,
                    (size_t)acount(m->vertices),
                    m->indices,
                    (size_t)acount(m->indices) }
    );
    afree(m->vertices);
    afree(m->indices);
}

static void chart_render(rml_element *el, void *user)
{
    chart_element *chart = user;
    rml_render_manager *manager = rml_element_render_manager(el);
    if (!manager) {
        return;
    }
    uint32_t line = pack(rml_element_color(el));
    uint32_t grid = pack(rml_element_border_top_color(el));
    chart->built &= chart->line_color == line && chart->grid_color == grid;
    if (!chart->built) {
        chart->built = true;
        chart->line_color = line;
        chart->grid_color = grid;
        mesh m = { 0 };
        draw_chart(&m, el, &chart->frame);
        upload(&chart->geometry, manager, &m);
    }
    rml_geometry_render(chart->geometry, rml_element_absolute_offset(el, RML_BOX_CONTENT));
}

static void chart_resize(rml_element *, void *user)
{
    chart_element *chart = user;
    chart->built = false;
}

/* Frees the series past count and zeroes new ones. */
static void resize_series(chart_series **series, int count)
{
    int have = acount(*series);
    for (int i = count; i < have; i++) {
        afree((*series)[i].times);
        afree((*series)[i].values);
    }
    asetlen(*series, count);
    for (int i = have; i < count; i++) {
        (*series)[i] = (chart_series){ 0 };
    }
}

static void chart_release(void *user)
{
    chart_element *chart = user;
    resize_series(&chart->frame.series, 0);
    afree(chart->frame.series);
    afree(chart->frame.ticks);
    rml_geometry_destroy(chart->geometry);
    free(chart);
}

static const rml_element_hooks chart_hooks = { chart_render, chart_resize, chart_release };

void register_chart_element()
{
    static rml_instancer *instancer;
    if (!instancer) {
        instancer = rml_instancer_create(&chart_hooks);
    }
    rml_register_element_instancer("kit-chart", instancer);
}

static void format_value(char out[static VALUE_TEXT], double v, int precision, const char *unit)
{
    snprintf(out, VALUE_TEXT, "%.*f%s%s", precision, v, unit && unit[0] ? " " : "", unit ? unit : "");
}

/* Positioning a live readout is paint work. Absolute top/left changes would
 * format every unrelated panel in the shared document. */
static void translate_label(rml_element *el, const char *axis, double v)
{
    char value[48];
    snprintf(value, sizeof value, "translate%s(%.1fdp)", axis, v);
    rml_element_set_property(el, "transform", value);
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

/* Value of the first series at time t, linearly between samples. */
static double value_at(const chart_series *s, double t)
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

static void copy_floats(float **out, const float *in, int count)
{
    asetlen(*out, count);
    for (int i = 0; i < count; i++) {
        (*out)[i] = in[i];
    }
}

/* Copies the series into the frame. lo and hi get the range of the values
 * shown, empty (lo > hi) when none is. */
static void copy_series(chart_frame *f, const kit_chart_desc *d, double *lo, double *hi)
{
    *lo = INFINITY;
    *hi = -INFINITY;
    resize_series(&f->series, d->series_count);
    for (int i = 0; i < d->series_count; i++) {
        const kit_chart_series *in = &d->series[i];
        chart_series *out = &f->series[i];
        out->color = in->color;
        copy_floats(&out->times, in->times, in->count);
        copy_floats(&out->values, in->values, in->count);
        for (int j = 0; j < in->count; j++) {
            if (in->times[j] >= f->now - f->window) {
                *lo = math_min((double)in->values[j], *lo);
                *hi = math_max((double)in->values[j], *hi);
            }
        }
    }
}

/* The range to show: the given bounds, or the data's with a margin. */
static void target_range(const kit_chart_desc *d, double *lo, double *hi)
{
    if (d->min != 0 || d->max != 0) {
        *lo = d->min;
        *hi = d->max;
        return;
    }
    if (*lo > *hi) {
        *lo = 0;
        *hi = 1;
        return;
    }
    if (d->reference != 0) {
        *lo = math_min((double)d->reference, *lo);
        *hi = math_max((double)d->reference, *hi);
    }
    double span = math_max(math_max(0.4, fabs(*hi) * 0.1), *hi - *lo);
    double mid = (*hi + *lo) / 2;
    *lo = math_max(mid - span / 2 - span * MARGIN, 0.0);
    *hi = mid + span / 2 + span * MARGIN;
}

/* Eases the shown range toward the data, faster when the gap is small. */
static void ease_range(chart_state *c, double lo, double hi, double dt, bool instant)
{
    if (!c->ranged || instant) {
        c->lo = lo;
        c->hi = hi;
        c->ranged = true;
        return;
    }
    double gap = fabs(hi - c->hi) + fabs(lo - c->lo);
    double speed = RANGE_SPEED +
                   (1 - math_min(gap / math_max(1e-6, hi - lo), 1.0)) * 0.2;
    double factor = 1 - pow(1 - speed, dt / (1.0 / 60));
    c->lo += (lo - c->lo) * factor;
    c->hi += (hi - c->hi) * factor;
}

/* Tick labels: reuse spans top to bottom. */
static void label_ticks(node *n, const chart_frame *f, kit_rect r, double step)
{
    chart_state *c = n->chart;
    while (acount(c->labels) < acount(f->ticks)) {
        apush(c->labels, raw(n->el, "span", "chart-tick", NULL));
    }
    for (int i = 0; i < acount(c->labels); i++) {
        rml_element *label = c->labels[i];
        bool shown = i < acount(f->ticks) && r.h > 0;
        rml_element_set_property(label, "display", shown ? "block" : "none");
        if (!shown) {
            continue;
        }
        char text[VALUE_TEXT];
        format_value(text, f->ticks[i], step < 1 ? 1 : 0, NULL);
        line_text_set(label, text);
        rml_element_set_property(label, "top", "0dp");
        translate_label(label, "Y", r.h - (f->ticks[i] - f->lo) / (f->hi - f->lo) * r.h - 7);
    }
}

/* The first series when it has values, or NULL. */
static const chart_series *first_series(const chart_frame *f)
{
    return acount(f->series) && acount(f->series[0].values) ? &f->series[0] : NULL;
}

/* Badge: the newest value of the first series, at its height. */
static void place_badge(chart_state *c, const chart_frame *f, kit_rect r, const kit_chart_desc *d)
{
    const chart_series *first = first_series(f);
    rml_element_set_property(c->badge, "display", first && r.h > 0 ? "block" : "none");
    if (!first) {
        return;
    }
    double v = alast(first->values);
    char text[VALUE_TEXT];
    format_value(text, v, d->precision, d->unit);
    line_text_set(c->badge, text);
    rml_element_set_property(c->badge, "top", "0dp");
    translate_label(
        c->badge,
        "Y",
        math_clamp((float)(r.h - (v - f->lo) / (f->hi - f->lo) * r.h) - 9, 0.0f, math_max(r.h - 18, 0.0f))
    );
}

/* Scrub: a crosshair and the value under the pointer. Returns the time under
 * it, or -1. */
static float scrub(kit *k, node *n, chart_frame *f, kit_rect r, const kit_chart_desc *d)
{
    const chart_series *first = first_series(f);
    bool hovered = rml_element_is_pseudo_class_set(n->el, "hover") &&
                   rect_contains(r, k->in.mouse_x, k->in.mouse_y);
    f->hover_x = -1;
    f->hover_value = NAN;
    float hover_time = -1;
    if (hovered && first) {
        f->hover_x = k->in.mouse_x - r.x;
        hover_time = (float)(f->now - f->window +
                             f->hover_x / math_max(1.0f, r.w) * f->window);
        f->hover_value = value_at(first, hover_time);
    }
    bool shown = hovered && !isnan(f->hover_value);
    rml_element *hover = n->chart->hover;
    rml_element_set_property(hover, "display", shown ? "block" : "none");
    if (!shown) {
        return hover_time;
    }
    char text[VALUE_TEXT];
    format_value(text, f->hover_value, d->precision, d->unit);
    line_text_set(hover, text);
    rml_element_set_property(hover, "left", "0dp");
    translate_label(hover, "X", math_clamp(f->hover_x + 8, 0.0f, math_max(r.w - 64, 0.0f)));
    return hover_time;
}

static chart_element *chart_element_of(rml_element *el)
{
    chart_element *chart = rml_element_user(el);
    if (chart) {
        return chart;
    }
    chart = xcalloc(1, sizeof *chart);
    chart->frame = (chart_frame){ .hi = 1, .window = 30, .dp = 1, .hover_x = -1 };
    rml_element_set_user(el, chart);
    return chart;
}

static chart_state *chart_state_of(node *n)
{
    if (n->chart) {
        return n->chart;
    }
    n->chart = xcalloc(1, sizeof *n->chart);
    n->chart->hi = 1;
    n->chart->badge = raw(n->el, "span", "chart-badge", NULL);
    n->chart->hover = raw(n->el, "span", "chart-hover", NULL);
    return n->chart;
}

static bool same_series(const chart_frame *f, const kit_chart_desc *d)
{
    if (acount(f->series) != d->series_count) {
        return false;
    }
    for (int i = 0; i < d->series_count; i++) {
        const chart_series *a = &f->series[i];
        const kit_chart_series *b = &d->series[i];
        if (a->color != b->color || acount(a->times) != b->count) {
            return false;
        }
        for (int j = 0; j < b->count; j++) {
            if (a->times[j] != b->times[j] || a->values[j] != b->values[j]) {
                return false;
            }
        }
    }
    return true;
}

float kit_chart(kit *k, const char *key, const char *cls, const kit_chart_desc *d)
{
    node *n = emit(k, key, KIND_BOX, "kit-chart", KIT_CX(k, "chart", { cls, cls != NULL }));
    chart_state *c = chart_state_of(n);
    chart_element *chart = chart_element_of(n->el);
    chart_frame *f = &chart->frame;
    chart_frame previous = *f;
    bool samples_same = same_series(f, d);
    double dt = math_clamp(k->in.time - c->time, 0.0, 0.05);
    c->time = k->in.time;
    f->now = d->now;
    f->window = d->window > 0 ? d->window : 30;
    f->dp = dp(k);
    f->fill = d->fill;
    f->live = d->live;
    f->reference = d->reference;
    f->reduced_motion = k->reduced_motion;
    double lo, hi;
    copy_series(f, d, &lo, &hi);
    target_range(d, &lo, &hi);
    ease_range(c, lo, hi, dt, k->reduced_motion);
    f->lo = c->lo;
    f->hi = c->hi;
    aclear(f->ticks);
    double step = nice_step(f->hi - f->lo);
    for (double t = ceil(f->lo / step) * step; t <= f->hi; t += step) {
        apush(f->ticks, t);
    }
    kit_rect r = element_rect(k, n->el);
    label_ticks(n, f, r, step);
    place_badge(c, f, r, d);
    float hover_time = scrub(k, n, f, r, d);
    bool hover_same = previous.hover_value == f->hover_value ||
                      (isnan(previous.hover_value) && isnan(f->hover_value));
    chart->built &= samples_same && previous.now == f->now &&
                    previous.window == f->window && previous.dp == f->dp &&
                    previous.lo == f->lo && previous.hi == f->hi &&
                    previous.reference == f->reference &&
                    previous.fill == f->fill && previous.live == f->live &&
                    previous.reduced_motion == f->reduced_motion &&
                    previous.hover_x == f->hover_x && hover_same;
    return hover_time;
}
