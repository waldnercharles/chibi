#include "assets/system_document.h"

#include "assets/code_print.h"
#include "assets/code_source.h"
#include "graph/document/serialize.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
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
    av_value *root,
    game_query_component_resolver resolve,
    void *user,
    char *error,
    size_t error_size
)
{
    av_value *query = av_obj_get(root, "query");
    if (!query) {
        return NULL;
    }
    return game_query_load_value(query, resolve, user, error, error_size);
}

game_system_document *game_system_document_from_value(
    av_value *root,
    game_query_component_resolver resolve,
    void *user,
    char *error,
    size_t error_size
)
{
    behavior_asset *definition = behavior_asset_from_value(root);
    if (!definition) {
        set_error(error, error_size, "malformed system behavior asset");
        return NULL;
    }
    game_system_document *document = (game_system_document *)xcalloc(1, sizeof *document);
    document->behavior_asset = definition;
    int has_query = av_obj_get(root, "query") != NULL;
    if (has_query && !resolve) {
        game_system_document_free(document);
        set_error(error, error_size, "system query resolver is missing");
        return NULL;
    }
    document->query = query_from_root(root, resolve, user, error, error_size);
    if (has_query && !document->query) {
        game_system_document_free(document);
        return NULL;
    }
    return document;
}

game_system_document *game_system_document_load(
    const char *text,
    game_query_component_resolver resolve,
    void *user,
    char *error,
    size_t error_size
)
{
    return game_system_document_load_context(text, NULL, resolve, user, error, error_size);
}

game_system_document *game_system_document_load_context(
    const char *text,
    const code_source_context *context,
    game_query_component_resolver resolve,
    void *user,
    char *error,
    size_t error_size
)
{
    code_source *source = code_source_parse_context(text, NULL, context);
    if (!source->asset) {
        set_error(
            error,
            error_size,
            acount(source->diagnostics) ? source->diagnostics[0].message
                                        : "Expected executable system source"
        );
        code_source_free(source);
        return NULL;
    }
    game_system_document *document = game_system_document_from_value(
        av_doc_root(source->document),
        resolve,
        user,
        error,
        error_size
    );
    code_source_free(source);
    return document;
}

av_value *game_system_document_to_value(
    av_doc *doc,
    const game_system_document *document,
    game_query_component_resolver resolve,
    game_query_component_name_resolver name,
    void *user
)
{
    av_value *root = behavior_asset_to_value(doc, document->behavior_asset);
    if (document->query) {
        av_value *query = game_query_save_value(doc, document->query, resolve, name, user);
        if (!query) {
            return NULL;
        }
        av_obj_set(doc, root, "query", query);
    }
    return root;
}

char *game_system_document_save_with_metadata(
    const game_system_document *document,
    char **metadata,
    game_query_component_resolver resolve,
    game_query_component_name_resolver name,
    void *user
)
{
    if (metadata) {
        *metadata = NULL;
    }
    if (!document) {
        return NULL;
    }
    av_doc *doc = av_doc_new();
    av_value *query = document->query
                        ? game_query_save_value(doc, document->query, resolve, name, user)
                        : NULL;
    if (document->query && !query) {
        av_doc_free(doc);
        return NULL;
    }
    char error[256];
    char *text = code_print_system_with_metadata(document->behavior_asset, query, metadata, error, sizeof error);
    av_doc_free(doc);
    return text;
}

char *game_system_document_save(
    const game_system_document *document,
    game_query_component_resolver resolve,
    game_query_component_name_resolver name,
    void *user
)
{
    return game_system_document_save_with_metadata(document, NULL, resolve, name, user);
}
