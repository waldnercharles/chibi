#ifndef CHIBI_MODEL_AUTHORING_H
#define CHIBI_MODEL_AUTHORING_H
#include "graph/modeling/model_compile.h"
/* Adds a sparse numeric or material input identified by its declaration and
 * initialized to its current default.
 * Existing overrides retain their value; removal restores inherited defaults. */
int model_instance_override(
    graph *g,
    node *instance,
    const behavior_asset *target,
    const char *name,
    shader_error *error
);
int model_instance_anchor(
    graph *g,
    node *instance,
    const behavior_asset *target,
    const char *name,
    shader_error *error
);

#endif
