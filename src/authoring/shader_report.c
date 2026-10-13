#include "authoring/shader_report.h"

#include "graph/authoring/graph_edit.h"
#include "graph/shader/shader_graph.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void shader_authoring_report_init(shader_authoring_report *report)
{
    *report = (shader_authoring_report){ 0 };
    shader_graph_init(&report->program);
}
void shader_authoring_report_free(shader_authoring_report *report)
{
    shader_graph_free(&report->program);
    for (int i = 0; i < SHADER_PARAMETER_MAX; i++) {
        free(report->names[i]);
    }
    shader_authoring_report_init(report);
}
static const node *parameter_source(const behavior_asset *asset, uint64_t id)
{
    for (int i = 0; i < acount(asset->graphs); i++) {
        node *source = graph_node_by_id(asset->graphs[i], id);
        if (source && !strcmp(source->desc->name, "Shader Parameter")) {
            return source;
        }
    }
    return NULL;
}

static shader_authoring_presentation presentation(
    const behavior_asset *asset,
    const shader_graph *program,
    int index,
    const node *source
)
{
    const shader_node *parameter = &program->nodes[index];
    if (parameter->width == 1) {
        return SHADER_AUTHORING_SCALAR;
    }
    bool color = source && source->output_count &&
                 (source->output_pins[0].flags & PIN_FLAG_COLOR);
    if (asset->domain == BEHAVIOR_DOMAIN_MATERIAL) {
        color |= program->output[SHADER_SURFACE_BASE_COLOR] == index ||
                 program->output[SHADER_SURFACE_EMISSION] == index;
    }
    return color && parameter->width >= 3 ? SHADER_AUTHORING_COLOR
                                          : SHADER_AUTHORING_VECTOR;
}

bool shader_authoring_inspect(const behavior_asset *asset, shader_authoring_report *report, shader_error *error)
{
    if (asset->domain != BEHAVIOR_DOMAIN_MATERIAL &&
        asset->domain != BEHAVIOR_DOMAIN_SHAPE) {
        *error = (shader_error){ 0 };
        snprintf(error->message, sizeof error->message, "Inspect a material or shape; shader libraries need a consumer.");
        return false;
    }
    if (asset->domain == BEHAVIOR_DOMAIN_SHAPE &&
        (!(asset->bounds[0] > 0) || !(asset->bounds[1] > 0) ||
         !isfinite(asset->bounds[0]) || !isfinite(asset->bounds[1]))) {
        *error = (shader_error){ 0 };
        snprintf(error->message, sizeof error->message, "Shape bounds must be finite positive half extents.");
        return false;
    }
    shader_authoring_report fresh;
    shader_authoring_report_init(&fresh);
    if (!shader_graph_from_graph(asset->graph, &fresh.program, error)) {
        shader_authoring_report_free(&fresh);
        return false;
    }
    fresh.alpha_mode = shader_graph_alpha_mode(asset->graph);
    fresh.costs = shader_graph_inspect(&fresh.program);
    for (int i = 0; i < acount(fresh.program.nodes); i++) {
        const shader_node *ir = &fresh.program.nodes[i];
        if (ir->op != SHADER_PARAMETER) {
            continue;
        }
        const node *source = parameter_source(asset, ir->source_id);
        const char *name = source && source->reference_name ? source->reference_name
                                                            : "";
        free(fresh.names[ir->parameter]);
        fresh.names[ir->parameter] = xstrdup(name);
        fresh.widths[ir->parameter] = ir->width;
        fresh.source_nodes[ir->parameter] = source ? source->id : 0;
        shader_authoring_presentation kind = presentation(asset, &fresh.program, i, source);
        if (fresh.presentation[ir->parameter] != SHADER_AUTHORING_COLOR) {
            fresh.presentation[ir->parameter] = kind;
        }
        memcpy(fresh.defaults[ir->parameter], ir->value, sizeof ir->value);
    }
    shader_authoring_report_free(report);
    *report = fresh;
    return true;
}
static bool edit_error(shader_error *error, uint64_t node, const char *message)
{
    *error = (shader_error){ .node = node };
    snprintf(error->message, sizeof error->message, "%s", message);
    return false;
}

