#include "runtime/models/model_program_internal.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

bool model_program_hit(
    const model_program *program,
    const float prepared[SHADER_PARAMETER_MAX][4],
    const float object_position[3],
    model_hit *hit,
    shader_error *error
)
{
    const struct model_bounds_program *bp = program->bounds_program;
    float defaults[SHADER_PARAMETER_MAX][4];
    model_bounds bounds;
    if (!prepared) {
        if (!model_program_prepare(program, NULL, defaults, &bounds, error)) {
            return false;
        }
        prepared = defaults;
    }
    shader_sample_context sample = { .resolve_image = program->resolve_image,
                                     .image_userdata = program->image_userdata };
    memcpy(sample.object_position, object_position, sizeof sample.object_position);
    for (int i = 0; i < 3; i++) {
        if (!isfinite(object_position[i])) {
            return model_fail(error, 0, "Model hit position must be finite");
        }
    }
    const shader_graph *g = &bp->hit_proof;
    float (*values)[4] = xcalloc(acount(g->nodes), sizeof *values);
    bool valid = shader_graph_eval_values(g, &sample, prepared, values, error);
    for (int i = 0; valid && i < acount(g->nodes); i++) {
        for (int j = 0; j < g->nodes[i].width; j++) {
            if (!isfinite(values[i][j])) {
                valid = model_fail(error, g->nodes[i].source_id, "Model hit exceeds finite arithmetic");
                break;
            }
        }
    }
    model_hit result = { .part = -1, .material = -1 };
    if (valid) {
        result.part = (int)values[g->output[0]][0];
        memcpy(result.position, values[g->output[1]], sizeof result.position);
        if (result.part < -1 || result.part >= acount(program->parts)) {
            valid = model_fail(error, 0, "Model hit owner is outside the compiled part table");
        } else if (result.part >= 0) {
            result.material = program->parts[result.part].material;
        }
    }
    free(values);
    if (!valid) {
        return false;
    }
    if (!model_program_frame(program, result.part, prepared, result.part_from_object, error)) {
        return false;
    }
    *hit = result;
    return true;
}

bool model_program_part_shaders(
    const model_program *p,
    int part,
    shader_graph *distance,
    shader_graph *hit,
    shader_error *error
)
{
    if (distance == hit) {
        return model_fail(error, 0, "Part shader outputs must be distinct");
    }
    if (part < 0 || part >= acount(p->parts) || p->parts[part].distance < 0) {
        return model_fail(error, 0, "Selected Part has no compiled geometry");
    }
    shader_graph a, b;
    shader_graph_init(&a);
    shader_graph_init(&b);
    const struct model_bounds_program *bp = p->bounds_program;
    if (!shader_graph_clone(&bp->part_distance[part], &a, error) ||
        !shader_graph_clone(&bp->part_hit[part], &b, error)) {
        shader_graph_free(&a);
        shader_graph_free(&b);
        return false;
    }
    shader_graph_free(distance);
    shader_graph_free(hit);
    *distance = a;
    *hit = b;
    return true;
}
