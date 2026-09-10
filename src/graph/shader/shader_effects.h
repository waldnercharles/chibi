#ifndef CHIBI_SHADER_EFFECTS_H
#define CHIBI_SHADER_EFFECTS_H
#include "graph/shader/shader_graph.h"

float shader_noise(const float *position, int width, float scale);
void shader_effect_eval(
    const shader_graph *g,
    const shader_node *n,
    const float (*values)[4],
    float out[4]
);
const char *shader_effects_glsl();
#endif
