#ifndef CHIBI_WORKSPACE_RUNTIME_MODEL_H
#define CHIBI_WORKSPACE_RUNTIME_MODEL_H
#include "authoring/workspace_runtime.h"
/* Applied runtime model snapshots and validated transient parameter edits. */
typedef struct
{
    float min[3], max[3];
    bool empty;
} workspace_model_bounds;
typedef struct
{
    int slot, width;
    bool write;
    uint64_t node;
    const char *asset, *scope;
    float values[4];
} workspace_model_parameter_edit;
typedef struct
{
    const char *name, *asset, *scope, *type_name;
    uint64_t node;
    int width;
    bool found, hidden;
    float values[4], minimum[4], maximum[4];
} workspace_model_parameter;
bool workspace_runtime_model_snapshot(
    workspace_runtime *,
    int session,
    int object,
    int part,
    workspace_model_bounds *,
    char *,
    size_t
);
bool workspace_runtime_model_inspection(
    workspace_runtime *,
    int session,
    int object,
    int part,
    bool section,
    const float normal[3],
    float offset,
    char *,
    size_t
);
bool workspace_runtime_model_parameter(
    workspace_runtime *,
    int session,
    int object,
    const workspace_model_parameter_edit *,
    arena *,
    workspace_model_parameter *,
    char *,
    size_t
);

/* Replay one recorded model action against the exact artifact and fixed step. */
bool workspace_runtime_model_replay(workspace_runtime *, int session, const char *json, char *, size_t);
#endif
