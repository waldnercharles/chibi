#include "assets/system_document.h"

#include "../third_party/yyjson/yyjson.h"
#include "graph/document/serialize.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct game_system_document
{
    behavior_asset *behavior_asset;
    ecs_query_expr *query;
    unsigned revision;
};

static void set_error(char *error, size_t size, const char *message)
{
    if (error && size) {
        snprintf(error, size, "%s", message);
    }
}

game_system_document *game_system_document_new(const char *name)
{
    if (!name || !name[0]) {
        return NULL;
    }
    game_system_document *document = (game_system_document *)xcalloc(1, sizeof *document);
    document->behavior_asset = behavior_asset_new(name);
    if (!document->behavior_asset) {
        free(document);
        return NULL;
    }
    return document;
}

void game_system_document_free(game_system_document *document)
{
    if (!document) {
        return;
    }
    behavior_asset_free(document->behavior_asset);
    ecs_query_expr_free(document->query);
    free(document);
}

const behavior_asset *game_system_document_behavior_asset(const game_system_document *document)
{
    return document ? document->behavior_asset : NULL;
}

behavior_asset *game_system_document_behavior_asset_mutable(game_system_document *document)
{
    return document ? document->behavior_asset : NULL;
}

int game_system_document_replace_behavior_asset(game_system_document *document, behavior_asset *definition)
{
    if (!document || !definition) {
        return -1;
    }
    behavior_asset_free(document->behavior_asset);
    document->behavior_asset = definition;
    return 0;
}

const ecs_query_expr *game_system_document_query(const game_system_document *document)
{
    return document ? document->query : NULL;
}

int game_system_document_set_query(game_system_document *document, const ecs_query_expr *expression)
{
    if (!document) {
        return -1;
    }
    ecs_query_expr *copy = expression ? ecs_query_expr_clone(expression) : NULL;
    ecs_query_expr_free(document->query);
    document->query = copy;
    document->revision++;
    return 0;
}

unsigned game_system_document_revision(const game_system_document *document)
{
    return document ? document->revision : 0;
}

static ecs_query_expr *query_from_root(
    yyjson_val *root,
    game_query_component_resolver resolve,
    void *user,
    char *error,
    size_t error_size
)
{
    yyjson_val *query = yyjson_obj_get(root, "query");
    if (!query) {
        return NULL;
    }
    yyjson_mut_doc *copy = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *value = yyjson_val_mut_copy(copy, query);
    yyjson_mut_doc_set_root(copy, value);
    size_t size = 0;
    char *json = yyjson_mut_write(copy, 0, &size);
    yyjson_mut_doc_free(copy);
    if (!json) {
        set_error(error, error_size, "could not read system query");
        return NULL;
    }
    ecs_query_expr *expression = game_query_load(json, resolve, user, error, error_size);
    free(json);
    if (!expression) {
        return NULL;
    }
    return expression;
}

game_system_document *game_system_document_load(
    const char *json,
    game_query_component_resolver resolve,
    void *user,
    char *error,
    size_t error_size
)
{
    if (!json) {
        set_error(error, error_size, "system document is missing");
        return NULL;
    }
    behavior_asset *definition = behavior_asset_load_json(json);
    if (!definition) {
        set_error(error, error_size, "malformed system behavior_asset");
        return NULL;
    }
    yyjson_doc *parsed = asset_text_read(json, error, error_size);
    if (!parsed) {
        behavior_asset_free(definition);
        return NULL;
    }
    yyjson_val *root = yyjson_doc_get_root(parsed);
    if (!yyjson_is_obj(root)) {
        yyjson_doc_free(parsed);
        behavior_asset_free(definition);
        set_error(error, error_size, "system document root must be an object");
        return NULL;
    }
    game_system_document *document = (game_system_document *)xcalloc(1, sizeof *document);
    document->behavior_asset = definition;
    int has_query = yyjson_obj_get(root, "query") != NULL;
    if (has_query && !resolve) {
        yyjson_doc_free(parsed);
        game_system_document_free(document);
        set_error(error, error_size, "system query resolver is missing");
        return NULL;
    }
    document->query = query_from_root(root, resolve, user, error, error_size);
    yyjson_doc_free(parsed);
    if (has_query && !document->query) {
        game_system_document_free(document);
        return NULL;
    }
    return document;
}

char *game_system_document_save_behavior_asset(
    const behavior_asset *definition,
    const ecs_query_expr *query,
    game_query_component_resolver resolve,
    game_query_component_name_resolver name,
    void *user
)
{
    if (!definition) {
        return NULL;
    }
    char *behavior_asset_json = behavior_asset_save_json(definition);
    if (!behavior_asset_json || !query) {
        return behavior_asset_json;
    }
    char *query_json = game_query_save(query, resolve, name, user);
    if (!query_json) {
        free(behavior_asset_json);
        return NULL;
    }
    size_t behavior_asset_size = strlen(behavior_asset_json);
    while (behavior_asset_size &&
           (behavior_asset_json[behavior_asset_size - 1] == ' ' ||
            behavior_asset_json[behavior_asset_size - 1] == '\n')) {
        behavior_asset_size--;
    }
    if (!behavior_asset_size || behavior_asset_json[behavior_asset_size - 1] != '}') {
        free(behavior_asset_json);
        free(query_json);
        return NULL;
    }
    behavior_asset_json[behavior_asset_size - 1] = '\0';
    size_t size = behavior_asset_size + strlen(query_json) + 12;
    char *json = (char *)xmalloc(size);
    snprintf(json, size, "%s,\"query\":%s}", behavior_asset_json, query_json);
    free(behavior_asset_json);
    free(query_json);
    return json;
}

char *game_system_document_save(
    const game_system_document *document,
    game_query_component_resolver resolve,
    game_query_component_name_resolver name,
    void *user
)
{
    return document ? game_system_document_save_behavior_asset(
                          document->behavior_asset,
                          document->query,
                          resolve,
                          name,
                          user
                      )
                    : NULL;
}
