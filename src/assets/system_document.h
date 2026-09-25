#ifndef CHIBI_GAME_SYSTEM_DOCUMENT_H
#define CHIBI_GAME_SYSTEM_DOCUMENT_H

#include "runtime/systems/query.h"
#include "runtime/systems/system.h"

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
const behavior_asset *game_system_document_behavior_asset(const game_system_document *document);
int game_system_document_replace_behavior_asset(game_system_document *document, behavior_asset *definition);
const ecs_query_expr *game_system_document_query(const game_system_document *document);
int game_system_document_set_query(game_system_document *document, const ecs_query_expr *expression);
unsigned game_system_document_revision(const game_system_document *document);
char *game_system_document_save(
    const game_system_document *document,
    game_query_component_resolver resolve,
    game_query_component_name_resolver name,
    void *user
);

#endif
