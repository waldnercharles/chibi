#include "runtime/models/model_bounds.h"

#include "runtime/shaders/volume.h"
#include "support/xalloc.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct
{
    double min[3], max[3];
    bool empty;
} bound;
typedef struct
{
    const model_bounds_plan *plan;
    const model_bound_node *stack[MODEL_DEPTH_MAX];
    int depth, bound_work, cache_count;
    shader_error *error;
    struct
    {
        const model_bound_node *source;
        double threshold;
        bound value;
    } cache[SHADER_NODE_MAX];
} bounds_context;
static bool visit(bounds_context *, const model_bound_node *, double, bound *);
static const model_bound_node *bounds_child(const bounds_context *c, const model_bound_node *n, int pin)
{
    return &c->plan->nodes[n->inputs[pin].child];
}
static float number(const model_bound_node *n, int pin)
{
    return n->inputs[pin].value[0];
}
static void vector(const model_bound_node *n, int pin, float out[3])
{
    memcpy(out, n->inputs[pin].value, 3 * sizeof *out);
}
bool model_profile_operation(int op)
{
    return (op >= MODEL_PROFILE_CIRCLE && op <= MODEL_REVOLVE) ||
           op == MODEL_PROFILE_OFFSET || op == MODEL_PROFILE_UNION ||
           op == MODEL_PROFILE_INTERSECTION || op == MODEL_PROFILE_DIFFERENCE ||
           op == MODEL_ROUNDED_EXTRUDE || op == MODEL_BEVELED_EXTRUDE;
}
static bool profile_bounds(bounds_context *c, const model_bound_node *n, double threshold, bound *out)
{
    int op = n->op;
    *out = (bound){ 0 };
    if (op == MODEL_PROFILE_OFFSET) {
        return visit(c, bounds_child(c, n, 0), fmax(0, threshold + number(n, 1)), out);
    }
    if (op == MODEL_PROFILE_UNION || op == MODEL_PROFILE_INTERSECTION ||
        op == MODEL_PROFILE_DIFFERENCE) {
        bound a, b;
        if (!visit(c, bounds_child(c, n, 0), threshold, &a)) {
            return false;
        }
        *out = a;
        if (op == MODEL_PROFILE_DIFFERENCE) {
            return true;
        }
        if (!visit(c, bounds_child(c, n, 1), threshold, &b)) {
            return false;
        }
        if (op == MODEL_PROFILE_UNION) {
            if (a.empty) {
                *out = b;
                return true;
            }
            if (b.empty) {
                return true;
            }
            for (int axis = 0; axis < 2; axis++) {
                out->min[axis] = fmin(a.min[axis], b.min[axis]);
                out->max[axis] = fmax(a.max[axis], b.max[axis]);
            }
            return true;
        }
        out->empty = a.empty || b.empty;
        for (int axis = 0; axis < 2; axis++) {
            out->min[axis] = fmax(a.min[axis], b.min[axis]);
            out->max[axis] = fmin(a.max[axis], b.max[axis]);
            out->empty |= out->min[axis] >= out->max[axis];
        }
        return true;
    }
    if (op == MODEL_EXTRUDE || op == MODEL_REVOLVE ||
        op == MODEL_ROUNDED_EXTRUDE || op == MODEL_BEVELED_EXTRUDE) {
        const model_bound_node *profile = bounds_child(c, n, 0);
        bound base;
        if (!visit(c, profile, 0, &base)) {
            return false;
        }
        if (op == MODEL_REVOLVE && base.min[0] < 0) {
            return model_fail(c->error, n->id, "Revolve profile radial coordinates must be nonnegative");
        }
        if (!visit(c, profile, threshold, out)) {
            return false;
        }
        if (op != MODEL_REVOLVE) {
            out->min[2] = -number(n, 1) - threshold;
            out->max[2] = number(n, 1) + threshold;
        } else {
            out->min[0] = out->min[2] = -out->max[0];
            out->max[2] = out->max[0];
        }
        return true;
    }
    if (op == MODEL_PROFILE_POLYGON) {

        for (int axis = 0; axis < 2; axis++) {
            out->min[axis] = DBL_MAX;
            out->max[axis] = -DBL_MAX;
        }
        for (int i = 0; i < n->point_count; i++) {
            for (int axis = 0; axis < 2; axis++) {
                out->min[axis] = fmin(out->min[axis], n->points[i][axis]);
                out->max[axis] = fmax(out->max[axis], n->points[i][axis]);
            }
        }
        for (int axis = 0; axis < 2; axis++) {
            out->min[axis] -= threshold;
            out->max[axis] += threshold;
        }
        return true;
    }
    float center[3], half[3];
    vector(n, 1, center);
    if (op == MODEL_PROFILE_CIRCLE) {
        half[0] = half[1] = number(n, 0);
    } else {
        vector(n, 0, half);
    }
    for (int axis = 0; axis < 2; axis++) {
        out->min[axis] = (double)center[axis] - half[axis] - threshold;
        out->max[axis] = (double)center[axis] + half[axis] + threshold;
    }
    return true;
}
static bool constructive_bounds(bounds_context *c, const model_bound_node *n, double threshold, bound *out)
{
    int op = n->op;
    if (op == MODEL_OFFSET || op == MODEL_SHELL) {
        double outer = number(n, 1);
        if (op == MODEL_SHELL) {
            int side = n->inputs[2].choice;
            outer *= side == 0 ? .5 : side == 1 ? 1 : 0;
        }
        return visit(c, bounds_child(c, n, 0), fmax(0, threshold + outer), out);
    }

    *out = (bound){ 0 };
    for (int axis = 0; axis < 3; axis++) {
        out->min[axis] = DBL_MAX;
        out->max[axis] = -DBL_MAX;
        for (int i = 0; i < n->point_count; i++) {
            out->min[axis] = fmin(out->min[axis], n->points[i][axis]);
            out->max[axis] = fmax(out->max[axis], n->points[i][axis]);
        }
        out->min[axis] -= number(n, 1) + threshold;
        out->max[axis] += number(n, 1) + threshold;
    }
    return true;
}
static void rotation(const float angles[3], double m[3][3])
{
    double x = angles[0], y = angles[1], z = angles[2];
    double cx = cos(x), sx = sin(x), cy = cos(y), sy = sin(y), cz = cos(z),
           sz = sin(z);
    m[0][0] = cz * cy;
    m[0][1] = cz * sy * sx - sz * cx;
    m[0][2] = cz * sy * cx + sz * sx;
    m[1][0] = sz * cy;
    m[1][1] = sz * sy * sx + cz * cx;
    m[1][2] = sz * sy * cx - cz * sx;
    m[2][0] = -sy;
    m[2][1] = cy * sx;
    m[2][2] = cy * cx;
}
/* The GPU uses float coefficients. Bound the implemented inverse map rather
 * than an ideal double rotation, and keep its distance map nonexpansive. */
