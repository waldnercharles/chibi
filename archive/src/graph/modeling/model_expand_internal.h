#ifndef CHIBI_MODEL_EXPAND_INTERNAL_H
#define CHIBI_MODEL_EXPAND_INTERNAL_H
#include "graph/modeling/model_expand.h"

#define MODEL_EXPAND_DEPTH 128
#define MODEL_ARRAY_MAX 64
#define MODEL_PATH_MAX 4096

typedef struct
{
    int width, expression;
    float values[4];
} number_value;
typedef struct scope
{
    const behavior_asset *asset;
    const graph *graph;
    const struct scope *parent;
    const node *call, *instance;
    char path[MODEL_PATH_MAX];
} scope;
typedef struct
{
    model_expansion *result;
    const behavior_asset *root;
    const behavior_asset *const *catalog;
    int count, depth, work;
    shader_error *error;
    struct
    {
        const graph *graph;
        const node *node, *call, *instance;
    } stack[MODEL_EXPAND_DEPTH];
    const node **visited;
} expander;
typedef struct
{
    number_value position, angles;
} anchor_value;
void model_expand_identity(const scope *, char out[MODEL_PATH_MAX]);
bool model_expand_error(expander *, const scope *, const node *, const char *);
bool model_expand_enter(expander *, const scope *, const node *);
bool model_expand_path(expander *, const scope *, const node *, scope *, char, int);
bool model_expand_function_scope(expander *, const scope *, const node *, int, scope *, const node **, int *);
int model_expand_data_pin(const node *, bool, int);
int model_expand_ordinal(const node *, int);
bool model_expand_number_input(expander *, const scope *, const node *, int, number_value *);
bool model_expand_number_output(expander *, const scope *, const node *, int, number_value *);
bool model_expand_numeric_expression(
    expander *,
    const node *,
    shader_op,
    const number_value *,
    int,
    number_value *
);
bool model_expand_set_number(expander *, const scope *, const node *, node *, int, const number_value *);
node *model_expand_add(expander *, const scope *, const node *, node_desc *);
node *model_expand_geometry_input(expander *, const scope *, const node *, int);
bool model_expand_instance_scope(expander *, const scope *, const node *, scope *);
bool model_expand_anchor_input(expander *, const scope *, const node *, int, anchor_value *);
bool model_expand_anchor_output(expander *, const scope *, const node *, int, anchor_value *);
bool model_expand_anchor_declarations(expander *, const scope *);
node *model_expand_around(expander *, const scope *, const node *, bool);
node *model_expand_attach(expander *, const scope *, const node *);
#endif
