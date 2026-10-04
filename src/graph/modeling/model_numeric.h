#ifndef CHIBI_MODEL_NUMERIC_H
#define CHIBI_MODEL_NUMERIC_H
#include "graph/modeling/model_compile.h"

typedef struct
{
    uint64_t node;
    int pin, expression;
} model_numeric_input;
typedef struct
{
    uint64_t node;
    int expression, width;
    float minimum[4], maximum[4];
    char *asset;
} model_numeric_constraint;
typedef struct
{
    shader_graph graph;
    model_parameter *parameters;
    model_numeric_constraint *constraints;
} model_numeric;
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