void model_implemented_rotation(
    const float angles[3],
    double inverse[3][3],
    double forward[3][3],
    float *distance_factor
)
{
    double m[3][3];
    rotation(angles, m);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            m[i][j] = (float)m[i][j];
        }
    }
    /* All entries must be rounded before transposition. */
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            inverse[i][j] = m[j][i];
        }
    }
    double cofactor[3][3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            int r0 = (i + 1) % 3, r1 = (i + 2) % 3;
            int c0 = (j + 1) % 3, c1 = (j + 2) % 3;
            cofactor[i][j] = m[r0][c0] * m[r1][c1] - m[r0][c1] * m[r1][c0];
        }
    }
    double determinant = 0;
    for (int j = 0; j < 3; j++) {
        determinant += m[0][j] * cofactor[0][j];
    }
    double norm_squared = 0;
    for (int i = 0; i < 3; i++) {
        double row_sum = 0;
        for (int j = 0; j < 3; j++) {
            forward[i][j] = cofactor[i][j] / determinant;
            double dot = 0;
            for (int k = 0; k < 3; k++) {
                dot += m[i][k] * m[j][k];
            }
            row_sum += fabs(dot);
        }
        norm_squared = fmax(norm_squared, row_sum);
    }
    float factor = (float)fmin(1, 1 / sqrt(norm_squared));
    *distance_factor = nextafterf(factor, 0);
}
static bool enter(bounds_context *c, const model_bound_node *n)
{
    for (int i = 0; i < c->depth; i++) {
        if (c->stack[i] == n) {
            return model_fail(c->error, n->id, "Model construction contains a geometry cycle");
        }
    }
    if (c->depth == MODEL_DEPTH_MAX) {
        return model_fail(c->error, n->id, "Model construction exceeds the supported depth of 128");
    }
    c->stack[c->depth++] = n;
    return true;
}
static bool visit(bounds_context *c, const model_bound_node *n, double threshold, bound *out)
{
    if (++c->bound_work > MODEL_BOUND_WORK_MAX) {
        return model_fail(c->error, n->id, "Model bounds expansion exceeds the supported construction work limit");
    }
    for (int i = 0; i < c->cache_count; i++) {
        if (c->cache[i].source == n && c->cache[i].threshold == threshold) {
            *out = c->cache[i].value;
            return true;
        }
    }
    if (!enter(c, n)) {
        return false;
    }
    int op = n->op;
    bound a = { 0 }, b = { 0 };
    if (model_profile_operation(op)) {
        if (!profile_bounds(c, n, threshold, out)) {
            c->depth--;
            return false;
        }
    } else if (op == MODEL_OFFSET || op == MODEL_SHELL || op == MODEL_TUBE_SWEEP) {
        if (!constructive_bounds(c, n, threshold, out)) {
            c->depth--;
            return false;
        }
    } else if (op == MODEL_SAMPLED_SDF) {
        out->empty = false;
        float half[3];
        vector(n, 1, half);
        for (int axis = 0; axis < 3; axis++) {
            out->min[axis] = -half[axis] - fmax(0, threshold);
            out->max[axis] = half[axis] + fmax(0, threshold);
        }
    } else if (op == MODEL_PART) {
        if (!visit(c, bounds_child(c, n, 0), threshold, out)) {
            c->depth--;
            return false;
        }
    } else if (op >= MODEL_TRANSLATE && op <= MODEL_REFLECT) {
        float v[3] = { 0 };
        if (op != MODEL_REFLECT) {
            vector(n, 1, v);
        }
        double t = threshold;
        if (op == MODEL_SCALE) {
            t /= fminf(v[0], fminf(v[1], v[2]));
        }
        if (op == MODEL_ROTATE) {
            double inverse[3][3], forward[3][3];
            float factor;
            model_implemented_rotation(v, inverse, forward, &factor);
            t /= factor;
        }
        if (!visit(c, bounds_child(c, n, 0), t, &a)) {
            c->depth--;
            return false;
        }
        *out = a;
        if (op == MODEL_TRANSLATE || op == MODEL_SCALE) {
            for (int axis = 0; axis < 3; axis++) {
                out->min[axis] = op == MODEL_TRANSLATE ? a.min[axis] + v[axis]
                                                       : a.min[axis] * v[axis];
                out->max[axis] = op == MODEL_TRANSLATE ? a.max[axis] + v[axis]
                                                       : a.max[axis] * v[axis];
            }
        }
        if (op == MODEL_REFLECT) {
            int axis = n->inputs[1].choice;
            out->min[axis] = -a.max[axis];
            out->max[axis] = -a.min[axis];
        }
        if (op == MODEL_ROTATE && !a.empty) {
            double inverse[3][3], m[3][3];
            float factor;
            model_implemented_rotation(v, inverse, m, &factor);
            for (int axis = 0; axis < 3; axis++) {
                out->min[axis] = DBL_MAX;
                out->max[axis] = -DBL_MAX;
            }
            for (int corner = 0; corner < 8; corner++) {
                for (int axis = 0; axis < 3; axis++) {
                    double p = 0;
                    for (int j = 0; j < 3; j++) {
                        p += m[axis][j] * ((corner & (1 << j)) ? a.max[j] : a.min[j]);
                    }
                    out->min[axis] = fmin(out->min[axis], p);
                    out->max[axis] = fmax(out->max[axis], p);
                }
            }
            double magnitude = 0;
            for (int axis = 0; axis < 3; axis++) {
                magnitude = fmax(magnitude, fmax(fabs(out->min[axis]), fabs(out->max[axis])));
            }
            for (int axis = 0; axis < 3; axis++) {
                out->min[axis] -= magnitude * 4 * FLT_EPSILON;
                out->max[axis] += magnitude * 4 * FLT_EPSILON;
            }
        }
    } else if (op >= MODEL_UNION && op <= MODEL_SMOOTH_UNION) {
        double t = threshold + (op == MODEL_SMOOTH_UNION ? number(n, 2) * .25 : 0);
        if (!visit(c, bounds_child(c, n, 0), t, &a) ||
            !visit(c, bounds_child(c, n, 1), t, &b)) {
            c->depth--;
            return false;
        }
        *out = a;
        if (op == MODEL_UNION || op == MODEL_SMOOTH_UNION) {
            if (a.empty) {
                *out = b;
            }
            if (!a.empty && !b.empty) {
                for (int axis = 0; axis < 3; axis++) {
                    out->min[axis] = fmin(a.min[axis], b.min[axis]);
                    out->max[axis] = fmax(a.max[axis], b.max[axis]);
                }
            }
        }
        if (op == MODEL_INTERSECTION) {
            out->empty = a.empty || b.empty;
            for (int axis = 0; axis < 3; axis++) {
                out->min[axis] = fmax(a.min[axis], b.min[axis]);
                out->max[axis] = fmin(a.max[axis], b.max[axis]);
                out->empty |= out->min[axis] >= out->max[axis];
            }
        }
    } else {
        float v[3] = { 0 };
        double h[3] = { 0 };
        if (op == MODEL_SPHERE) {
            h[0] = h[1] = h[2] = number(n, 0);
        }
        if (op == MODEL_BOX || op == MODEL_ROUNDED_BOX) {
            vector(n, 0, v);
            for (int axis = 0; axis < 3; axis++) {
                h[axis] = v[axis];
            }
        }
        if (op == MODEL_CYLINDER || op == MODEL_CAPSULE) {
            h[0] = h[2] = number(n, 0);
            h[1] = number(n, 1) + (op == MODEL_CAPSULE ? number(n, 0) : 0);
        }
        if (op == MODEL_TORUS) {
            h[0] = h[2] = (double)number(n, 0) + number(n, 1);
            h[1] = number(n, 1);
        }
        *out = (bound){ 0 };
        for (int axis = 0; axis < 3; axis++) {
            out->min[axis] = -h[axis] - threshold;
            out->max[axis] = h[axis] + threshold;
        }
    }
    c->depth--;
    for (int axis = 0; axis < 3; axis++) {
        if (!isfinite(out->min[axis]) || !isfinite(out->max[axis]) ||
            fabs(out->min[axis]) > FLT_MAX || fabs(out->max[axis]) > FLT_MAX ||
            out->max[axis] - out->min[axis] > FLT_MAX) {
            return model_fail(c->error, n->id, "Model bounds exceed the finite supported range");
        }
    }
    if (c->cache_count < SHADER_NODE_MAX) {
        c->cache[c->cache_count].source = n;
        c->cache[c->cache_count].threshold = threshold;
        c->cache[c->cache_count++].value = *out;
    }
    return true;
}
static bool part_path_bounds(
    bounds_context *c,
    const model_bound_node *n,
    int part,
    int depth,
    int *work,
    bound *out,
    bool *found
)
{
    if (depth >= MODEL_DEPTH_MAX || ++*work > MODEL_BOUND_WORK_MAX) {
        return model_fail(c->error, n->id, "Part bounds exceed construction traversal budget");
    }
    if (n->op == MODEL_PART && n->part == (uint64_t)part + 1) {
        *found = true;
        return visit(c, n, 0, out);
    }
    for (int pin = 0; pin < n->input_count; pin++) {
        if (n->inputs[pin].child < 0) {
            continue;
        }
        const model_bound_node *child = bounds_child(c, n, pin);
        if (!child) {
            return model_fail(c->error, n->id, "Part path has a missing construction child");
        }
        bool match = false;
        if (!part_path_bounds(c, child, part, depth + 1, work, out, &match)) {
            return false;
        }
        if (!match) {
            continue;
        }
        *found = true;
        int op = n->op;
        if (op == MODEL_TRANSLATE || op == MODEL_ROTATE || op == MODEL_SCALE ||
            op == MODEL_REFLECT) {
            c->cache_count = 1;
            c->cache[0].source = child;
            c->cache[0].threshold = 0;
            c->cache[0].value = *out;
            return visit(c, n, 0, out);
        }
        return true;
    }
    *found = false;
    return true;
}

