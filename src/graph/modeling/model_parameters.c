#include "graph/modeling/model_internal.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int width(const pin_spec *pin)
{
    if (pin->kind != PIN_DATA || pin->container != PIN_CONTAINER_SINGLE) {
        return 0;
    }
    if (pin->type == PIN_F32 || pin->type == PIN_I32) {
        return 1;
    }
    if (pin->type != PIN_STRUCT || !pin->type_name) {
        return 0;
    }
    if (!strcmp(pin->type_name, "ShaderVec2")) {
        return 2;
    }
    if (!strcmp(pin->type_name, "ShaderVec3")) {
        return 3;
    }
    if (!strcmp(pin->type_name, "ShaderVec4")) {
        return 4;
    }
    return 0;
}
static bool declaration_error(shader_error *err, const node *n, const char *text)
{
    if (err) {
        err->node = n->id;
        snprintf(err->message, sizeof err->message, "%s", text);
    }
    return false;
}
bool model_parameter_get(const node *n, model_parameter_decl *out, shader_error *err)
{
    if (n->disabled || n->desc != &NODE_ModelParameter || n->input_count != 3 ||
        n->output_count != 1 || !n->reference_name || !*n->reference_name) {
        return declaration_error(err, n, "Model Parameter requires a name and Default/Minimum/Maximum declaration");
    }
    if (strlen(n->reference_name) > 255) {
        return declaration_error(err, n, "Model Parameter name exceeds 255 characters");
    }
    if (!strncmp(n->reference_name, "shape.", 6) ||
        !strncmp(n->reference_name, "material.", 9)) {
        return declaration_error(err, n, "Model Parameter name uses a reserved family prefix");
    }
    int w = width(&n->output_pins[0]);
    if (!w || n->output_pins[0].type == PIN_I32) {
        return declaration_error(err, n, "Model Parameter requires float or ShaderVec2/3/4");
    }
    *out = (model_parameter_decl){ .id = n->id,
                                   .name = n->reference_name,
                                   .type = n->output_pins[0].type,
                                   .type_name = n->output_pins[0].type_name,
                                   .width = w };
    float *values[] = { out->defaults, out->minimum, out->maximum };
    for (int i = 0; i < 3; i++) {
        const pin_spec *p = &n->input_pins[i];
        if (p->binding || width(p) != w || p->type != out->type ||
            (out->type_name && (!p->type_name || strcmp(p->type_name, out->type_name)))) {
            return declaration_error(err, n, "Model Parameter declaration values require the same numeric type");
        }
        if (w == 1) {
            values[i][0] = p->default_value.f;
        } else if (p->default_value.ptr) {
            memcpy(values[i], p->default_value.ptr, w * sizeof(float));
        }
        for (int j = 0; j < w; j++) {
            if (!isfinite(values[i][j])) {
                return declaration_error(err, n, "Model Parameter range and default must be finite");
            }
        }
    }
    for (int i = 0; i < w; i++) {
        if (out->minimum[i] > out->maximum[i] || out->defaults[i] < out->minimum[i] ||
            out->defaults[i] > out->maximum[i]) {
            return declaration_error(err, n, "Model Parameter default must lie in its declared ordered range");
        }
    }
    return true;
}

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