static int parameter_width(const pin_spec *pin)
{
    if (pin->kind != PIN_DATA || pin->container != PIN_CONTAINER_SINGLE) {
        return 0;
    }
    if (pin->type == PIN_F32 || pin->type == PIN_F64 || pin->type == PIN_I32 ||
        pin->type == PIN_BOOL) {
        return 1;
    }
    if (pin->type != PIN_STRUCT || !pin->type_name) {
        return 0;
    }
    if (!strcmp(pin->type_name, "ShaderVec2") || !strcmp(pin->type_name, "v2") ||
        !strcmp(pin->type_name, "vec2") || !strcmp(pin->type_name, "CF_V2")) {
        return 2;
    }
    if (!strcmp(pin->type_name, "ShaderVec3")) {
        return 3;
    }
    return !strcmp(pin->type_name, "ShaderVec4") ? 4 : 0;
}

bool shader_authoring_set_default(
    behavior_asset *asset,
    int slot,
    const float *values,
    int width,
    shader_error *error
)
{
    if (slot < 0 || slot >= SHADER_PARAMETER_MAX || width < 1 || width > 4 || !values) {
        return edit_error(error, 0, "Edit an exposed parameter slot with one through four components.");
    }
    for (int i = 0; i < width; i++) {
        if (!isfinite(values[i])) {
            return edit_error(error, 0, "Shader parameter defaults must be finite.");
        }
    }
    graph *owner = NULL;
    node *source = NULL;
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *g = asset->graphs[i];
        for (int j = 0; j < acount(g->nodes); j++) {
            node *candidate = g->nodes[j];
            if (strcmp(candidate->desc->name, "Shader Parameter") ||
                candidate->reference_id != (uint64_t)slot) {
                continue;
            }
            if (source) {
                return edit_error(error, candidate->id, "Parameter slot has more than one source declaration.");
            }
            source = candidate;
            owner = g;
        }
    }
    if (!source || !source->output_count) {
        return edit_error(error, 0, "Parameter source declaration is unavailable.");
    }
    uint64_t id = source->id;
    const pin_spec *pin = &source->output_pins[0];
    if (parameter_width(pin) != width) {
        return edit_error(error, id, "Parameter default width differs from its source declaration.");
    }
    pin_value value = { .type = pin->type, .type_name = pin->type_name };
    switch (pin->type) {
        case PIN_F32: value.f = values[0]; break;
        case PIN_F64: value.d = values[0]; break;
        case PIN_I32:
            if ((double)values[0] < INT32_MIN || (double)values[0] > INT32_MAX ||
                truncf(values[0]) != values[0]) {
                return edit_error(error, id, "Integer parameter default must be a representable whole number.");
            }
            value.i = (int32_t)values[0];
            break;
        case PIN_BOOL:
            if (values[0] != 0 && values[0] != 1) {
                return edit_error(error, id, "Boolean parameter default must be zero or one.");
            }
            value.b = values[0] != 0;
            break;
        case PIN_STRUCT: value.ptr = (void *)values; break;
        default:
            return edit_error(error, id, "Parameter declaration has an unsupported default type.");
    }
    if (graph_set_pin_default(owner, source, 1, 0, &value)) {
        return edit_error(error, id, "Parameter declaration is not editable.");
    }
    return true;
}

shader_sample_context shader_authoring_center_context(const behavior_asset *asset)
{
    float width = asset->domain == BEHAVIOR_DOMAIN_SHAPE ? asset->bounds[0] : 100;
    float height = asset->domain == BEHAVIOR_DOMAIN_SHAPE ? asset->bounds[1] : 100;
    return (shader_sample_context){
        .uv = { .5f, .5f },
        .normal = { 0, 1, 0 },
        .view_direction = { 0, 1, 0 },
        .object_from_world = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
        .part_from_world = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
        .uv_from_world = { { 1 / (2 * width), 0, 0 }, { 0, 0, -1 / (2 * height) } },
    };
}

bool shader_authoring_sample(
    const shader_authoring_report *report,
    const shader_sample_context *context,
    const float parameters[SHADER_PARAMETER_MAX][4],
    float outputs[SHADER_OUTPUT_MAX][4],
    shader_error *error
)
{
    if (!shader_graph_eval_outputs(
            &report->program,
            context,
            parameters ? parameters : report->defaults,
            outputs,
            error
        )) {
        return false;
    }
    for (int i = 0; i < report->program.output_count; i++) {
        for (int j = 0; j < 4; j++) {
            if (!isfinite(outputs[i][j])) {
                *error = (shader_error){ 0 };
                snprintf(error->message, sizeof error->message, "CPU shader sample produced a nonfinite output.");
                return false;
            }
        }
    }
    return true;
}
