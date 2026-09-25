/* Node-editor wires (kit_wires.c): one element draws the canvas grid and every
 * wire as geometry, after xyflow's edges and Blender's noodles. Wire ends anchor
 * to kit paths and resolve after layout, so a wire follows its node in the same
 * frame the node moves. The shape of each wire comes from arrange_path through
 * kit_wire_shape, so the canvas draws what auto-arrange routes and scores.
 * Strokes carry a one-pixel feather because the canvas has no multisampling.
 *
 * math_min and math_max return their second argument on a tie or NaN, so the
 * value to keep in that case comes second. */
#include "editor/rmlui/kit_internal.h"
#include "support/math.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CURVE_SAMPLES 24
#define DASH 6.0f
#define DASH_GAP 4.0f
#define FLOW_SPACING 32.0f
#define FLOW_SPEED 60.0f
#define FLOW_RADIUS 2.5f
/* Pieces of a wire's shape that fit without the heap. */
#define SHAPE_PIECES 16

typedef struct mesh
{
    rml_vertex *vertices;
    int *indices;
} mesh;

typedef struct wire_data
{
    char *from, *to; /* kit paths; "" uses the point */
    vec2 from_point, to_point;
    vec2 *via;
    uint32_t color;
    float width;
    unsigned flags;
    float flow;
} wire_data;

/* The wires element's user data. */
typedef struct wires_element
{
    kit *owner;
    wire_data *wires;
    kit_wire_style style;
    float grid, grid_x, grid_y;
    uint32_t grid_color, grid_major;
    /* Sampled curves from the last render, in dp relative to the element. */
    vec2 **drawn;
    rml_geometry *geometry;
} wires_element;

/* The dot element's user data. */
typedef struct dot_element
{
    kit_dot_desc desc;
    rml_geometry *geometry;
    bool built;
} dot_element;

static uint8_t channel(uint32_t rgba, int shift, float a)
{
    return (uint8_t)lroundf((float)((rgba >> shift) & 255) * a);
}

static rml_color premultiply(uint32_t rgba, float alpha)
{
    float a = math_clamp(((float)(rgba & 255) / 255.0f) * alpha, 0.0f, 1.0f);
    return (rml_color){ channel(rgba, 24, a),
                        channel(rgba, 16, a),
                        channel(rgba, 8, a),
                        (uint8_t)lroundf(a * 255) };
}

static void vertex(mesh *m, vec2 p, rml_color color)
{
    apush(m->vertices, (rml_vertex){ { p.x, p.y }, color, { 0, 0 } });
}

