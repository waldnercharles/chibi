/* Authored absolute M/L/C paths; routing and appearance remain in assets. */
#include "support/array.h"
#include "support/math.h"
#include "support/xalloc.h"
#include "ui/controls/controls_internal.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define PATH_BYTES 16384
#define PATH_COMMANDS 256
#define CURVE_SAMPLES 24
#define COORDINATE_MAX 1000000.0f
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
} vector_path;
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

static void separators(const char **text)
{
    while (isspace((unsigned char)**text) || **text == ',') {
        (*text)++;
    }
}
static bool point(const char **text, vec2 *out)
{
    float *xy[] = { &out->x, &out->y };
    for (int i = 0; i < 2; i++) {
        separators(text);
        char *end;
        double value = strtod(*text, &end);
        if (end == *text || !isfinite(value) || fabs(value) > COORDINATE_MAX) {
            return false;
        }
        *text = end;
        *xy[i] = (float)value;
    }
    return true;
}
static void cubic(vec2 **points, vec2 a, vec2 b, vec2 end)
{
    vec2 start = alast(*points);
    bool straight = a.x == start.x && a.y == start.y && b.x == end.x && b.y == end.y;
    int samples = straight ? 1 : CURVE_SAMPLES;
    for (int k = 1; k <= samples; k++) {
        float t = (float)k / samples, u = 1 - t;
        vec2 at = vec2_add(
            vec2_add(
                vec2_add(vec2_scale(start, u * u * u), vec2_scale(a, 3 * u * u * t)),
                vec2_scale(b, 3 * u * t * t)
            ),
            vec2_scale(end, t * t * t)
        );
        apush(*points, at);
    }
}
static void run(mesh *m, vec2 **points, float dp, float width, rml_color color)
{
    for (int i = 0; i < acount(*points); i++) {
        (*points)[i] = vec2_scale((*points)[i], dp);
    }
    stroke(m, *points, acount(*points), width * dp, color, 1);
    aclear(*points);
}
static bool path(mesh *m, const char *text, float dp, float width, rml_color color)
{
    vec2 *points = NULL;
    bool valid = true;
    int commands = 0;
    separators(&text);
    while (*text && valid) {
        char command = *text++;
        if (++commands > PATH_COMMANDS) {
            break;
        }
        vec2 a, b, end;
        switch (command) {
            case 'M':
                valid = point(&text, &a);
                if (valid) {
                    run(m, &points, dp, width, color);
                    apush(points, a);
                }
                break;
            case 'L':
                valid = acount(points) && point(&text, &a);
                if (valid) {
                    apush(points, a);
                }
                break;
            case 'C':
                valid = acount(points) && point(&text, &a) &&
                        point(&text, &b) && point(&text, &end);
                if (valid) {
                    cubic(&points, a, b, end);
                }
                break;
            default: valid = false; break;
        }
        separators(&text);
    }
    valid &= commands <= PATH_COMMANDS;
    if (valid) {
        run(m, &points, dp, width, color);
    }
    afree(points);
    return valid;
}
static bool color(rml_element *el, rml_color *out)
{
    char text[16];
    control_string(el, "stroke", text, sizeof text);
    size_t size = strlen(text);
    if (text[0] != '#' || (size != 7 && size != 9)) {
        return false;
    }
    for (size_t i = 1; i < size; i++) {
        if (!isxdigit((unsigned char)text[i])) {
            return false;
        }
    }
    uint32_t value = (uint32_t)strtoul(text + 1, NULL, 16);
    *out = premultiply(size == 7 ? (value << 8) | 255 : value, 1);
    return true;
}
static void render(rml_element *el, void *user)
{
    vector_path *view = user;
    rml_render_manager *manager = rml_element_render_manager(el);
    if (!view || !manager) {
        return;
    }
    float dp = control_dp(el);
    if (!view->built || view->dp != dp) {
        view->built = true;
        view->dp = dp;
        char text[PATH_BYTES] = "";
        size_t length = rml_element_attribute(el, "path", text, sizeof text);
        float width = (float)control_number(el, "stroke-width", 1.5);
        mesh m = { 0 };
        rml_color stroke_color;
        bool valid = length < sizeof text && isfinite(width) && width >= 0 &&
                     width <= 4096 && color(el, &stroke_color);
        if (valid) {
            valid = path(&m, text, dp, width, stroke_color);
        }
        control_flag(el, "data-invalid", !valid);
        rml_geometry_destroy(view->geometry);
        view->geometry = valid && width > 0 && acount(m.vertices)
                           ? rml_geometry_create(
                                 manager,
                                 (rml_mesh){ m.vertices,
                                             (size_t)acount(m.vertices),
                                             m.indices,
                                             (size_t)acount(m.indices) }
                             )
                           : NULL;
        afree(m.vertices);
        afree(m.indices);
    }
    if (view->geometry) {
        rml_geometry_render(view->geometry, rml_element_absolute_offset(el, RML_BOX_CONTENT));
    }
}
static void invalidate(rml_element *, void *user)
{
    if (user) {
        ((vector_path *)user)->built = false;
    }
}
static void attribute(rml_element *el, void *user, const char *name)
{
    if (strcmp(name, "path") && strcmp(name, "stroke") && strcmp(name, "stroke-width")) {
        return;
    }
    if (!user) {
        user = xcalloc(1, sizeof(vector_path));
        rml_element_set_user(el, user);
    }
    invalidate(el, user);
}
static void release(void *user)
{
    vector_path *view = user;
    if (!view) {
        return;
    }
    rml_geometry_destroy(view->geometry);
    free(view);
}
void vector_path_register()
{
    static const rml_element_hooks hooks = { .render = render,
                                             .resize = invalidate,
                                             .attribute = attribute,
                                             .release = release };
    static rml_instancer *instancer;
    if (!instancer) {
        instancer = rml_instancer_create(&hooks);
    }
    rml_register_element_instancer("vector-path", instancer);
}
