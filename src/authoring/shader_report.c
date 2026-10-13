#include "authoring/shader_report.h"

#include "graph/shader/shader_graph.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <math.h>
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
        const node *source = graph_node_by_id(asset->graph, ir->source_id);
        const char *name = source && source->reference_name ? source->reference_name
                                                            : "";
        free(fresh.names[ir->parameter]);
        fresh.names[ir->parameter] = xstrdup(name);
        fresh.widths[ir->parameter] = ir->width;
        memcpy(fresh.defaults[ir->parameter], ir->value, sizeof ir->value);
    }
    shader_authoring_report_free(report);
    *report = fresh;
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
