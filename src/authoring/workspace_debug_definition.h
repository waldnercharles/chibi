#ifndef CHIBI_WORKSPACE_DEBUG_DEFINITION_H
#define CHIBI_WORKSPACE_DEBUG_DEFINITION_H
#include "authoring/workspace_debug.h"
#include "runtime/native_definitions.h"
#include "runtime/type_definitions.h"
typedef struct
{
    const char *kind, *name, *text, *type, *source;
    uint64_t query_variables[ECS_QUERY_VARS];
    uint64_t owner, module, function, occurrence, run, step, generation;
    int index, depth, start, end, line, column, status;
    bool enabled, available, retired, output;
    const char *prototype;
    int64_t creation; /* Proven per-world chronology in [1,INT64_MAX];0 unavailable. */
    int object;
} workspace_debug_row;
typedef struct
{
    const char *source;
    uint64_t generation, owner, module, function, occurrence;
    uint64_t from_module, from_function, from_occurrence, count;
    uint64_t query_variables[ECS_QUERY_VARS];
} workspace_debug_activity;
extern const type_desc WORKSPACE_DEBUG_ACTIVITY_LAYOUT;
extern const type_desc WORKSPACE_DEBUG_ROW_LAYOUT;
extern const native_type_definition WORKSPACE_DEBUG_TYPES[2];
extern const native_capability_definition WORKSPACE_DEBUG_DEFINITIONS[9];
extern const native_definition_pack WORKSPACE_DEBUG_DEFINITION_PACK;
#endif
