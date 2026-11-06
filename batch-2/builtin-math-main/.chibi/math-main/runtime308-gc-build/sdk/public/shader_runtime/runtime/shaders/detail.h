#ifndef CHIBI_SHADER_DETAIL_H
#define CHIBI_SHADER_DETAIL_H
#include "runtime/shaders/program.h"

bool shader_detail_op(shader_op op);
/* Conservative generated sample-call bounds for one shading evaluation.
 * Excludes GPU CSE, disabled branches, geometry marching and lighting. */
int shader_detail_texture_samples(const shader_graph *g);
int shader_detail_expression_samples(const shader_graph *g);
bool shader_detail_validate(const shader_graph *g, const shader_node *n, shader_error *error);
bool shader_detail_eval(
    const shader_graph *g,
    const shader_node *n,
    const shader_sample_context *sample,
    const float (*parameters)[4],
    const float (*values)[4],
    float out[4],
    shader_error *error
);
/* Owned GLSL strings. Helpers contain only sampled upstream expressions;
 * no detail work is inserted into geometry or normal-estimation samples. */
char *shader_detail_glsl_helpers(const shader_graph *g, const char *prefix, shader_error *error);
char *shader_detail_glsl_expression(
    const shader_graph *g,
    const shader_node *n,
    const char *prefix,
    int index,
    const char *a,
    const char *b,
    const char *c,
    const char *d
);
#endif
