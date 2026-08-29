#ifndef CBLUEPRINT_GAME_SYSTEM_DOCUMENT_H
#define CBLUEPRINT_GAME_SYSTEM_DOCUMENT_H

#include "query.h"
#include "system.h"

/* A System document keeps graph data and its authored selection filter
 * together. The filter is optional; absent means the graph's inferred access
 * terms are used. */
typedef struct game_system_document game_system_document;

game_system_document *game_system_document_load(
    const char *json,
    game_query_component_resolver resolve,
    void *user,
    char *error,
    size_t error_size
);
game_system_document *game_system_document_new(const char *name);
void game_system_document_free(game_system_document *document);
const blueprint *game_system_document_blueprint(const game_system_document *document);
blueprint *game_system_document_blueprint_mutable(game_system_document *document);
const ecs_query_expr *game_system_document_query(const game_system_document *document);
int game_system_document_set_query(game_system_document *document, const ecs_query_expr *expression);
char *game_system_document_save(
    const game_system_document *document,
    game_query_component_resolver resolve,
    game_query_component_name_resolver name,
    void *user
);
char *game_system_document_save_blueprint(
    const blueprint *definition,
    const ecs_query_expr *query,
    game_query_component_resolver resolve,
    game_query_component_name_resolver name,
    void *user
);

#endif
