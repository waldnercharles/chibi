#ifndef CHIBI_GAME_QUERY_H
#define CHIBI_GAME_QUERY_H

#include "ecs/ecs.h"

#include <stddef.h>

/* Query documents use component names rather than runtime ids. The resolver is
 * supplied by the game registry so the query format stays stable across runs. */
typedef const ecs_component_desc *(*game_query_component_resolver)(const char *name, void *user);
typedef const char *(*game_query_component_name_resolver)(ecs_component_id component, void *user);

char *game_query_save(
    const ecs_query_expr *expression,
    game_query_component_resolver resolve,
    game_query_component_name_resolver name,
    void *user
);
ecs_query_expr *game_query_load(
    const char *json,
    game_query_component_resolver resolve,
    void *user,
    char *error,
    size_t error_size
);

#endif
