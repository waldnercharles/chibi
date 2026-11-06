#include "graph/model/value_copy.h"
#include "graph/modeling/model_internal.h"
#include "runtime/models/model_bounds.h"
#include "support/xalloc.h"
#include "types/array.h"

#include <stdlib.h>
#include <string.h>

static int node_index(const graph *g, const node *n)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i] == n) {
            return i;
        }
    }
    return -1;
}
model_bounds_plan *model_prepare_bounds(
    const graph *g,
    const model_numeric_input *bindings,
    int binding_count,
    shader_error *error
)
{
    if (acount(g->nodes) > SHADER_NODE_MAX) {
        model_fail(error, 0, "Model construction exceeds the prepared node limit");
        return NULL;
    }
    model_bounds_plan *p = xcalloc(1, sizeof *p);
    p->root = -1;
    int outputs = 0;
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (!model_validate_node(g, n, error) || n->input_count > MODEL_BOUND_INPUT_MAX) {
            model_bounds_plan_free(p);
            return NULL;
        }
        model_bound_node row = { .id = n->id,
                                 .part = n->reference_id,
                                 .op = model_operation(n),
                                 .input_count = n->input_count };
        for (int j = 0; j < n->input_count; j++) {
            const pin_spec *pin = &n->input_pins[j];
            model_bound_input *input = &row.inputs[j];
            *input = (model_bound_input){ .child = -1,
                                          .expression = -1,
                                          .resource = -1,
                                          .type = pin->type };
            if (pin->type == PIN_HANDLE && pin->type_name &&
                (!strcmp(pin->type_name, "Geometry3D") ||
                 !strcmp(pin->type_name, "Profile2D"))) {
                input->child = node_index(g, model_child(g, n, j));
                if (input->child < 0) {
                    model_fail(error, n->id, "Model construction child is outside its prepared graph");
                    model_bounds_plan_free(p);
                    return NULL;
                }
            } else if (pin->type == PIN_F32) {
                input->width = 1;
                input->value[0] = pin->default_value.f;
            } else if (pin->type == PIN_STRUCT && pin->container == PIN_CONTAINER_SINGLE) {
                input->width = !strcmp(pin->type_name, "ShaderVec2") ? 2 : 3;
                if (pin->default_value.ptr) {
                    memcpy(input->value, pin->default_value.ptr, (size_t)input->width * sizeof(float));
                }
            } else if (pin->type == PIN_STRUCT && pin->container == PIN_CONTAINER_ARRAY) {
                const pin_array *points = pin->default_value.ptr;
                row.point_count = points->count;
                int width = !strcmp(points->type_name, "ShaderVec2") ? 2 : 3;
                for (int k = 0; k < points->count; k++) {
                    memcpy(row.points[k], pin_array_data(points, k), (size_t)width * sizeof(float));
                }
            } else if (pin->type == PIN_STR) {
                input->resource = acount(p->resources);
                const char *key = graph_text_cstring(g, pin->default_value);
                if (!key) {
                    model_fail(error, n->id, "Model resource contains embedded NUL");
                    model_bounds_plan_free(p);
                    return NULL;
                }
                apush(p->resources, xstrdup(key));
            } else if (pin->type == PIN_ENUM) {
                input->choice = pin->default_value.e;
            } else if (pin->type == PIN_I32) {
                input->choice = pin->default_value.i;
            } else if (pin->type == PIN_BOOL) {
                input->choice = pin->default_value.b;
            }
            for (int k = 0; k < binding_count; k++) {
                if (bindings[k].node == n->id && bindings[k].pin == j) {
                    input->expression = bindings[k].expression;
                }
            }
        }
        if (row.op == MODEL_OUTPUT) {
            p->root = row.inputs[0].child;
            outputs++;
        }
        apush(p->nodes, row);
    }
    if (outputs != 1) {
        model_fail(error, 0, "Model requires exactly one Model Output");
        model_bounds_plan_free(p);
        return NULL;
    }
    return p;
}
bool model_construction_bounds(const graph *source, model_bounds *out, shader_error *error)
{
    model_bounds_plan *plan = model_prepare_bounds(source, NULL, 0, error);
    if (!plan) {
        return false;
    }
    bool valid = model_bounds_plan_evaluate(plan, NULL, 0, -1, out, error);
    model_bounds_plan_free(plan);
    return valid;
}
