#ifndef CHIBI_SHADER_EFFECTS_H
#define CHIBI_SHADER_EFFECTS_H
#include "runtime/shaders/program.h"

float shader_noise(const float *position, int width, float scale);
void shader_effect_eval(
    const shader_graph *g,
    const shader_node *n,
    const float (*values)[4],
    float out[4]
);
const char *shader_effects_glsl();
/* Star Field's GPU function, which reads screen derivatives. */
const char *shader_star_field_glsl();
#endif
