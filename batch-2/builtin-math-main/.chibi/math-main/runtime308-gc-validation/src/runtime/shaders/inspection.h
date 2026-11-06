#ifndef CHIBI_SHADER_INSPECTION_H
#define CHIBI_SHADER_INSPECTION_H
#include "runtime/shaders/program.h"
typedef struct
{
    int nodes, uniforms, images, ramp_vec4s;
    int texture_samples, detail_expressions;
} shader_inspection;
/* Static emitted program work. Sample calls are conservative bounds per
 * shading evaluation, not measured GPU reads or complete raymarch costs. */
shader_inspection shader_graph_inspect(const shader_graph *graph);
#endif
