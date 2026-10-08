#include "runtime/models/model_program_internal.h"
#include "runtime/shaders/volume.h"

#include <float.h>
#include <math.h>

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
