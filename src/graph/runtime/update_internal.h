#ifndef CHIBI_BEHAVIOR_UPDATE_INTERNAL_H
#define CHIBI_BEHAVIOR_UPDATE_INTERNAL_H
#include "graph/runtime/update.h"
#include "support/arena.h"

struct behavior_update_binding
{
    behavior_function_context context;
    const behavior_function *function;
    behavior_asset *asset;
    node *entry;
    void *code, *system_code;
    service_binding *services;
    behavior_function_activation scratch;
    const ecs_execution_column **columns;
    arena storage;
    int index;
};

#endif
