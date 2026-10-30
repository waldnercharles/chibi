#include "graph/modeling/model_internal.h"
#include "types/array.h"

#include <float.h>
#include <math.h>
#include <string.h>

static const pin_array *points(const node *n)
{
    return n->input_pins[0].default_value.ptr;
}
static const float *point(const pin_array *p, int i)
{
    return pin_array_data(p, i);
}
bool model_constructive_validate(const node *n, shader_error *error)
{
    int op = model_operation(n);
    if (op == MODEL_SHELL) {
        if (model_number(n, 1) <= 0 || n->input_pins[2].default_value.e < 0 ||
            n->input_pins[2].default_value.e > 2) {
            return model_fail(error, n->id, "Shell requires positive full Thickness and a valid Side");
        }
    }
    if (op != MODEL_TUBE_SWEEP) {
        return true;
    }
    const pin_array *p = points(n);
    bool closed = n->input_pins[2].default_value.b;
    if (!p || p->element_type != PIN_STRUCT || !p->type_name ||
        strcmp(p->type_name, "ShaderVec3") || p->count < (closed ? 3 : 2) ||
        p->count > (closed ? 32 : 33) || model_number(n, 1) <= 0) {
        return model_fail(error, n->id, "Tube Sweep requires positive Radius and at most 32 nondegenerate segments");
    }
    for (int i = 0; i < p->count; i++) {
        pin_value element = pin_array_get(p, i);
        const pin_value *v = &element;
        if (v->type != PIN_STRUCT || v->container != PIN_CONTAINER_SINGLE ||
            !v->type_name || strcmp(v->type_name, "ShaderVec3") || !v->ptr) {
            return model_fail(error, n->id, "Tube points require typed ShaderVec3 values");
        }
        for (int axis = 0; axis < 3; axis++) {
            if (!isfinite(point(p, i)[axis])) {
                return model_fail(error, n->id, "Tube points must be finite");
            }
        }
    }
    for (int i = 0; i < p->count - !closed; i++) {
        double squared = 0;
        const float *a = point(p, i), *b = point(p, (i + 1) % p->count);
        for (int axis = 0; axis < 3; axis++) {
            float delta = b[axis] - a[axis];
            squared += (double)delta * delta;
        }
        if (!isfinite(squared) || squared > FLT_MAX || (float)squared <= 0) {
            return model_fail(error, n->id, "Tube segment exceeds finite arithmetic or repeats an adjacent point");
        }
    }
    return true;
}
int model_lower_constructive(compiler *c, const node *n, int p)
{
    int op = model_operation(n);
    if (op == MODEL_OFFSET || op == MODEL_SHELL) {
        int d = model_lower(c, model_child(c->source, n, 0), p);
        int amount = model_field(c, n, 1);
        if (op == MODEL_OFFSET) {
            return model_binary(c, n, SHADER_SUBTRACT, 1, d, amount);
        }
        int half = model_binary(c, n, SHADER_MULTIPLY, 1, amount, model_scalar(c, n, .5f));
        int side = n->input_pins[2].default_value.e;
        if (side) {
            int offset = side == 1
                           ? half
                           : model_binary(c, n, SHADER_MULTIPLY, 1, half, model_scalar(c, n, -1));
            d = model_binary(c, n, SHADER_SUBTRACT, 1, d, offset);
        }
        return model_binary(c, n, SHADER_SUBTRACT, 1, model_unary(c, n, SHADER_ABS, 1, d), half);
    }
    const pin_array *path = points(n);
    int result = -1;
    bool closed = n->input_pins[2].default_value.b;
    int radius = model_field(c, n, 1), zero = model_scalar(c, n, 0),
        one = model_scalar(c, n, 1);
    for (int i = 0; i < path->count - !closed && !c->failed; i++) {
        const float *a = point(path, i), *b = point(path, (i + 1) % path->count);
        float edge[3];
        double squared = 0;
        for (int axis = 0; axis < 3; axis++) {
            edge[axis] = b[axis] - a[axis];
            squared += (double)edge[axis] * edge[axis];
        }
        int q = model_binary(c, n, SHADER_SUBTRACT, 3, p, model_constant(c, n, 3, a));
        int e = model_constant(c, n, 3, edge);
        int projection = model_binary(
            c,
            n,
            SHADER_DIVIDE,
            1,
            model_binary(c, n, SHADER_DOT, 1, q, e),
            model_scalar(c, n, (float)squared)
        );
        int t = model_emit(c, n, SHADER_CLAMP, 1, projection, zero, one);
        int nearest = model_binary(
            c,
            n,
            SHADER_SUBTRACT,
            3,
            q,
            model_binary(c, n, SHADER_MULTIPLY, 3, e, t)
        );
        int d = model_binary(c, n, SHADER_SUBTRACT, 1, model_unary(c, n, SHADER_LENGTH, 1, nearest), radius);
        result = result < 0 ? d : model_binary(c, n, SHADER_MIN, 1, result, d);
    }
    return result;
}
int model_lower_extrusion(compiler *c, const node *n, int p)
{
    int op = model_operation(n), x = model_coordinate(c, n, p, 0),
        y = model_coordinate(c, n, p, 1), z = model_coordinate(c, n, p, 2), u = x;
    if (op == MODEL_REVOLVE) {
        u = model_unary(c, n, SHADER_LENGTH, 1, model_emit(c, n, SHADER_VECTOR2, 2, x, z, -1));
    }
    int profile_point = model_emit(c, n, SHADER_VECTOR2, 2, u, y, -1);
    int d = model_lower_profile(c, model_child(c->source, n, 0), profile_point, op == MODEL_REVOLVE);
    if (op == MODEL_REVOLVE) {
        return d;
    }
    int depth = model_binary(
        c,
        n,
        SHADER_SUBTRACT,
        1,
        model_unary(c, n, SHADER_ABS, 1, z),
        model_field(c, n, 1)
    );
    if (op == MODEL_BEVELED_EXTRUDE) {
        int chamfer = model_binary(
            c,
            n,
            SHADER_MULTIPLY,
            1,
            model_binary(c, n, SHADER_ADD, 1, model_binary(c, n, SHADER_ADD, 1, d, depth), model_field(c, n, 2)),
            model_scalar(c, n, .7071067811865475f)
        );
        return model_binary(c, n, SHADER_MAX, 1, model_binary(c, n, SHADER_MAX, 1, d, depth), chamfer);
    }
    int rim = -1;
    if (op == MODEL_ROUNDED_EXTRUDE) {
        rim = model_field(c, n, 2);
        d = model_binary(c, n, SHADER_ADD, 1, d, rim);
        depth = model_binary(c, n, SHADER_ADD, 1, depth, rim);
    }
    int pair = model_emit(c, n, SHADER_VECTOR2, 2, d, depth, -1),
        zero = model_scalar(c, n, 0);
    int inside = model_binary(c, n, SHADER_MIN, 1, model_binary(c, n, SHADER_MAX, 1, d, depth), zero);
    int outside = model_unary(c, n, SHADER_LENGTH, 1, model_binary(c, n, SHADER_MAX, 2, pair, zero));
    int result = model_binary(c, n, SHADER_ADD, 1, inside, outside);
    return rim < 0 ? result : model_binary(c, n, SHADER_SUBTRACT, 1, result, rim);
}
