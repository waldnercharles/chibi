#include "graph/modeling/model_internal.h"
#include "types/array.h"

#include <float.h>
#include <math.h>
#include <string.h>

static const pin_array *vertices(const node *n)
{
    return n->input_pins[0].default_value.ptr;
}
static const float *vertex(const pin_array *points, int i)
{
    return pin_array_data(points, i);
}
static double cross(const float a[2], const float b[2], const float c[2])
{
    return ((double)b[0] - a[0]) * ((double)c[1] - a[1]) -
           ((double)b[1] - a[1]) * ((double)c[0] - a[0]);
}
static bool segments_cross(
    const float a[2],
    const float b[2],
    const float c[2],
    const float d[2],
    double tolerance
)
{
    double ab_c = cross(a, b, c), ab_d = cross(a, b, d), cd_a = cross(c, d, a),
           cd_b = cross(c, d, b);
    if ((ab_c > tolerance && ab_d > tolerance) ||
        (ab_c < -tolerance && ab_d < -tolerance) ||
        (cd_a > tolerance && cd_b > tolerance) ||
        (cd_a < -tolerance && cd_b < -tolerance)) {
        return false;
    }
    for (int i = 0; i < 2; i++) {
        if (fmax(a[i], b[i]) < fmin(c[i], d[i]) ||
            fmax(c[i], d[i]) < fmin(a[i], b[i])) {
            return false;
        }
    }
    return true;
}
static bool polygon_validate(const node *n, shader_error *error)
{
    const pin_array *points = vertices(n);
    if (!points || points->count < 3 || points->count > 32 ||
        points->element_type != PIN_STRUCT || !points->type_name ||
        strcmp(points->type_name, "ShaderVec2")) {
        return model_fail(error, n->id, "Profile Polygon requires 3 to 32 structural ShaderVec2 vertices");
    }
    double lo[2] = { DBL_MAX, DBL_MAX }, hi[2] = { -DBL_MAX, -DBL_MAX };
    for (int i = 0; i < points->count; i++) {
        pin_value element = pin_array_get(points, i);
        const pin_value *v = &element;
        if (v->type != PIN_STRUCT || v->container != PIN_CONTAINER_SINGLE ||
            !v->type_name || strcmp(v->type_name, "ShaderVec2") || !v->ptr) {
            return model_fail(error, n->id, "Polygon vertices require typed ShaderVec2 values");
        }
        const float *p = vertex(points, i);
        for (int axis = 0; axis < 2; axis++) {
            if (!isfinite(p[axis])) {
                return model_fail(error, n->id, "Polygon vertices must be finite");
            }
            lo[axis] = fmin(lo[axis], p[axis]);
            hi[axis] = fmax(hi[axis], p[axis]);
        }
        for (int j = 0; j < i; j++) {
            if (p[0] == vertex(points, j)[0] && p[1] == vertex(points, j)[1]) {
                return model_fail(error, n->id, "Polygon vertices must be distinct; omit the repeated closing vertex");
            }
        }
    }
    double span = fmax(hi[0] - lo[0], hi[1] - lo[1]);
    double tolerance = 32 * DBL_EPSILON * span * span;
    for (int i = 0; i < points->count; i++) {
        const float *a = vertex(points, i),
                    *b = vertex(points, (i + 1) % points->count);
        float dx = b[0] - a[0], dy = b[1] - a[1];
        double squared = (double)dx * dx + (double)dy * dy;
        if (!isfinite(dx) || !isfinite(dy) || squared > FLT_MAX || (float)squared <= 0) {
            return model_fail(error, n->id, "Polygon edge exceeds finite distance arithmetic or is degenerate");
        }
        for (int j = i + 1; j < points->count; j++) {
            if (j == i + 1 || (i == 0 && j == points->count - 1)) {
                continue;
            }
            if (segments_cross(
                    a,
                    b,
                    vertex(points, j),
                    vertex(points, (j + 1) % points->count),
                    tolerance
                )) {
                return model_fail(error, n->id, "Polygon has a self intersection");
            }
        }
    }
    double area = 0;
    const float *origin = vertex(points, 0);
    for (int i = 1; i < points->count - 1; i++) {
        area += cross(origin, vertex(points, i), vertex(points, i + 1));
    }
    if (area <= tolerance) {
        return model_fail(error, n->id, "Polygon requires nondegenerate counterclockwise winding");
    }
    for (int i = 0; i < points->count; i++) {
        if (fabs(cross(
                vertex(points, i),
                vertex(points, (i + 1) % points->count),
                vertex(points, (i + 2) % points->count)
            )) <= tolerance) {
            return model_fail(error, n->id, "Polygon corners must not be collinear");
        }
    }
    return true;
}
bool model_profile_validate(const node *n, shader_error *error)
{
    int op = model_operation(n);
    if (op == MODEL_PROFILE_POLYGON) {
        return polygon_validate(n, error);
    }
    if (op == MODEL_PROFILE_CIRCLE && model_number(n, 0) <= 0) {
        return model_fail(error, n->id, "Profile Circle Radius must be positive");
    }
    if (op == MODEL_PROFILE_RECTANGLE) {
        float h[3];
        model_vector(n, 0, h);
        if (h[0] <= 0 || h[1] <= 0) {
            return model_fail(error, n->id, "Profile Rectangle Half Size must be positive");
        }
    }
    if ((op == MODEL_EXTRUDE || op == MODEL_ROUNDED_EXTRUDE || op == MODEL_BEVELED_EXTRUDE) &&
        model_number(n, 1) <= 0) {
        return model_fail(error, n->id, "Extrude Half Depth must be positive");
    }
    if (op == MODEL_ROUNDED_EXTRUDE || op == MODEL_BEVELED_EXTRUDE) {
        float radius = model_number(n, 2);
        if (radius <= 0 || radius > model_number(n, 1)) {
            return model_fail(error, n->id, "Extrusion edge width must be positive and at most Half Depth");
        }
    }
    return true;
}
static int rectangle_distance(compiler *c, const node *n, int q)
{
    int zero = model_scalar(c, n, 0);
    int inside = model_binary(
        c,
        n,
        SHADER_MIN,
        1,
        model_binary(c, n, SHADER_MAX, 1, model_coordinate(c, n, q, 0), model_coordinate(c, n, q, 1)),
        zero
    );
    int outside = model_unary(c, n, SHADER_LENGTH, 1, model_binary(c, n, SHADER_MAX, 2, q, zero));
    return model_binary(c, n, SHADER_ADD, 1, inside, outside);
}
static bool rectangle_vertices(const pin_array *points, float min[2], float max[2])
{
    if (points->count != 4) {
        return false;
    }
    memcpy(min, vertex(points, 0), 2 * sizeof(float));
    memcpy(max, min, 2 * sizeof(float));
    for (int i = 0; i < 4; i++) {
        const float *a = vertex(points, i), *b = vertex(points, (i + 1) % 4);
        if ((a[0] == b[0]) == (a[1] == b[1])) {
            return false;
        }
        for (int axis = 0; axis < 2; axis++) {
            min[axis] = fminf(min[axis], a[axis]);
            max[axis] = fmaxf(max[axis], a[axis]);
        }
    }
    return true;
}
static int polygon_rectangle(
    compiler *c,
    const node *n,
    int p,
    bool revolve,
    const float min[2],
    const float max[2]
)
{
    int upper = model_binary(c, n, SHADER_SUBTRACT, 2, p, model_constant(c, n, 2, max));
    int lower = model_binary(c, n, SHADER_SUBTRACT, 2, model_constant(c, n, 2, min), p);
    int q = model_binary(c, n, SHADER_MAX, 2, lower, upper);
    if (revolve && min[0] == 0) {
        q = model_emit(
            c,
            n,
            SHADER_VECTOR2,
            2,
            model_coordinate(c, n, upper, 0),
            model_coordinate(c, n, q, 1),
            -1
        );
    }
    return rectangle_distance(c, n, q);
}
static bool polygon_convex(const pin_array *points)
{
    for (int i = 0; i < points->count; i++) {
        if (cross(
                vertex(points, i),
                vertex(points, (i + 1) % points->count),
                vertex(points, (i + 2) % points->count)
            ) < 0) {
            return false;
        }
    }
    return true;
}
static int polygon_winding(compiler *c, const node *n, int p)
{
    const pin_array *points = vertices(n);
    int x = model_coordinate(c, n, p, 0), y = model_coordinate(c, n, p, 1);
    int winding = model_scalar(c, n, 0), one = model_scalar(c, n, 1);
    for (int i = 0; i < points->count; i++) {
        const float *a = vertex(points, i),
                    *b = vertex(points, (i + 1) % points->count);
        if (a[1] == b[1]) {
            continue;
        }
        int interval = model_binary(
            c,
            n,
            SHADER_SUBTRACT,
            1,
            model_binary(c, n, SHADER_STEP, 1, y, model_scalar(c, n, a[1])),
            model_binary(c, n, SHADER_STEP, 1, y, model_scalar(c, n, b[1]))
        );
        int intersection = model_binary(
            c,
            n,
            SHADER_ADD,
            1,
            model_scalar(c, n, a[0]),
            model_binary(
                c,
                n,
                SHADER_MULTIPLY,
                1,
                model_binary(c, n, SHADER_SUBTRACT, 1, y, model_scalar(c, n, a[1])),
                model_scalar(c, n, (b[0] - a[0]) / (b[1] - a[1]))
            )
        );
        int right = model_binary(
            c,
            n,
            SHADER_SUBTRACT,
            1,
            one,
            model_binary(c, n, SHADER_STEP, 1, x, intersection)
        );
        winding = model_binary(
            c,
            n,
            SHADER_ADD,
            1,
            winding,
            model_binary(c, n, SHADER_MULTIPLY, 1, interval, right)
        );
    }
    int inside = model_binary(
        c,
        n,
        SHADER_STEP,
        1,
        model_unary(c, n, SHADER_ABS, 1, winding),
        model_scalar(c, n, 0.5f)
    );
    return model_binary(
        c,
        n,
        SHADER_SUBTRACT,
        1,
        one,
        model_binary(c, n, SHADER_MULTIPLY, 1, inside, model_scalar(c, n, 2))
    );
}
static int polygon_distance(compiler *c, const node *n, int p, bool revolve)
{
    const pin_array *points = vertices(n);
    float min[2], max[2];
    if (rectangle_vertices(points, min, max)) {
        return polygon_rectangle(c, n, p, revolve, min, max);
    }
    bool convex = polygon_convex(points);
    int minimum = -1, maximum = -1, zero = model_scalar(c, n, 0),
        one = model_scalar(c, n, 1);
    for (int i = 0; i < points->count && !c->failed; i++) {
        const float *a = vertex(points, i),
                    *b = vertex(points, (i + 1) % points->count);
        if (revolve && a[0] == 0 && b[0] == 0) {
            continue;
        }
        float edge[2] = { b[0] - a[0], b[1] - a[1] },
              normal[2] = { edge[1], -edge[0] };
        float squared = (float)((double)edge[0] * edge[0] + (double)edge[1] * edge[1]);
        int q = model_binary(c, n, SHADER_SUBTRACT, 2, p, model_constant(c, n, 2, a));
        int e = model_constant(c, n, 2, edge);
        int projection = model_binary(
            c,
            n,
            SHADER_DIVIDE,
            1,
            model_binary(c, n, SHADER_DOT, 1, q, e),
            model_scalar(c, n, squared)
        );
        int h = model_emit(c, n, SHADER_CLAMP, 1, projection, zero, one);
        int offset = model_binary(c, n, SHADER_SUBTRACT, 2, q, model_binary(c, n, SHADER_MULTIPLY, 2, e, h));
        int distance = model_binary(c, n, SHADER_DOT, 1, offset, offset);
        minimum = minimum < 0
                    ? distance
                    : model_binary(c, n, SHADER_MIN, 1, minimum, distance);
        if (convex) {
            int side = model_binary(c, n, SHADER_DOT, 1, q, model_constant(c, n, 2, normal));
            maximum = maximum < 0
                        ? side
                        : model_binary(c, n, SHADER_MAX, 1, maximum, side);
        }
    }
    int sign;
    if (convex) {
        sign = model_binary(
            c,
            n,
            SHADER_SUBTRACT,
            1,
            model_binary(
                c,
                n,
                SHADER_MULTIPLY,
                1,
                model_binary(c, n, SHADER_STEP, 1, maximum, zero),
                model_scalar(c, n, 2)
            ),
            one
        );
    } else {
        sign = polygon_winding(c, n, p);
    }
    return model_binary(c, n, SHADER_MULTIPLY, 1, model_unary(c, n, SHADER_SQRT, 1, minimum), sign);
}
int model_lower_profile(compiler *c, const node *n, int p, bool revolve)
{
    if (c->failed) {
        return 0;
    }
    if (!model_enter(c, n)) {
        c->failed = true;
        return 0;
    }
    int op = model_operation(n), result;
    if (op == MODEL_PROFILE_OFFSET) {
        const node *child = model_child(c->source, n, 0);
        int offset = model_field(c, n, 1);
        int distance = model_lower_profile(c, child, p, false);
        if (revolve) {
            /* A nonzero profile offset changes the radial boundary itself;
             * erosion of an axis-touching profile creates a real hole. */
            int corrected = model_lower_profile(c, child, p, true);
            distance = model_emit(c, n, SHADER_SELECT, 1, corrected, distance, offset);
        }
        result = model_binary(c, n, SHADER_SUBTRACT, 1, distance, offset);
    } else if (op == MODEL_PROFILE_UNION || op == MODEL_PROFILE_INTERSECTION || op == MODEL_PROFILE_DIFFERENCE) {
        int a = model_lower_profile(c, model_child(c->source, n, 0), p, revolve);
        int b = model_lower_profile(c, model_child(c->source, n, 1), p, revolve);
        if (op == MODEL_PROFILE_DIFFERENCE) {
            b = model_binary(c, n, SHADER_MULTIPLY, 1, b, model_scalar(c, n, -1));
        }
        result = model_binary(c, n, op == MODEL_PROFILE_UNION ? SHADER_MIN : SHADER_MAX, 1, a, b);
    } else if (op == MODEL_PROFILE_POLYGON) {
        result = polygon_distance(c, n, p, revolve);
    } else {
        int q = model_binary(c, n, SHADER_SUBTRACT, 2, p, model_field(c, n, 1));
        if (op == MODEL_PROFILE_CIRCLE) {
            result = model_binary(
                c,
                n,
                SHADER_SUBTRACT,
                1,
                model_unary(c, n, SHADER_LENGTH, 1, q),
                model_field(c, n, 0)
            );
        } else {
            if (revolve) {
                int half = model_field(c, n, 0), center = model_field(c, n, 1);
                int hu = model_coordinate(c, n, half, 0),
                    cu = model_coordinate(c, n, center, 0),
                    u = model_coordinate(c, n, p, 0);
                int inner = model_binary(c, n, SHADER_SUBTRACT, 1, cu, hu),
                    outer = model_binary(c, n, SHADER_ADD, 1, cu, hu);
                int upper = model_binary(c, n, SHADER_SUBTRACT, 1, u, outer),
                    lower = model_binary(c, n, SHADER_SUBTRACT, 1, inner, u);
                int axis = model_binary(
                    c,
                    n,
                    SHADER_STEP,
                    1,
                    model_binary(c, n, SHADER_MULTIPLY, 1, inner, model_scalar(c, n, -1)),
                    model_scalar(c, n, 0)
                );
                int corrected = model_binary(
                    c,
                    n,
                    SHADER_ADD,
                    1,
                    model_binary(
                        c,
                        n,
                        SHADER_MULTIPLY,
                        1,
                        lower,
                        model_binary(c, n, SHADER_SUBTRACT, 1, model_scalar(c, n, 1), axis)
                    ),
                    model_binary(c, n, SHADER_MULTIPLY, 1, upper, axis)
                );
                int radial = model_binary(c, n, SHADER_MAX, 1, upper, corrected);
                int vertical = model_binary(
                    c,
                    n,
                    SHADER_SUBTRACT,
                    1,
                    model_unary(c, n, SHADER_ABS, 1, model_coordinate(c, n, q, 1)),
                    model_coordinate(c, n, half, 1)
                );
                q = model_emit(c, n, SHADER_VECTOR2, 2, radial, vertical, -1);
            } else {
                q = model_binary(
                    c,
                    n,
                    SHADER_SUBTRACT,
                    2,
                    model_unary(c, n, SHADER_ABS, 2, q),
                    model_field(c, n, 0)
                );
            }
            result = rectangle_distance(c, n, q);
        }
    }
    c->depth--;
    return result;
}
