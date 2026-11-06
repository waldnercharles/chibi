#ifndef CHIBI_MODEL_EXPAND_H
#define CHIBI_MODEL_EXPAND_H

#include "graph/modeling/model_compile.h"
#include "graph/modeling/model_numeric.h"

typedef struct
{
    behavior_asset *asset;
    model_source *sources;
    model_part *parts;
    model_anchor *anchors;
    char **dependencies;
    model_material_bindings materials;
    model_numeric numeric;
    model_numeric_input *inputs;
} model_expansion;
bool model_expand(
    const behavior_asset *source,
    const behavior_asset *const *catalog,
    int count,
    model_expansion *result,
    shader_error *error
);
void model_expansion_free(model_expansion *expansion);

#endif
