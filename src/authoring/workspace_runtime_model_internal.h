#ifndef CHIBI_WORKSPACE_RUNTIME_MODEL_INTERNAL_H
#define CHIBI_WORKSPACE_RUNTIME_MODEL_INTERNAL_H
#include "authoring/workspace_runtime_model.h"
/* Validated model presenter state, consumed at the next render boundary. */
void workspace_runtime_model_present(
    workspace_runtime *,
    int object,
    int part,
    bool section,
    const float normal[3],
    float offset
);
#endif
