#include "system_document.h"

#include "../core/serialize.h"
#include "../core/xalloc.h"
#include "../third_party/yyjson/yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct game_system_document
{
    blueprint *blueprint;
    ecs_query_expr *query;
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
    document->blueprint = blueprint_new(name);
    if (!document->blueprint) {
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
    blueprint_free(document->blueprint);
    ecs_query_expr_free(document->query);
    free(document);
}

const blueprint *game_system_document_blueprint(const game_system_document *document)
{
    return document ? document->blueprint : NULL;
}

blueprint *game_system_document_blueprint_mutable(game_system_document *document)
{
    return document ? document->blueprint : NULL;
}

int game_system_document_replace_blueprint(game_system_document *document, blueprint *definition)
{
    if (!document || !definition) {
        return -1;
    }
    blueprint_free(document->blueprint);
    document->blueprint = definition;
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
    return 0;
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
    blueprint *definition = blueprint_load_json(json);
    if (!definition) {
        set_error(error, error_size, "malformed system blueprint");
        return NULL;
    }
    yyjson_doc *parsed = yyjson_read(json, strlen(json), 0);
    if (!parsed) {
        blueprint_free(definition);
        set_error(error, error_size, "malformed system document");
        return NULL;
    }
    yyjson_val *root = yyjson_doc_get_root(parsed);
    if (!yyjson_is_obj(root)) {
        yyjson_doc_free(parsed);
        blueprint_free(definition);
        set_error(error, error_size, "system document root must be an object");
        return NULL;
    }
    game_system_document *document = (game_system_document *)xcalloc(1, sizeof *document);
    document->blueprint = definition;
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

char *game_system_document_save_blueprint(
    const blueprint *definition,
    const ecs_query_expr *query,
    game_query_component_resolver resolve,
    game_query_component_name_resolver name,
    void *user
)
{
    if (!definition) {
        return NULL;
    }
    char *blueprint_json = blueprint_save_json(definition);
    if (!blueprint_json || !query) {
        return blueprint_json;
    }
    char *query_json = game_query_save(query, resolve, name, user);
    if (!query_json) {
        free(blueprint_json);
        return NULL;
    }
    size_t blueprint_size = strlen(blueprint_json);
    while (blueprint_size && (blueprint_json[blueprint_size - 1] == ' ' ||
                              blueprint_json[blueprint_size - 1] == '\n')) {
        blueprint_size--;
    }
    if (!blueprint_size || blueprint_json[blueprint_size - 1] != '}') {
        free(blueprint_json);
        free(query_json);
        return NULL;
    }
    blueprint_json[blueprint_size - 1] = '\0';
    size_t size = blueprint_size + strlen(query_json) + 12;
    char *json = (char *)xmalloc(size);
    snprintf(json, size, "%s,\"query\":%s}", blueprint_json, query_json);
    free(blueprint_json);
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
    return document
             ? game_system_document_save_blueprint(document->blueprint, document->query, resolve, name, user)
             : NULL;
}