static void indices(mesh *m, const int *list, int count)
{
    for (int i = 0; i < count; i++) {
        apush(m->indices, list[i]);
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

/* Appends one cubic piece, x0 y0 .. x3 y3 in dp, sampled into pixels. */
static void sample_piece(vec2 **out, const float *p, float dp)
{
    vec2 p0 = { p[0], p[1] }, p1 = { p[2], p[3] }, p2 = { p[4], p[5] },
         p3 = { p[6], p[7] };
    bool straight = p1.x == p0.x && p1.y == p0.y && p2.x == p3.x && p2.y == p3.y;
    int samples = straight ? 1 : CURVE_SAMPLES;
    for (int k = 1; k <= samples; k++) {
        float t = (float)k / samples, u = 1 - t;
        vec2 at = vec2_add(
            vec2_add(
                vec2_add(vec2_scale(p0, u * u * u), vec2_scale(p1, 3 * u * u * t)),
                vec2_scale(p2, 3 * u * t * t)
            ),
            vec2_scale(p3, t * t * t)
        );
        apush(*out, vec2_scale(at, dp));
    }
}

/* Samples the cubic pieces kit_wire_shape gives a wire through its knots, in
   dp, into pixels. */
static void shape(vec2 **out, kit_wire_style style, const float *knots, int count, float dp)
{
    float stack[SHAPE_PIECES * 8];
    float *pieces = stack;
    int n = kit_wire_shape(style, knots, count, pieces, SHAPE_PIECES);
    if (n > SHAPE_PIECES) {
        pieces = xmalloc((size_t)n * 8 * sizeof *pieces);
        kit_wire_shape(style, knots, count, pieces, n);
    }
    if (n == 0) {
        apush(*out, vec2_scale((vec2){ knots[0], knots[1] }, dp));
        apush(*out, vec2_scale((vec2){ knots[count * 2 - 2], knots[count * 2 - 1] }, dp));
        return;
    }
    apush(*out, vec2_scale((vec2){ pieces[0], pieces[1] }, dp));
    for (int i = 0; i < n; i++) {
        sample_piece(out, &pieces[i * 8], dp);
    }
    if (pieces != stack) {
        free(pieces);
    }
}

/* Four vertices per point: feather, core edge, core edge, feather. */
static void stroke(mesh *m, const vec2 *p, int n, float width, rml_color color, float px)
{
    if (n < 2) {
        return;
    }
    float alpha = math_min(1.0f, width / px);
    float core = math_max(0.0f, width / 2 - px / 2), outer = core + px;
    rml_color solid = { (uint8_t)(color.r * alpha),
                        (uint8_t)(color.g * alpha),
                        (uint8_t)(color.b * alpha),
                        (uint8_t)(color.a * alpha) };
    rml_color clear = { 0, 0, 0, 0 };
    int base = acount(m->vertices);
    for (int i = 0; i < n; i++) {
        vec2 d0 = i > 0 ? vec2_sub(p[i], p[i - 1]) : vec2_sub(p[i + 1], p[i]);
        vec2 d1 = i + 1 < n ? vec2_sub(p[i + 1], p[i]) : d0;
        float l0 = math_max(1e-5f, hypotf(d0.x, d0.y)),
              l1 = math_max(1e-5f, hypotf(d1.x, d1.y));
        vec2 n0 = { -d0.y / l0, d0.x / l0 }, n1 = { -d1.y / l1, d1.x / l1 };
        vec2 dir = vec2_add(n0, n1);
        float ld = hypotf(dir.x, dir.y);
        dir = ld > 1e-5f ? (vec2){ dir.x / ld, dir.y / ld } : n0;
        float miter = math_min(4.0f, 1.0f / math_max(0.25f, dir.x * n0.x + dir.y * n0.y));
        vertex(m, vec2_add(p[i], vec2_scale(dir, outer * miter)), clear);
        vertex(m, vec2_add(p[i], vec2_scale(dir, core * miter)), solid);
        vertex(m, vec2_sub(p[i], vec2_scale(dir, core * miter)), solid);
        vertex(m, vec2_sub(p[i], vec2_scale(dir, outer * miter)), clear);
    }
    for (int i = 0; i + 1 < n; i++) {
        int a = base + i * 4, b = a + 4;
        for (int s = 0; s < 3; s++) {
            indices(m, (int[]){ a + s, b + s, b + s + 1, a + s, b + s + 1, a + s + 1 }, 6);
        }
    }
}

static void disc(mesh *m, vec2 c, float r, rml_color color, float px)
{
    /* Enough sides that the edge stays round at any size. */
    int sides = math_clamp((int)(r * 1.5f), 12, 64);
    int base = acount(m->vertices);
    rml_color clear = { 0, 0, 0, 0 };
    vertex(m, c, color);
    for (int i = 0; i < sides; i++) {
        float a = 2 * MATH_PI_F * i / sides;
        vec2 u = { cosf(a), sinf(a) };
        vertex(m, vec2_add(c, vec2_scale(u, r)), color);
        vertex(m, vec2_add(c, vec2_scale(u, r + px)), clear);
    }
    for (int i = 0; i < sides; i++) {
        int j = (i + 1) % sides;
        int in_i = base + 1 + i * 2, out_i = in_i + 1, in_j = base + 1 + j * 2,
            out_j = in_j + 1;
        indices(m, (int[]){ base, in_i, in_j, in_i, out_i, out_j, in_i, out_j, in_j }, 9);
    }
}

/* Strokes the dashes of a polyline, each as its own run of points. */
static void stroke_dashes(mesh *m, const vec2 *p, float width, rml_color color, float dp)
{
    float dash = DASH * dp, gap = DASH_GAP * dp;
    float along = 0;
    bool on = true;
    vec2 *run = NULL;
    apush(run, p[0]);
    for (int i = 1; i < acount(p); i++) {
        vec2 a = p[i - 1], b = p[i];
        float length = hypotf(b.x - a.x, b.y - a.y), used = 0;
        while (length - used > 1e-4f) {
            float left = (on ? dash : gap) - along;
            float step = math_min(length - used, left);
            used += step;
            along += step;
            vec2 q = vec2_add(a, vec2_scale(vec2_sub(b, a), used / length));
            if (on) {
                apush(run, q);
            }
            if (along >= (on ? dash : gap) - 1e-4f) {
                if (on) {
                    stroke(m, run, acount(run), width, color, 1);
                }
                on = !on;
                along = 0;
                aclear(run);
                apush(run, q);
            }
        }
    }
    if (on) {
        stroke(m, run, acount(run), width, color, 1);
    }
    afree(run);
}

/* Markers move from source to target, spaced evenly along the wire. */
static void flow_markers(mesh *m, const vec2 *path, const wire_data *w, float dp)
{
    int n = acount(path);
    float spacing = FLOW_SPACING * dp, total = 0;
    for (int i = 1; i < n; i++) {
        total += hypotf(path[i].x - path[i - 1].x, path[i].y - path[i - 1].y);
    }
    if (total <= 0) {
        return;
    }
    float phase = fmodf(w->flow * FLOW_SPEED * dp, spacing);
    rml_color color = premultiply(w->color, 1);
    float along = 0;
    int seg = 1;
    for (float d = phase; d < total; d += spacing) {
        while (seg < n) {
            vec2 a = path[seg - 1], b = path[seg];
            float l = hypotf(b.x - a.x, b.y - a.y);
            if (along + l >= d) {
                vec2 at = vec2_add(a, vec2_scale(vec2_sub(b, a), (d - along) / math_max(1e-5f, l)));
                disc(m, at, FLOW_RADIUS * dp, color, 1);
                break;
            }
            along += l;
            seg++;
        }
    }
}

static void draw_wire(mesh *m, const vec2 *path, const wire_data *w, float dp)
{
    rml_color color = premultiply(w->color, 1);
    if (w->flags & KIT_WIRE_DASHED) {
        stroke_dashes(m, path, w->width * dp, color, dp);
    } else {
        stroke(m, path, acount(path), w->width * dp, color, 1);
    }
    if (w->flags & KIT_WIRE_FLOW) {
        flow_markers(m, path, w, dp);
    }
}

/* Minor lines fade out before they crowd; major lines every fourth. */
static void grid_lines(mesh *m, const wires_element *w, int axis, rml_vec2 size, float dp)
{
    float extent = axis == 0 ? size.x : size.y, across = axis == 0 ? size.y : size.x;
    float offset = axis == 0 ? w->grid_x : w->grid_y;
    float step = w->grid * dp;
    float minor = math_clamp((step - 6) / 10, 0.0f, 1.0f);
    float first = ceilf(offset / w->grid);
    for (float g = first;; g += 1) {
        float at = (g * w->grid - offset) * dp;
        if (at > extent) {
            break;
        }
        bool major = fmodf(fabsf(g), 4.0f) < 0.5f;
        float alpha = major ? 1 : minor;
        if (alpha <= 0) {
            continue;
        }
        rml_color color = premultiply(major ? w->grid_major : w->grid_color, alpha);
        float x = floorf(at) + 0.5f;
        vec2 line[2] = { { x, 0 }, { x, across } };
        if (axis == 1) {
            line[0] = (vec2){ 0, x };
            line[1] = (vec2){ across, x };
        }
        stroke(m, line, 2, 1, color, 1);
    }
}

static void draw_grid(mesh *m, const wires_element *w, rml_element *el, float dp)
{
    rml_vec2 size = rml_element_box_size(el, RML_BOX_BORDER);
    if (w->grid * dp <= 0 || size.x <= 0 || size.y <= 0) {
        return;
    }
    grid_lines(m, w, 0, size, dp);
    grid_lines(m, w, 1, size, dp);
}

/* A wire end in pixels from the origin: the center of the element at its path,
 * or its point when the path is empty. False when that element is gone or
 * hidden. */
static bool resolve(const kit *k, const char *path, vec2 point, vec2 origin, float dp, vec2 *out)
{
    if (str_eq(path, "")) {
        *out = vec2_scale(point, dp);
        return true;
    }
    node *n = path_node(k, path);
    if (!n || !rml_element_is_visible(n->el, true)) {
        return false;
    }
    rml_vec2 at = rml_element_absolute_offset(n->el, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(n->el, RML_BOX_BORDER);
    *out = vec2_sub(vec2_add((vec2){ at.x, at.y }, (vec2){ size.x / 2, size.y / 2 }), origin);
    return true;
}

/* Samples wire i into drawn[i] and draws it; drawn[i] stays empty when an end
 * does not resolve. */
static void wire_path(mesh *m, wires_element *w, int i, vec2 origin, float dp)
{
    const wire_data *wire = &w->wires[i];
    vec2 a, b;
    if (!resolve(w->owner, wire->from, wire->from_point, origin, dp, &a) ||
        !resolve(w->owner, wire->to, wire->to_point, origin, dp, &b)) {
        return;
    }
    float *knots = NULL;
    apush(knots, a.x / dp);
    apush(knots, a.y / dp);
    for (int v = 0; v < acount(wire->via); v++) {
        apush(knots, wire->via[v].x);
        apush(knots, wire->via[v].y);
    }
    apush(knots, b.x / dp);
    apush(knots, b.y / dp);
    vec2 **path = &w->drawn[i];
    shape(path, w->style, knots, acount(knots) / 2, dp);
    afree(knots);
    draw_wire(m, *path, wire, dp);
    for (int j = 0; j < acount(*path); j++) {
        (*path)[j] = (vec2){ (*path)[j].x / dp, (*path)[j].y / dp };
    }
}

static void drawn_free(wires_element *w)
{
    for (int i = 0; i < acount(w->drawn); i++) {
        afree(w->drawn[i]);
    }
    afree(w->drawn);
}

static void wires_render(rml_element *el, void *user)
{
    wires_element *w = user;
    rml_render_manager *manager = rml_element_render_manager(el);
    if (!manager) {
        return;
    }
    float dp = rml_context_dp_ratio(rml_element_context(el));
    rml_vec2 origin = rml_element_absolute_offset(el, RML_BOX_BORDER);
    mesh m = { 0 };
    draw_grid(&m, w, el, dp);
    drawn_free(w);
    for (int i = 0; i < acount(w->wires); i++) {
        apush(w->drawn, NULL);
    }
    for (int i = 0; i < acount(w->wires); i++) {
        wire_path(&m, w, i, (vec2){ origin.x, origin.y }, dp);
    }
    upload(&w->geometry, manager, &m);
    rml_geometry_render(w->geometry, origin);
}

/* Frees the wires past count and zeroes new ones. */
static void resize_wires(wires_element *w, int count)
{
    int have = acount(w->wires);
    for (int i = count; i < have; i++) {
        free(w->wires[i].from);
        free(w->wires[i].to);
        afree(w->wires[i].via);
    }
    asetlen(w->wires, count);
    for (int i = have; i < count; i++) {
        w->wires[i] = (wire_data){ 0 };
    }
}

static void wires_release(void *user)
{
    wires_element *w = user;
    resize_wires(w, 0);
    afree(w->wires);
    drawn_free(w);
    rml_geometry_destroy(w->geometry);
    free(w);
}

static const rml_element_hooks wires_hooks = { wires_render, NULL, wires_release };

void register_wires_element()
{
    static rml_instancer *instancer;
    if (!instancer) {
        instancer = rml_instancer_create(&wires_hooks);
    }
    rml_register_element_instancer("kit-wires", instancer);
}

/* An element's user data, zeroed on first use. */
static void *element_data(rml_element *el, size_t size)
{
    void *data = rml_element_user(el);
    if (!data) {
        data = xcalloc(1, size);
        rml_element_set_user(el, data);
    }
    return data;
}

static void copy_wire(wire_data *out, const kit_wire *in)
{
    str_set(&out->from, in->from);
    str_set(&out->to, in->to);
    out->from_point = (vec2){ in->from_x, in->from_y };
    out->to_point = (vec2){ in->to_x, in->to_y };
    aclear(out->via);
    for (int v = 0; v < in->via_count; v++) {
        apush(out->via, (vec2){ in->via[v * 2], in->via[v * 2 + 1] });
    }
    out->color = in->color;
    out->width = in->width > 0 ? in->width : 1.5f;
    out->flags = in->flags;
    out->flow = in->flow;
}

void kit_wires(kit *k, const char *key, const char *cls, const kit_wires_desc *d)
{
    node *n = emit(k, key, KIND_BOX, "kit-wires", KIT_CX(k, "wires", { cls, cls != NULL }));
    wires_element *w = element_data(n->el, sizeof *w);
    w->owner = k;
    w->style = d->style;
    w->grid = d->grid;
    w->grid_x = d->grid_x;
    w->grid_y = d->grid_y;
    w->grid_color = d->grid_color;
    w->grid_major = d->grid_major_color;
    resize_wires(w, math_max(d->count, 0));
    for (int i = 0; i < d->count; i++) {
        copy_wire(&w->wires[i], &d->wires[i]);
    }
}

int kit_wire_points(const kit *k, const char *path, int index, float *xy, int max)
{
    node *n = path_node(k, path);
    if (!n || !str_eq(n->tag, "kit-wires")) {
        return 0;
    }
    const wires_element *w = rml_element_user(n->el);
    if (index < 0 || index >= acount(w->drawn)) {
        return 0;
    }
    const vec2 *points = w->drawn[index];
    int count = acount(points);
    for (int i = 0; i < count && i < max; i++) {
        xy[i * 2] = points[i].x;
        xy[i * 2 + 1] = points[i].y;
    }
    return count;
}

/* Dots (kit_dot): sockets and status marks drawn as geometry, because RmlUi's
   rounded boxes carry no antialiasing and read as squares at small sizes. */

static void triangle(mesh *m, vec2 a, vec2 b, vec2 c, rml_color color)
{
    int base = acount(m->vertices);
    vertex(m, a, color);
    vertex(m, b, color);
    vertex(m, c, color);
    indices(m, (int[]){ base, base + 1, base + 2 }, 3);
}

static void square(mesh *m, vec2 min, vec2 max, rml_color color)
{
    triangle(m, min, (vec2){ max.x, min.y }, max, color);
    triangle(m, min, max, (vec2){ min.x, max.y }, color);
}

static void dot_circle(mesh *m, vec2 size, float ring, rml_color fill, rml_color edge)
{
    vec2 c = { size.x / 2, size.y / 2 };
    float r = math_min(size.y, size.x) / 2 - 0.5f;
    if (ring > 0) {
        disc(m, c, r - 0.5f, edge, 1);
    }
    disc(m, c, math_max(0.0f, r - ring - 0.5f), fill, ring > 0 ? 0.75f : 1);
}

static void dot_square(mesh *m, vec2 size, float ring, rml_color fill, rml_color edge)
{
    /* Inset so it weighs about as much as a circle of the same box. */
    float inset = 0.5f + size.x * 0.1f;
    vec2 a = { inset, inset }, b = { size.x - inset, inset },
         c = { size.x - inset, size.y - inset }, d = { inset, size.y - inset };
    rml_color outline = ring > 0 ? edge : fill;
    square(m, a, c, outline);
    if (ring > 0) {
        square(m, vec2_add(a, (vec2){ ring, ring }), vec2_sub(c, (vec2){ ring, ring }), fill);
    }
    stroke(m, (vec2[]){ a, b, c, d, a }, 5, 1, outline, 1);
}

/* An arrow pointing right, inset by half a pixel for its feather. */
static void dot_arrow(mesh *m, vec2 size, float ring, rml_color fill, rml_color edge)
{
    vec2 a = { 0.5f + size.x * 0.1f, 0.5f }, b = { size.x - 0.5f, size.y / 2 },
         c = { 0.5f + size.x * 0.1f, size.y - 0.5f };
    rml_color outline = ring > 0 ? edge : fill;
    triangle(m, a, b, c, outline);
    if (ring > 0) {
        vec2 sum = vec2_add(vec2_add(a, b), c);
        vec2 center = { sum.x / 3, sum.y / 3 };
        float k = math_max(1 - 2.2f * ring / size.y, 0.0f);
        triangle(
            m,
            vec2_add(center, vec2_scale(vec2_sub(a, center), k)),
            vec2_add(center, vec2_scale(vec2_sub(b, center), k)),
            vec2_add(center, vec2_scale(vec2_sub(c, center), k)),
            fill
        );
    }
    stroke(m, (vec2[]){ a, b, c, a }, 4, 1, outline, 1);
}

static void draw_dot(mesh *m, rml_element *el, const kit_dot_desc *desc)
{
    float dp = rml_context_dp_ratio(rml_element_context(el));
    rml_vec2 box = rml_element_box_size(el, RML_BOX_BORDER);
    vec2 size = { box.x, box.y };
    float ring = desc->ring_width * dp;
    rml_color fill = premultiply(desc->fill, 1), edge = premultiply(desc->ring, 1);
    switch (desc->shape) {
        case KIT_DOT_CIRCLE: dot_circle(m, size, ring, fill, edge); return;
        case KIT_DOT_SQUARE: dot_square(m, size, ring, fill, edge); return;
        case KIT_DOT_ARROW: dot_arrow(m, size, ring, fill, edge); return;
    }
}

static void dot_render(rml_element *el, void *user)
{
    dot_element *dot = user;
    rml_render_manager *manager = rml_element_render_manager(el);
    if (!manager) {
        return;
    }
    if (!dot->built) {
        dot->built = true;
        mesh m = { 0 };
        draw_dot(&m, el, &dot->desc);
        upload(&dot->geometry, manager, &m);
    }
    rml_geometry_render(dot->geometry, rml_element_absolute_offset(el, RML_BOX_BORDER));
}

static void dot_resize(rml_element *, void *user)
{
    dot_element *dot = user;
    dot->built = false;
}

static void dot_release(void *user)
{
    dot_element *dot = user;
    rml_geometry_destroy(dot->geometry);
    free(dot);
}

static const rml_element_hooks dot_hooks = { dot_render, dot_resize, dot_release };

void register_dot_element()
{
    static rml_instancer *instancer;
    if (!instancer) {
        instancer = rml_instancer_create(&dot_hooks);
    }
    rml_register_element_instancer("kit-dot", instancer);
}

void kit_dot(kit *k, const char *key, const char *cls, const kit_dot_desc *d)
{
    node *n = emit(k, key, KIND_BOX, "kit-dot", cls);
    dot_element *dot = element_data(n->el, sizeof *dot);
    if (memcmp(&dot->desc, d, sizeof *d) != 0) {
        dot->desc = *d;
        dot->built = false;
    }
}
