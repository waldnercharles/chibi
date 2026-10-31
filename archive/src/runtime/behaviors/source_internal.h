#ifndef CHIBI_SOURCE_INTERNAL_H
#define CHIBI_SOURCE_INTERNAL_H

#include "runtime/behaviors/source.h"
#include "support/array.h"

#include <string.h>

typedef struct
{
    char *name;
    execution_module *module; /* retained by program */
    execution_program *program;
    bool system, selected;
} source_record;
typedef struct
{
    ecs_entity entity;
    execution_program *program;
    uint64_t sequence;
    int begin; /* 0 pending, 1 started, -1 failed */
} source_attachment;
typedef struct
{
    source_profile_row row;
    execution_module *module;
    const behavior_function *function;
    char *name;
} source_profile;
struct source_registry
{
    ecs_world *world;
    event_catalog *events;
    source_record *records;
    source_attachment *attachments;
    service_binding *services; /* Owned names, borrowed host values. */
    char error[192];
    uint64_t attachment_sequence;
    int executing;
    bool explicit_systems, profiling;
    source_profile *profiles;
    uint64_t profile_dropped;
    source_staging *staging;
    execution_natives *natives; /* NULL until source_bind_natives */
};
void source_profile_bind(source_registry *);

static inline bool source_module_system(const execution_module *module)
{
    for (int i = 0; i < execution_module_function_count(module); i++) {
        if (execution_module_function_at(module, i).kind == EXECUTION_FUNCTION_SYSTEM) {
            return true;
        }
    }
    return false;
}
static inline bool source_record_active(const source_registry *r, const source_record *record)
{
    return !r->explicit_systems || !record->system || record->selected;
}
static inline int record_index(const source_registry *r, const char *name)
{
    for (int i = 0; i < acount(r->records); i++) {
        if (!strcmp(r->records[i].name, name)) {
            return i;
        }
    }
    return -1;
}

#endif
