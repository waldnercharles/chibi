#include "graph/modeling/model_internal.h"
#include "graph/shader/shader_volume.h"
#include "support/xalloc.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static node_desc *const descriptors[] = { &NODE_ModelSphere,
                                          &NODE_ModelBox,
                                          &NODE_ModelRoundedBox,
                                          &NODE_ModelCylinder,
                                          &NODE_ModelCapsule,
                                          &NODE_ModelTorus,
                                          &NODE_ModelTranslate,
                                          &NODE_ModelRotate,
                                          &NODE_ModelScale,
                                          &NODE_ModelReflect,
                                          &NODE_ModelUnion,
                                          &NODE_ModelIntersection,
                                          &NODE_ModelDifference,
                                          &NODE_ModelSmoothUnion,
                                          &NODE_ModelOutput,
                                          &NODE_ModelPart,
                                          &NODE_ModelProfileCircle,
                                          &NODE_ModelProfileRectangle,
                                          &NODE_ModelProfilePolygon,
                                          &NODE_ModelExtrude,
                                          &NODE_ModelRevolve,
                                          &NODE_ModelProfileOffset,
                                          &NODE_ModelProfileUnion,
                                          &NODE_ModelProfileIntersection,
                                          &NODE_ModelProfileDifference,
                                          &NODE_ModelRoundedExtrude,
                                          &NODE_ModelBeveledExtrude,
                                          &NODE_ModelOffset,
                                          &NODE_ModelShell,
                                          &NODE_ModelTubeSweep,
                                          &NODE_ModelSampledSdf };