static bool validate(const model_bound_node *n, shader_error *error)
{
    int op = n->op;
    static const int counts[] = { 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2,
                                  2, 2, 3, 1, 2, 2, 2, 1, 2, 1, 2,
                                  2, 2, 2, 3, 3, 2, 3, 3, 5 };
    if (op < MODEL_SPHERE || op > MODEL_SAMPLED_SDF || n->input_count != counts[op]) {
        return model_fail(error, n->id, "Model construction row has an invalid input contract");
    }
    bool first_child = (op >= MODEL_TRANSLATE && op <= MODEL_PART) ||
                       op == MODEL_EXTRUDE || op == MODEL_REVOLVE ||
                       (op >= MODEL_PROFILE_OFFSET && op <= MODEL_SHELL);
    bool second_child = (op >= MODEL_UNION && op <= MODEL_SMOOTH_UNION) ||
                        (op >= MODEL_PROFILE_UNION && op <= MODEL_PROFILE_DIFFERENCE);
    if ((first_child && n->inputs[0].child < 0) ||
        (second_child && n->inputs[1].child < 0)) {
        return model_fail(error, n->id, "Model construction has a missing prepared child");
    }
    if ((op == MODEL_PROFILE_POLYGON && (n->point_count < 3 || n->point_count > 32)) ||
        (op == MODEL_TUBE_SWEEP &&
         (n->point_count < (n->inputs[2].choice ? 3 : 2) ||
          n->point_count > (n->inputs[2].choice ? 32 : 33)))) {
        return model_fail(error, n->id, "Model construction has an invalid prepared point count");
    }
    if (op == MODEL_SAMPLED_SDF) {
        shader_node spec = { .parameter = n->inputs[2].choice,
                             .volume_range = number(n, 3),
                             .volume_lipschitz = number(n, 4) };
        vector(n, 1, spec.value);
        if (!shader_volume_contract(&spec)) {
            return model_fail(error, n->id, "Sampled SDF needs finite prepared metadata");
        }
    }
    for (int i = 0; i < n->input_count; i++) {
        for (int j = 0; j < n->inputs[i].width; j++) {
            if (!isfinite(n->inputs[i].value[j])) {
                return model_fail(error, n->id, "Model dimensions and transforms must be finite");
            }
        }
    }
    if (op == MODEL_SPHERE) {
        if (number(n, 0) <= 0) {
            return model_fail(error, n->id, "Model Sphere Radius must be finite and positive");
        }
        if (number(n, 0) >= sqrtf(FLT_MAX / 3.0f)) {
            return model_fail(error, n->id, "Model Sphere Radius exceeds the finite local distance range");
        }
    }
    if (op == MODEL_BOX || op == MODEL_ROUNDED_BOX || op == MODEL_SCALE) {
        float v[3];
        vector(n, op == MODEL_SCALE ? 1 : 0, v);
        float minimum = fminf(v[0], fminf(v[1], v[2]));
        if (minimum <= 0) {
            return model_fail(error, n->id, "Model half sizes and scale Factors must be positive");
        }
        if (op == MODEL_ROUNDED_BOX && (number(n, 1) < 0 || number(n, 1) > minimum)) {
            return model_fail(error, n->id, "Rounded Box Radius must lie between zero and the smallest outer half size");
        }
    }
    if ((op == MODEL_CYLINDER || op == MODEL_CAPSULE || op == MODEL_TORUS) &&
        (number(n, 0) <= 0 || number(n, 1) < 0 ||
         (op != MODEL_CAPSULE && number(n, 1) == 0))) {
        return model_fail(error, n->id, "Model radii and cylinder half height must be positive; capsule Half Segment may be zero");
    }
    if (op == MODEL_TORUS && number(n, 0) <= number(n, 1)) {
        return model_fail(error, n->id, "Torus Major Radius must exceed Minor Radius");
    }
    if (op == MODEL_REFLECT && (n->inputs[1].choice < 0 || n->inputs[1].choice > 2)) {
        return model_fail(error, n->id, "Model Reflect Axis must be X, Y or Z");
    }
    if (op == MODEL_SMOOTH_UNION && number(n, 2) <= 0) {
        return model_fail(error, n->id, "Smooth Union Smoothness must be positive");
    }

    if (op == MODEL_PROFILE_CIRCLE && number(n, 0) <= 0) {
        return model_fail(error, n->id, "Profile Circle Radius must be positive");
    }
    if (op == MODEL_PROFILE_RECTANGLE &&
        (number(n, 0) <= 0 || n->inputs[0].value[1] <= 0)) {
        return model_fail(error, n->id, "Profile Rectangle Half Size must be positive");
    }
    if ((op == MODEL_EXTRUDE || op == MODEL_ROUNDED_EXTRUDE || op == MODEL_BEVELED_EXTRUDE) &&
        number(n, 1) <= 0) {
        return model_fail(error, n->id, "Extrude Half Depth must be positive");
    }
    if ((op == MODEL_ROUNDED_EXTRUDE || op == MODEL_BEVELED_EXTRUDE) &&
        (number(n, 2) <= 0 || number(n, 2) > number(n, 1))) {
        return model_fail(error, n->id, "Extrusion edge width must be positive and at most Half Depth");
    }
    if (op == MODEL_SHELL &&
        (number(n, 1) <= 0 || n->inputs[2].choice < 0 || n->inputs[2].choice > 2)) {
        return model_fail(error, n->id, "Shell requires positive full Thickness and a valid Side");
    }
    if (op == MODEL_TUBE_SWEEP && number(n, 1) <= 0) {
        return model_fail(error, n->id, "Tube Sweep requires positive Radius");
    }
    return true;
}
void model_bounds_plan_free(model_bounds_plan *p)
{
    if (!p) {
        return;
    }
    afree(p->nodes);
    for (int i = 0; i < acount(p->resources); i++) {
        free(p->resources[i]);
    }
    afree(p->resources);
    free(p);
}
bool model_bounds_plan_equal(const model_bounds_plan *a, const model_bounds_plan *b)
{
    if (!a || !b || a->root != b->root || acount(a->nodes) != acount(b->nodes) ||
        acount(a->resources) != acount(b->resources)) {
        return false;
    }
    for (int i = 0; i < acount(a->resources); i++) {
        if (strcmp(a->resources[i], b->resources[i])) {
            return false;
        }
    }
    for (int i = 0; i < acount(a->nodes); i++) {
        const model_bound_node *x = &a->nodes[i], *y = &b->nodes[i];
        if (x->id != y->id || x->part != y->part || x->op != y->op ||
            x->input_count != y->input_count || x->point_count != y->point_count) {
            return false;
        }
        for (int j = 0; j < x->input_count; j++) {
            const model_bound_input *u = &x->inputs[j], *v = &y->inputs[j];
            if (u->child != v->child || u->expression != v->expression ||
                u->resource != v->resource || u->width != v->width ||
                u->type != v->type || u->choice != v->choice ||
                memcmp(u->value, v->value, sizeof u->value)) {
                return false;
            }
        }
        if (memcmp(x->points, y->points, (size_t)x->point_count * sizeof x->points[0])) {
            return false;
        }
    }
    return true;
}
bool model_bounds_plan_evaluate(
    const model_bounds_plan *p,
    const float (*values)[4],
    int value_count,
    int part,
    model_bounds *out,
    shader_error *error
)
{
    int count = acount(p->nodes);
    if (p->root < 0 || p->root >= count || count > SHADER_NODE_MAX) {
        return model_fail(error, 0, "Model construction plan has an invalid root or node count");
    }
    model_bound_node *nodes = xmalloc((size_t)count * sizeof *nodes);
    memcpy(nodes, p->nodes, (size_t)count * sizeof *nodes);
    bool valid = true;
    for (int i = 0; valid && i < count; i++) {
        model_bound_node *n = &nodes[i];
        if (n->input_count < 0 || n->input_count > MODEL_BOUND_INPUT_MAX ||
            n->point_count < 0 || n->point_count > MODEL_BOUND_POINT_MAX ||
            n->op < MODEL_SPHERE || n->op > MODEL_SAMPLED_SDF) {
            valid = model_fail(error, n->id, "Model construction plan has invalid row metadata");
            break;
        }
        for (int j = 0; valid && j < n->input_count; j++) {
            model_bound_input *input = &n->inputs[j];
            if (input->child < -1 || input->child >= count ||
                input->width < 0 || input->width > 4 || input->expression < -1 ||
                input->resource < -1 || input->resource >= acount(p->resources) ||
                (input->expression >= 0 &&
                 (!values || input->expression >= value_count))) {
                valid = model_fail(error, n->id, "Model construction plan has an invalid child or numeric binding");
                break;
            }
            if (input->expression >= 0) {
                memcpy(
                    input->value,
                    values[input->expression],
                    (size_t)input->width * sizeof *input->value
                );
            }
        }
        valid = valid && validate(n, error);
    }
    model_bounds_plan view = { .nodes = nodes, .resources = p->resources, .root = p->root };
    bounds_context c = { .plan = &view, .error = error };
    bound b = { .empty = true };
    if (valid && part < 0) {
        for (int i = 0; valid && i < count; i++) {
            if (nodes[i].op != MODEL_OUTPUT) {
                valid = visit(&c, &nodes[i], 0, &b);
            }
        }
        if (valid) {
            valid = visit(&c, &nodes[p->root], 0, &b);
        }
    } else if (valid) {
        bool found = false;
        int work = 0;
        valid = part_path_bounds(&c, &nodes[p->root], part, 0, &work, &b, &found);
        if (valid && !found) {
            b = (bound){ .empty = true };
        }
    }
    model_bounds result = { .empty = b.empty };
    double squared_corner = 0;
    for (int axis = 0; valid && axis < 3; axis++) {
        float lo = (float)b.min[axis], hi = (float)b.max[axis];
        result.min[axis] = lo > b.min[axis] ? nextafterf(lo, -INFINITY) : lo;
        result.max[axis] = hi < b.max[axis] ? nextafterf(hi, INFINITY) : hi;
        double magnitude = fmax(fabs(result.min[axis]), fabs(result.max[axis]));
        squared_corner += magnitude * magnitude;
    }
    if (valid && part < 0 && squared_corner >= FLT_MAX) {
        valid = model_fail(error, nodes[p->root].id, "Model bounds exceed the finite local distance range");
    }
    free(nodes);
    if (valid) {
        *out = result;
    }
    return valid;
}

