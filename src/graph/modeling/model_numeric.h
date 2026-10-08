#ifndef CHIBI_MODEL_NUMERIC_H
#define CHIBI_MODEL_NUMERIC_H
#include "runtime/models/model_program_internal.h"

void model_numeric_init(model_numeric *numeric);
void model_numeric_free(model_numeric *numeric);
int model_numeric_constant(
    model_numeric *numeric,
    int width,
    const float values[4],
    uint64_t source,
    shader_error *error
);
int model_numeric_operation(
    model_numeric *numeric,
    shader_op op,
    int width,
    const int inputs[4],
    uint64_t source,
    shader_error *error
);
int model_numeric_parameter(
    model_numeric *numeric,
    const model_parameter_decl *declaration,
    const char *asset,
    const char *name,
    const char *scope,
    shader_error *error
);
void model_parameter_free(model_parameter *parameter);
#endif
