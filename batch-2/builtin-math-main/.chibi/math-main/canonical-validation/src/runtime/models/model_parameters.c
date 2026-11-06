#include "runtime/models/model_program_internal.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool model_program_prepare(
    const model_program *program,
    const float incoming[SHADER_PARAMETER_MAX][4],
    float prepared[SHADER_PARAMETER_MAX][4],
    model_bounds *bounds,
    shader_error *error
)
{
    float payload[SHADER_PARAMETER_MAX][4] = { { 0 } };
    for (int i = 0; i < acount(program->parameters); i++) {
        const model_parameter *p = &program->parameters[i];
        if (p->hidden) {
            continue;
        }
        for (int j = 0; j < p->width; j++) {
            float value = incoming ? incoming[p->slot][j] : p->defaults[j];
            if (!isfinite(value) || value < p->minimum[j] || value > p->maximum[j]) {
                if (error) {
                    error->node = p->node;
                    snprintf(
                        error->message,
                        sizeof error->message,
                        "%.32s: parameter %.40s lies outside its finite "
                        "declared range",
                        p->asset,
                        p->name
                    );
                }
                return false;
            }
            payload[p->slot][j] = value;
        }
    }
    model_bounds result = program->bounds;
    const struct model_bounds_program *p = program->bounds_program;
    if (p) {
        int count = acount(p->numeric.graph.nodes);
        float (*values)[4] = xcalloc(count ? count : 1, sizeof *values);
        shader_sample_context sample = { 0 };
        bool valid = !count ||
                     shader_graph_eval_values(&p->numeric.graph, &sample, payload, values, error);
        for (int i = 0; valid && i < count; i++) {
            const shader_node *n = &p->numeric.graph.nodes[i];
            for (int j = 0; j < n->width; j++) {
                if (!isfinite(values[i][j])) {
                    valid = model_fail(error, n->source_id, "Model numeric expression exceeds finite arithmetic");
                    break;
                }
            }
            if (n->op == SHADER_DIVIDE) {
                for (int j = 0; valid && j < n->width; j++) {
                    int width = p->numeric.graph.nodes[n->input[1]].width;
                    if (values[n->input[1]][width == 1 ? 0 : j] == 0) {
                        valid = model_fail(error, n->source_id, "Model numeric division by zero");
                    }
                }
            }
            if (n->op == SHADER_SQRT && values[n->input[0]][0] < 0) {
                valid = model_fail(error, n->source_id, "Model numeric square root requires a nonnegative value");
            }
        }
        for (int i = 0; valid && i < acount(p->numeric.constraints); i++) {
            const model_numeric_constraint *c = &p->numeric.constraints[i];
            for (int j = 0; j < c->width; j++) {
                if (values[c->expression][j] < c->minimum[j] ||
                    values[c->expression][j] > c->maximum[j]) {
                    valid = model_fail(error, c->node, "Model Instance argument lies outside the referenced parameter range");
                    break;
                }
            }
        }
        for (int i = 0; valid && i < acount(p->rotations); i++) {
            const model_rotation_uniform *r = &p->rotations[i];
            double inverse[3][3], forward[3][3];
            float factor;
            model_implemented_rotation(values[r->expression], inverse, forward, &factor);
            for (int row = 0; row < 3; row++) {
                for (int j = 0; j < 3; j++) {
                    payload[r->slot + row][j] = inverse[row][j];
                }
            }
            payload[r->slot][3] = factor;
        }
        if (valid) {
            valid = model_snapshot_bounds(p, (const float (*)[4])values, &result, error);
        }
        if (valid) {
            valid = model_finite_program(&p->proof, &result, payload, (const float (*)[4])values, count, error);
        }
        if (valid) {
            valid = model_finite_program(
                &p->hit_proof,
                &result,
                payload,
                (const float (*)[4])values,
                p->hit_numeric_count,
                error
            );
        }
        free(values);
        if (!valid) {
            return false;
        }
    }
    memcpy(prepared, payload, sizeof payload);
    *bounds = result;
    return true;
}
bool model_program_parameters(
    const model_program *program,
    float parameters[SHADER_PARAMETER_MAX][4],
    shader_error *error
)
{
    model_bounds bounds;
    return model_program_prepare(program, NULL, parameters, &bounds, error);
}
bool model_program_bounds(
    const model_program *program,
    const float parameters[SHADER_PARAMETER_MAX][4],
    model_bounds *bounds,
    shader_error *error
)
{
    float prepared[SHADER_PARAMETER_MAX][4];
    return model_program_prepare(program, parameters, prepared, bounds, error);
}
bool model_program_anchor(
    const model_program *p,
    int index,
    const float prepared[SHADER_PARAMETER_MAX][4],
    float position[3],
    float angles[3],
    shader_error *error
)
{
    if (index < 0 || index >= acount(p->anchors)) {
        return model_fail(error, 0, "Anchor index is invalid");
    }
    float payload[SHADER_PARAMETER_MAX][4];
    model_bounds bounds;
    if (!model_program_prepare(p, prepared, payload, &bounds, error)) {
        return false;
    }
    const shader_graph *g = &p->bounds_program->numeric.graph;
    float (*values)[4] = xcalloc(acount(g->nodes), sizeof *values);
    shader_sample_context sample = { 0 };
    bool valid = shader_graph_eval_values(g, &sample, payload, values, error);
    if (valid) {
        memcpy(position, values[p->anchors[index].position], 3 * sizeof(float));
        memcpy(angles, values[p->anchors[index].angles], 3 * sizeof(float));
    }
    free(values);
    return valid;
}
