/* Small authored vector marks, with a one-pixel antialiasing feather. */
#include "support/array.h"
#include "support/math.h"
#include "support/xalloc.h"
#include "ui/controls/controls_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
typedef struct
{
    rml_vertex *vertices;
    int *indices;
} mesh;
typedef struct
{
    rml_geometry *geometry;
    bool built;
    float dp;
} shape_glyph;
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

static uint32_t color(rml_element *el, const char *name)
{
    char text[16] = "";
    control_string(el, name, text, sizeof text);
    if (text[0] != '#' || (strlen(text) != 7 && strlen(text) != 9)) {
        return 0;
    }
    char *end;
    uint32_t value = (uint32_t)strtoul(text + 1, &end, 16);
    if (*end) {
        return 0;
    }
    return strlen(text) == 7 ? (value << 8) | 255 : value;
}
static void render(rml_element *el, void *user)
{
    shape_glyph *glyph = user;
    rml_render_manager *manager = rml_element_render_manager(el);
    if (!glyph || !manager) {
        return;
    }
    float dp = control_dp(el);
    if (!glyph->built || glyph->dp != dp) {
        glyph->built = true;
        glyph->dp = dp;
        mesh m = { 0 };
        rml_vec2 box = rml_element_box_size(el, RML_BOX_BORDER);
        vec2 size = { box.x, box.y };
        float ring = (float)control_number(el, "stroke-width", 0) * dp;
        ring = isfinite(ring) ? math_max(0.0f, ring) : 0;
        rml_color fill = premultiply(color(el, "fill"), 1);
        rml_color edge = premultiply(color(el, "stroke"), 1);
        char shape[16];
        control_string(el, "shape", shape, sizeof shape);
        if (!strcmp(shape, "square")) {
            dot_square(&m, size, ring, fill, edge);
        } else if (!strcmp(shape, "arrow")) {
            dot_arrow(&m, size, ring, fill, edge);
        } else {
            dot_circle(&m, size, ring, fill, edge);
        }
        rml_geometry_destroy(glyph->geometry);
        glyph->geometry = rml_geometry_create(
            manager,
            (rml_mesh){ m.vertices,
                        (size_t)acount(m.vertices),
                        m.indices,
                        (size_t)acount(m.indices) }
        );
        afree(m.vertices);
        afree(m.indices);
    }
    rml_geometry_render(glyph->geometry, rml_element_absolute_offset(el, RML_BOX_BORDER));
}
static void invalidate(rml_element *, void *user)
{
    if (user) {
        ((shape_glyph *)user)->built = false;
    }
}
static void attribute(rml_element *el, void *user, const char *name)
{
    if (strcmp(name, "shape") && strcmp(name, "fill") &&
        strcmp(name, "stroke") && strcmp(name, "stroke-width")) {
        return;
    }
    if (!user) {
        user = xcalloc(1, sizeof(shape_glyph));
        rml_element_set_user(el, user);
    }
    invalidate(el, user);
}
static void release(void *user)
{
    shape_glyph *glyph = user;
    if (!glyph) {
        return;
    }
    rml_geometry_destroy(glyph->geometry);
    free(glyph);
}
void shape_glyph_register()
{
    static const rml_element_hooks hooks = { .render = render,
                                             .resize = invalidate,
                                             .attribute = attribute,
                                             .release = release };
    static rml_instancer *instancer;
    if (!instancer) {
        instancer = rml_instancer_create(&hooks);
    }
    rml_register_element_instancer("shape-glyph", instancer);
}