static bool snapshot_bounds(
    const struct model_bounds_program *p,
    const float (*values)[4],
    model_bounds *out,
    shader_error *error,
    int part
)
{
    bool valid = model_bounds_plan_evaluate(
        p->plan,
        values,
        acount(p->numeric.graph.nodes),
        part,
        out,
        error
    );
    if (!valid && error) {
        for (int i = 0; i < acount(p->sources); i++) {
            if (p->sources[i].generated == error->node) {
                error->node = p->sources[i].authored;
                char text[sizeof error->message];
                snprintf(text, sizeof text, "%s", error->message);
                snprintf(
                    error->message,
                    sizeof error->message,
                    "%.24s [%.24s]: %.72s",
                    p->sources[i].asset,
                    p->sources[i].path,
                    text
                );
                break;
            }
        }
    }
    return valid;
}
bool model_snapshot_bounds(
    const struct model_bounds_program *p,
    const float (*values)[4],
    model_bounds *out,
    shader_error *error
)
{
    return snapshot_bounds(p, values, out, error, -1);
}
bool model_program_part_bounds(
    const model_program *p,
    const float prepared[32][4],
    int part,
    model_bounds *out,
    shader_error *error
)
{
    if (part < 0) {
        return model_program_bounds(p, prepared, out, error);
    }
    if (part >= acount(p->parts)) {
        return model_fail(error, 0, "Part index is invalid");
    }
    float payload[32][4];
    model_bounds whole;
    if (!model_program_prepare(p, prepared, payload, &whole, error)) {
        return false;
    }
    const shader_graph *g = &p->bounds_program->numeric.graph;
    float (*values)[4] = xcalloc(acount(g->nodes), sizeof *values);
    shader_sample_context sample = { 0 };
    model_bounds local;
    bool valid = (!acount(g->nodes) ||
                  shader_graph_eval_values(g, &sample, payload, values, error)) &&
                 snapshot_bounds(p->bounds_program, values, &local, error, part);
    free(values);
    if (!valid) {
        return false;
    }
    *out = local;
    return true;
}
