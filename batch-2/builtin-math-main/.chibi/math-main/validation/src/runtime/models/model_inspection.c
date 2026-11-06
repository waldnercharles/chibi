#include "runtime/models/model_inspection.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <string.h>

static int reachable(const shader_graph *graph, int root)
{
    if (root < 0) {
        return 0;
    }
    assert(root < acount(graph->nodes));
    bool *seen = xcalloc((size_t)acount(graph->nodes), sizeof *seen);
    int *pending = NULL;
    apush(pending, root);
    int count = 0;
    while (acount(pending)) {
        int index = pending[acount(pending) - 1];
        asetlen(pending, acount(pending) - 1);
        if (seen[index]) {
            continue;
        }
        seen[index] = true;
        count++;
        const shader_node *node = &graph->nodes[index];
        for (int i = 0; i < shader_op_inputs(node->op); i++) {
            int input = node->input[i];
            if (input >= 0) {
                assert(input < acount(graph->nodes));
                apush(pending, input);
            }
        }
    }
    free(seen);
    afree(pending);
    return count;
}

void model_inspection_free(model_inspection *report)
{
    for (int i = 0; i < acount(report->parts); i++) {
        free(report->parts[i].asset);
        free(report->parts[i].name);
        free(report->parts[i].path);
    }
    afree(report->parts);
    memset(report, 0, sizeof *report);
}

void model_program_inspect(const model_program *program, model_inspection *report)
{
    model_inspection next = { .distance_nodes = acount(program->shader.nodes),
                              .hit_nodes = acount(program->hit_shader.nodes),
                              .material_slots = acount(program->material_slots),
                              .images = acount(program->shader.images),
                              .bounds = program->bounds,
                              .distance_limit = SHADER_NODE_MAX,
                              .hit_limit = SHADER_NODE_MAX,
                              .parameter_limit = SHADER_PARAMETER_MAX,
                              .material_limit = MODEL_MATERIAL_SLOT_MAX };
    for (int i = 0; i < acount(program->parameters); i++) {
        if (program->parameters[i].hidden) {
            next.private_slots++;
        } else {
            next.public_slots++;
        }
    }
    next.ramp_vec4s = shader_graph_ramp_vec4_count(&program->shader);
    for (int i = 0; i < acount(program->parts); i++) {
        const model_part *part = &program->parts[i];
        model_part_inspection value = {
            .node = part->node,
            .asset = xstrdup(part->asset ? part->asset : ""),
            .name = xstrdup(part->name ? part->name : ""),
            .path = xstrdup(part->path ? part->path : ""),
            .material = part->material,
            .distance_nodes = reachable(&program->shader, part->distance)
        };
        apush(next.parts, value);
    }
    model_inspection_free(report);
    *report = next;
}
