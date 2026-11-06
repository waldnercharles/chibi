#include "runtime/shaders/inspection.h"

#include "runtime/shaders/detail.h"
#include "support/array.h"

#include <assert.h>

shader_inspection shader_graph_inspect(const shader_graph *graph)
{
    shader_inspection report = {
        .nodes = acount(graph->nodes),
        .images = acount(graph->images),
        .ramp_vec4s = shader_graph_ramp_vec4_count(graph),
        .texture_samples = shader_detail_texture_samples(graph),
        .detail_expressions = shader_detail_expression_samples(graph)
    };
    bool uniforms[SHADER_PARAMETER_MAX] = { 0 };
    for (int i = 0; i < acount(graph->nodes); i++) {
        const shader_node *n = &graph->nodes[i];
        if (n->op == SHADER_PARAMETER) {
            assert(n->parameter >= 0 && n->parameter < SHADER_PARAMETER_MAX);
            if (!uniforms[n->parameter]) {
                uniforms[n->parameter] = true;
                report.uniforms++;
            }
        }
    }
    return report;
}