bool model_fail(shader_error *error, uint64_t id, const char *message)
{
    if (error) {
        error->node = id;
        snprintf(error->message, sizeof error->message, "%s", message);
    }
    return false;
}
int model_operation(const node *n)
{
    for (int i = 0; i < (int)(sizeof descriptors / sizeof *descriptors); i++) {
        if (n->desc == descriptors[i]) {
            return i;
        }
    }
    return -1;
}
static bool geometry_pin(const pin_spec *pin)
{
    return pin->kind == PIN_DATA && pin->type == PIN_HANDLE &&
           pin->container == PIN_CONTAINER_SINGLE && pin->type_name &&
           (!strcmp(pin->type_name, "Geometry3D") ||
            !strcmp(pin->type_name, "Profile2D")) &&
           !pin->binding && pin->default_value.u64 == 0;
}
float model_number(const node *n, int pin)
{
    return n->input_pins[pin].default_value.f;
}
void model_vector(const node *n, int pin, float out[3])
{
    memset(out, 0, 3 * sizeof *out);
    if (n->input_pins[pin].default_value.ptr) {
        memcpy(
            out,
            n->input_pins[pin].default_value.ptr,
            (!strcmp(n->input_pins[pin].type_name, "ShaderVec2") ? 2 : 3) * sizeof *out
        );
    }
}
bool model_validate_node(const graph *g, const node *n, shader_error *error)
{
    int op = model_operation(n);
    if (op < 0) {
        return model_fail(error, n->id, "Unsupported model construction node");
    }
    if (n->disabled || n->input_count != n->desc->input_count ||
        n->output_count != n->desc->output_count) {
        return model_fail(error, n->id, "Model construction has an invalid or disabled node contract");
    }
    for (int i = 0; i < n->input_count; i++) {
        const pin_spec *p = &n->input_pins[i], *declared = &n->desc->inputs[i];
        node *from;
        int from_pin;
        bool connected = graph_input_source(g, n, i, &from, &from_pin);
        if (n->desc == &NODE_ModelPart && i == 1) {
            if (p->type != PIN_HANDLE || !p->type_name ||
                strcmp(p->type_name, "ModelMaterialSlot") ||
                p->container != PIN_CONTAINER_SINGLE || p->binding ||
                p->default_value.u64 || connected) {
                return model_fail(error, n->id, "Compiled Part material binding must be resolved metadata");
            }
            continue;
        }
        if (declared->type == PIN_HANDLE) {
            if (!geometry_pin(p) || !connected || from_pin != 0 ||
                from->output_count != 1 || !geometry_pin(&from->output_pins[0]) ||
                strcmp(p->type_name, declared->type_name) ||
                strcmp(from->output_pins[0].type_name, declared->type_name) ||
                graph_input_conversion(g, n, i)) {
                return model_fail(error, n->id, "Model Geometry is required as an unevaluated Geometry3D wire");
            }
            continue;
        }
        if (declared->container == PIN_CONTAINER_ARRAY) {
            if (p->binding || connected || p->type != PIN_STRUCT ||
                !p->type_name || strcmp(p->type_name, declared->type_name) ||
                p->container != PIN_CONTAINER_ARRAY) {
                return model_fail(error, n->id, "Polygon Vertices require a literal structural ShaderVec2 array");
            }
            continue;
        }
        if (p->binding || connected || p->type != declared->type ||
            p->container != PIN_CONTAINER_SINGLE) {
            return model_fail(error, n->id, "Model dimensions and transforms require typed literal values");
        }
        if (p->type == PIN_STRUCT) {
            if (!p->type_name || strcmp(p->type_name, declared->type_name)) {
                return model_fail(error, n->id, "Model vector requires ShaderVec3");
            }
            float v[3];
            model_vector(n, i, v);
            for (int axis = 0; axis < (!strcmp(p->type_name, "ShaderVec2") ? 2 : 3);
                 axis++) {
                if (!isfinite(v[axis])) {
                    return model_fail(error, n->id, "Model vector values must be finite");
                }
            }
        }
        if (p->type == PIN_F32 && !isfinite(model_number(n, i))) {
            return model_fail(error, n->id, "Model dimensions must be finite and positive");
        }
    }
    if (n->output_count &&
        (!geometry_pin(&n->output_pins[0]) ||
         strcmp(n->output_pins[0].type_name, n->desc->outputs[0].type_name))) {
        return model_fail(error, n->id, "Model construction requires a Geometry3D output");
    }
    if (op == MODEL_SAMPLED_SDF) {
        shader_node spec = { .parameter = n->input_pins[2].default_value.i,
                             .volume_range = model_number(n, 3),
                             .volume_lipschitz = model_number(n, 4) };
        model_vector(n, 1, spec.value);
        const char *key = n->input_pins[0].default_value.str;
        const char *base = key ? strrchr(key, '/') : NULL;
        bool identity = base && strlen(base + 1) == 24 &&
                        !strcmp(base + 17, ".sdf.png");
        for (int i = 1; identity && i <= 16; i++) {
            identity = (base[i] >= '0' && base[i] <= '9') ||
                       (base[i] >= 'a' && base[i] <= 'f');
        }
        if (!shader_volume_contract(&spec) || !key ||
            strncmp(key, "content/", 8) || strstr(key, "..") || !identity) {
            return model_fail(error, n->id, "Sampled SDF needs finite metadata and a content-relative .sdf.png atlas");
        }
    }
    if (op == MODEL_SPHERE) {
        if (model_number(n, 0) <= 0) {
            return model_fail(error, n->id, "Model Sphere Radius must be finite and positive");
        }
        if (model_number(n, 0) >= sqrtf(FLT_MAX / 3.0f)) {
            return model_fail(error, n->id, "Model Sphere Radius exceeds the finite local distance range");
        }
    }
    if (op == MODEL_BOX || op == MODEL_ROUNDED_BOX || op == MODEL_SCALE) {
        float v[3];
        model_vector(n, op == MODEL_SCALE ? 1 : 0, v);
        float minimum = fminf(v[0], fminf(v[1], v[2]));
        if (minimum <= 0) {
            return model_fail(error, n->id, "Model half sizes and scale Factors must be positive");
        }
        if (op == MODEL_ROUNDED_BOX &&
            (model_number(n, 1) < 0 || model_number(n, 1) > minimum)) {
            return model_fail(error, n->id, "Rounded Box Radius must lie between zero and the smallest outer half size");
        }
    }
    if ((op == MODEL_CYLINDER || op == MODEL_CAPSULE || op == MODEL_TORUS) &&
        (model_number(n, 0) <= 0 || model_number(n, 1) < 0 ||
         (op != MODEL_CAPSULE && model_number(n, 1) == 0))) {
        return model_fail(error, n->id, "Model radii and cylinder half height must be positive; capsule Half Segment may be zero");
    }
    if (op == MODEL_TORUS && model_number(n, 0) <= model_number(n, 1)) {
        return model_fail(error, n->id, "Torus Major Radius must exceed Minor Radius");
    }
    if (op == MODEL_REFLECT && (n->input_pins[1].default_value.e < 0 ||
                                n->input_pins[1].default_value.e > 2)) {
        return model_fail(error, n->id, "Model Reflect Axis must be X, Y or Z");
    }
    if (op == MODEL_SMOOTH_UNION && model_number(n, 2) <= 0) {
        return model_fail(error, n->id, "Smooth Union Smoothness must be positive");
    }
    return model_profile_validate(n, error) && model_constructive_validate(n, error);
}
const node *model_child(const graph *g, const node *n, int pin)
{
    node *from;
    int from_pin;
    graph_input_source(g, n, pin, &from, &from_pin);
    return from;
}
/* Rz * Ry * Rx: forward construction rotation; inverse evaluation transposes it. */
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
bool model_enter(compiler *c, const node *n)
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
bool model_bounds_visit(compiler *c, const node *n, double threshold, bound *out)
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
    if (!model_enter(c, n)) {
        return false;
    }
    int op = model_operation(n);
    bound a = { 0 }, b = { 0 };
    if (model_profile_operation(op)) {
        if (!model_profile_bounds(c, n, threshold, out)) {
            c->depth--;
            return false;
        }
    } else if (op == MODEL_OFFSET || op == MODEL_SHELL || op == MODEL_TUBE_SWEEP) {
        if (!model_constructive_bounds(c, n, threshold, out)) {
            c->depth--;
            return false;
        }
    } else if (op == MODEL_SAMPLED_SDF) {
        float half[3];
        model_vector(n, 1, half);
        for (int axis = 0; axis < 3; axis++) {
            out->min[axis] = -half[axis] - fmax(0, threshold);
            out->max[axis] = half[axis] + fmax(0, threshold);
        }
    } else if (op == MODEL_PART) {
        if (!model_bounds_visit(c, model_child(c->source, n, 0), threshold, out)) {
            c->depth--;
            return false;
        }
    } else if (op >= MODEL_TRANSLATE && op <= MODEL_REFLECT) {
        float v[3] = { 0 };
        if (op != MODEL_REFLECT) {
            model_vector(n, 1, v);
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
        if (!model_bounds_visit(c, model_child(c->source, n, 0), t, &a)) {
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
            int axis = n->input_pins[1].default_value.e;
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
        double t = threshold +
                   (op == MODEL_SMOOTH_UNION ? model_number(n, 2) * .25 : 0);
        if (!model_bounds_visit(c, model_child(c->source, n, 0), t, &a) ||
            !model_bounds_visit(c, model_child(c->source, n, 1), t, &b)) {
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
            h[0] = h[1] = h[2] = model_number(n, 0);
        }
        if (op == MODEL_BOX || op == MODEL_ROUNDED_BOX) {
            model_vector(n, 0, v);
            for (int axis = 0; axis < 3; axis++) {
                h[axis] = v[axis];
            }
        }
        if (op == MODEL_CYLINDER || op == MODEL_CAPSULE) {
            h[0] = h[2] = model_number(n, 0);
            h[1] = model_number(n, 1) + (op == MODEL_CAPSULE ? model_number(n, 0) : 0);
        }
        if (op == MODEL_TORUS) {
            h[0] = h[2] = (double)model_number(n, 0) + model_number(n, 1);
            h[1] = model_number(n, 1);
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
/* Interval propagation proves finite intermediate arithmetic over the complete
 * proxy, including distant operands and inverse-scaled position contexts. */
typedef struct
{
    double lo, hi;
} interval;
static interval product(interval a, interval b)
{
    double v[4] = { a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi };
    interval result = { v[0], v[0] };
    for (int i = 1; i < 4; i++) {
        result.lo = fmin(result.lo, v[i]);
        result.hi = fmax(result.hi, v[i]);
    }
    return result;
}
static interval component_range(
    const shader_graph *g,
    interval ranges[SHADER_NODE_MAX][4],
    int input,
    int axis
)
{
    return ranges[input][g->nodes[input].width == 1 ? 0 : axis];
}
bool model_finite_program(
    const shader_graph *g,
    const model_bounds *bound,
    const float parameters[32][4],
    const float (*numeric)[4],
    int numeric_count,
    shader_error *error
)
{
    interval ranges[SHADER_NODE_MAX][4] = { 0 };
    for (int i = 0; i < acount(g->nodes); i++) {
        const shader_node *n = &g->nodes[i];
        for (int axis = 0; axis < n->width; axis++) {
            if (i < numeric_count) {
                ranges[i][axis] = (interval){ numeric[i][axis], numeric[i][axis] };
                continue;
            }
            interval a = { 0 }, b = { 0 }, value = { 0 };
            if (n->input[0] >= 0) {
                a = component_range(g, ranges, n->input[0], axis);
            }
            if (n->input[1] >= 0) {
                b = component_range(g, ranges, n->input[1], axis);
            }
            switch (n->op) {
                case SHADER_SAMPLED_SDF: {
                    double squared = 0;
                    for (int j = 0; j < 3; j++) {
                        interval p = component_range(g, ranges, n->input[0], j);
                        double maximum = fmax(fabs(p.lo), fabs(p.hi)) + n->value[j];
                        squared += maximum * maximum;
                    }
                    value = (interval){ -n->volume_range,
                                        n->volume_range + sqrt(squared) };
                    break;
                }
                case SHADER_OBJECT_POSITION:
                    value = (interval){ fmin(bound->min[axis], bound->max[axis]),
                                        fmax(bound->min[axis], bound->max[axis]) };
                    break;
                case SHADER_PARAMETER:
                    value = (interval){ parameters[n->parameter][axis],
                                        parameters[n->parameter][axis] };
                    break;
                case SHADER_CONSTANT:
                    value = (interval){ n->value[axis], n->value[axis] };
                    break;
                case SHADER_ADD:
                    value = (interval){ a.lo + b.lo, a.hi + b.hi };
                    break;
                case SHADER_SUBTRACT:
                    value = (interval){ a.lo - b.hi, a.hi - b.lo };
                    break;
                case SHADER_MULTIPLY: value = product(a, b); break;
                case SHADER_DIVIDE:
                    if (b.lo <= 0 && b.hi >= 0) {
                        return model_fail(error, n->source_id, "Model scale divides by zero");
                    }
                    value = product(a, (interval){ 1 / b.hi, 1 / b.lo });
                    break;
                case SHADER_ABS:
                    value = (interval){ a.lo <= 0 && a.hi >= 0
                                            ? 0
                                            : fmin(fabs(a.lo), fabs(a.hi)),
                                        fmax(fabs(a.lo), fabs(a.hi)) };
                    break;
                case SHADER_MIN:
                case SHADER_UNION:
                    value = (interval){ fmin(a.lo, b.lo), fmin(a.hi, b.hi) };
                    break;
                case SHADER_MAX:
                case SHADER_INTERSECTION:
                    value = (interval){ fmax(a.lo, b.lo), fmax(a.hi, b.hi) };
                    break;
                case SHADER_DIFFERENCE:
                    value = (interval){ fmax(a.lo, -b.hi), fmax(a.hi, -b.lo) };
                    break;
                case SHADER_SMOOTH_UNION: {
                    double k = ranges[n->input[2]][0].lo;
                    double delta = fmax(fabs(b.hi - a.lo), fabs(a.hi - b.lo));
                    if (delta / k > FLT_MAX) {
                        return model_fail(error, n->source_id, "Smooth Union ratio exceeds finite distance arithmetic");
                    }
                    value = (interval){ fmin(a.lo, b.lo) - k * .25, fmin(a.hi, b.hi) };
                    break;
                }
                case SHADER_CLAMP: {
                    interval upper = component_range(g, ranges, n->input[2], 0);
                    value = (interval){ fmax(b.lo, fmin(upper.hi, a.lo)),
                                        fmax(b.lo, fmin(upper.hi, a.hi)) };
                    break;
                }
                case SHADER_VECTOR2:
                case SHADER_VECTOR3:
                case SHADER_VECTOR4: value = ranges[n->input[axis]][0]; break;
                case SHADER_X:
                case SHADER_Y:
                case SHADER_Z:
                case SHADER_W:
                    value = ranges[n->input[0]][n->op - SHADER_X];
                    break;
                case SHADER_LENGTH: {
                    double squared = 0;
                    for (int j = 0; j < g->nodes[n->input[0]].width; j++) {
                        interval q = ranges[n->input[0]][j];
                        double largest = fmax(fabs(q.lo), fabs(q.hi));
                        squared += largest * largest;
                    }
                    if (squared > FLT_MAX) {
                        return model_fail(error, n->source_id, "Model exceeds the finite local distance range");
                    }
                    value = (interval){ 0, sqrt(squared) };
                    break;
                }
                case SHADER_DOT: {
                    double magnitude = 0;
                    for (int j = 0; j < g->nodes[n->input[0]].width; j++) {
                        interval q = product(ranges[n->input[0]][j], ranges[n->input[1]][j]);
                        if (n->input[0] == n->input[1]) {
                            interval a = ranges[n->input[0]][j];
                            double lo = a.lo <= 0 && a.hi >= 0
                                          ? 0
                                          : fmin(fabs(a.lo), fabs(a.hi));
                            double hi = fmax(fabs(a.lo), fabs(a.hi));
                            q = (interval){ lo * lo, hi * hi };
                        }
                        value.lo += q.lo;
                        value.hi += q.hi;
                        magnitude += fmax(fabs(q.lo), fabs(q.hi));
                    }
                    if (magnitude > FLT_MAX) {
                        return model_fail(error, n->source_id, "Model rotation exceeds finite arithmetic");
                    }
                    break;
                }
                case SHADER_SELECT:
                    value = (interval){ fmin(a.lo, b.lo), fmax(a.hi, b.hi) };
                    break;
                case SHADER_STEP: value = (interval){ 0, 1 }; break;
                case SHADER_SIN:
                case SHADER_COS: value = (interval){ -1, 1 }; break;
                case SHADER_SQRT:
                    if (a.lo < 0) {
                        return model_fail(error, n->source_id, "Model numeric square root requires a nonnegative value");
                    }
                    value = (interval){ sqrt(a.lo), sqrt(a.hi) };
                    break;
                default:
                    return model_fail(error, n->source_id, "Unsupported model arithmetic proof operation");
            }
            if (!isfinite(value.lo) || !isfinite(value.hi) ||
                fabs(value.lo) > FLT_MAX || fabs(value.hi) > FLT_MAX) {
                return model_fail(error, n->source_id, "Model exceeds finite distance arithmetic");
            }
            ranges[i][axis] = value;
        }
    }
    return true;
}

bool model_construction_bounds(const graph *source, model_bounds *out, shader_error *error)
{
    const node *output = NULL;
    for (int i = 0; i < acount(source->nodes); i++) {
        const node *n = source->nodes[i];
        if (!model_validate_node(source, n, error)) {
            return false;
        }
        if (model_operation(n) == MODEL_OUTPUT) {
            if (output) {
                return model_fail(error, n->id, "Model requires exactly one Model Output");
            }
            output = n;
        }
    }
    if (!output) {
        return model_fail(error, 0, "Model requires exactly one Model Output");
    }
    compiler c = { .source = source, .error = error };
    bound b;
    for (int i = 0; i < acount(source->nodes); i++) {
        const node *n = source->nodes[i];
        if (n != output && !model_bounds_visit(&c, n, 0, &b)) {
            return false;
        }
    }
    const node *root = model_child(source, output, 0);
    if (!model_bounds_visit(&c, root, 0, &b)) {
        return false;
    }
    (*out).empty = b.empty;
    for (int axis = 0; axis < 3; axis++) {
        float lo = (float)b.min[axis], hi = (float)b.max[axis];
        (*out).min[axis] = lo > b.min[axis] ? nextafterf(lo, -INFINITY) : lo;
        (*out).max[axis] = hi < b.max[axis] ? nextafterf(hi, INFINITY) : hi;
    }
    double squared_corner = 0;
    for (int axis = 0; axis < 3; axis++) {
        double magnitude = fmax(fabs((*out).min[axis]), fabs((*out).max[axis]));
        squared_corner += magnitude * magnitude;
    }
    if (squared_corner >= FLT_MAX) {
        return model_fail(error, root->id, "Model bounds exceed the finite local distance range");
    }
    return true;
}

void model_bounds_program_free(struct model_bounds_program *p)
{
    if (!p) {
        return;
    }
    behavior_asset_free(p->asset);
    model_numeric_free(&p->numeric);
    afree(p->inputs);
    for (int i = 0; i < acount(p->sources); i++) {
        free(p->sources[i].asset);
        free(p->sources[i].path);
    }
    afree(p->sources);
    afree(p->rotations);
    shader_graph_free(&p->proof);
    shader_graph_free(&p->hit_proof);
    afree(p->hit_nodes);
    for (int i = 0; i < acount(p->part_frames); i++) {
        shader_graph_free(&p->part_frames[i]);
    }
    afree(p->part_frames);
    free(p);
}

/* A snapshot borrows immutable edges and contracts. Only node and input-pin
 * headers are copied, and evaluated vector payloads borrow the numeric scratch
 * buffer for this call. No asset serialization or shader compilation occurs. */
/* Follow the construction path rather than inverting a rounded composed frame.
 * Existing affine bound rules remain conservative under arbitrarily nested
 * supported transforms; outer boolean/field modifiers are intentionally omitted. */
static bool part_path_bounds(compiler *c, const node *n, int part, int depth, int *work, bound *out, bool *found)
{
    if (depth >= MODEL_DEPTH_MAX || ++*work > MODEL_BOUND_WORK_MAX) {
        return model_fail(c->error, n->id, "Part bounds exceed construction traversal budget");
    }
    if (n->desc == &NODE_ModelPart && n->reference_id == (uint64_t)part + 1) {
        *found = true;
        return model_bounds_visit(c, n, 0, out);
    }
    for (int pin = 0; pin < n->input_count; pin++) {
        if (!geometry_pin(&n->input_pins[pin])) {
            continue;
        }
        const node *child = model_child(c->source, n, pin);
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
        int op = model_operation(n);
        if (op == MODEL_TRANSLATE || op == MODEL_ROTATE || op == MODEL_SCALE ||
            op == MODEL_REFLECT) {
            c->cache_count = 1;
            c->cache[0].source = child;
            c->cache[0].threshold = 0;
            c->cache[0].value = *out;
            return model_bounds_visit(c, n, 0, out);
        }
        return true;
    }
    *found = false;
    return true;
}
static bool snapshot_bounds(
    const struct model_bounds_program *p,
    const float (*values)[4],
    model_bounds *out,
    shader_error *error,
    int part
)
{
    graph view = *p->asset->graph;
    view.nodes = NULL;
    view.id_table = NULL;
    asetlen(view.id_table, acount(p->asset->graph->id_table));
    memset(view.id_table, 0, acount(view.id_table) * sizeof *view.id_table);
    int count = acount(p->asset->graph->nodes), pins = 0;
    for (int i = 0; i < count; i++) {
        pins += p->asset->graph->nodes[i]->input_count;
    }
    node *nodes = xcalloc(count, sizeof *nodes);
    pin_spec *inputs = xcalloc(pins, sizeof *inputs);
    int offset = 0;
    for (int i = 0; i < count; i++) {
        nodes[i] = *p->asset->graph->nodes[i];
        nodes[i].input_pins = inputs + offset;
        memcpy(
            nodes[i].input_pins,
            p->asset->graph->nodes[i]->input_pins,
            nodes[i].input_count * sizeof *inputs
        );
        offset += nodes[i].input_count;
        apush(view.nodes, &nodes[i]);
        view.id_table[nodes[i].id] = &nodes[i];
    }
    for (int i = 0; i < acount(p->inputs); i++) {
        const model_numeric_input *binding = &p->inputs[i];
        for (int j = 0; j < count; j++) {
            if (nodes[j].id == binding->node) {
                pin_spec *pin = &nodes[j].input_pins[binding->pin];
                if (pin->type == PIN_F32) {
                    pin->default_value.f = values[binding->expression][0];
                } else {
                    pin->default_value.ptr = (void *)values[binding->expression];
                }
                break;
            }
        }
    }
    bool valid;
    if (part < 0) {
        valid = model_construction_bounds(&view, out, error);
    } else {
        const node *root = NULL;
        for (int i = 0; i < count; i++) {
            if (nodes[i].desc == &NODE_ModelOutput) {
                root = model_child(&view, &nodes[i], 0);
            }
        }
        compiler c = { .source = &view, .error = error };
        bound b = { .empty = true };
        bool found = false;
        int work = 0;
        valid = root && part_path_bounds(&c, root, part, 0, &work, &b, &found);
        if (valid && !found) {
            b = (bound){ .empty = true };
        }
        if (valid) {
            out->empty = b.empty;
            for (int i = 0; i < 3; i++) {
                float lo = (float)b.min[i], hi = (float)b.max[i];
                out->min[i] = lo > b.min[i] ? nextafterf(lo, -INFINITY) : lo;
                out->max[i] = hi < b.max[i] ? nextafterf(hi, INFINITY) : hi;
            }
        }
    }
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
    afree(view.nodes);
    afree(view.id_table);
    free(inputs);
    free(nodes);
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
