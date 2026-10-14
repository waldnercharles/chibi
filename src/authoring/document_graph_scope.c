#include "authoring/document_graph_scope.h"

#include "graph/authoring/asset_edit.h"
#include "graph/document/graph_signature.h"
#include "graph/document/serialize.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "types/catalog.h"
#include "types/value_document.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static bool identity(const char *text, uint64_t *value)
{
    if (!text || !*text) {
        return false;
    }
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    errno = 0;
    char *end;
    *value = strtoull(text, &end, 10);
    return !errno && !*end;
}
static bool declared_type(const document_scope_edit *r)
{
    if (r->container < PIN_CONTAINER_SINGLE || r->container > PIN_CONTAINER_SET ||
        r->storage < PIN_BOOL || r->storage > PIN_ENUM || r->storage == PIN_HANDLE) {
        return false;
    }
    if (r->storage == PIN_STRUCT) {
        return r->type_name && type_catalog_find(r->type_name);
    }
    if (r->storage == PIN_ENUM) {
        return r->type_name && type_catalog_enum_find(r->type_name);
    }
    return true;
}
bool document_graph_scope_prepare(
    workspace_documents *documents,
    int document,
    int revision,
    const document_scope_edit *r,
    uint64_t *created,
    char **text,
    char *error,
    size_t size
)
{
    if (size) {
        *error = 0;
    }
    *text = NULL;
    if (created) {
        *created = 0;
    }
    if (!r) {
        return fail(error, size, "Missing graph scope request");
    }
    workspace_document_view view;
    workspace_document_status status = workspace_document_read(documents, document, &view);
    if (status != WORKSPACE_DOCUMENT_OK) {
        return fail(error, size, workspace_document_status_text(status));
    }
    if (view.revision != revision) {
        return fail(error, size, workspace_document_status_text(WORKSPACE_DOCUMENT_STALE));
    }
    behavior_asset *asset = behavior_asset_load(view.text);
    if (!asset) {
        return fail(error, size, graph_last_error());
    }
    bool ok = false;
    uint64_t graph_id = 0, caller_id = 0;
    if (!behavior_domain_pure(asset->domain)) {
        fail(error, size, "Source functions use source graph edits");
        goto done;
    }
    graph *g = NULL;
    if (r->operation != DOCUMENT_SCOPE_ADD) {
        if (!identity(r->graph, &graph_id) ||
            (g = behavior_asset_find_graph(asset, graph_id)) == NULL ||
            g->kind != GRAPH_FUNCTION) {
            fail(error, size, "Function scope no longer exists");
            goto done;
        }
    }
    pin_spec type = { .kind = PIN_DATA,
                      .name = r->name,
                      .type = r->storage,
                      .type_name = r->type_name,
                      .container = r->container };
    switch (r->operation) {
        case DOCUMENT_SCOPE_ADD:
            if (!r->name || !*r->name || strlen(r->name) > 256 ||
                acount(asset->graphs) >= 256) {
                break;
            }
            g = behavior_asset_add_function(asset, r->name, true);
            ok = g != NULL;
            if (ok && created) {
                *created = g->graph_id;
            }
            break;
        case DOCUMENT_SCOPE_REMOVE:
            ok = !behavior_asset_delete_graph(asset, graph_id);
            break;
        case DOCUMENT_SCOPE_RENAME:
            ok = r->name && *r->name &&
                 !behavior_asset_rename_graph(asset, graph_id, r->name);
            break;
        case DOCUMENT_SCOPE_ADD_PIN:
            ok = declared_type(r) && !graph_signature_add(asset, g, r->result, &type);
            break;
        case DOCUMENT_SCOPE_REMOVE_PIN:
            ok = !graph_signature_remove(asset, g, r->result, r->index);
            break;
        case DOCUMENT_SCOPE_RENAME_PIN:
            ok = r->name &&
                 !graph_signature_rename(asset, g, r->result, r->index, r->name);
            break;
        case DOCUMENT_SCOPE_MOVE_PIN:
            ok = !graph_signature_move(asset, g, r->result, r->index, r->target);
            break;
        case DOCUMENT_SCOPE_RETYPE_PIN:
            ok = declared_type(r) &&
                 !graph_signature_retype(asset, g, r->result, r->index, &type);
            break;
        case DOCUMENT_SCOPE_DEFAULT: {
            node *entry = graph_node_by_id(g, g->entry_id);
            if (!entry || r->index < 0 || r->index >= entry->output_count || !r->value) {
                break;
            }
            const pin_spec *p = &entry->output_pins[r->index];
            arena storage = { 0 };
            pin_value value = { 0 }, declaration = { .type = p->type,
                                                     .type_name = p->type_name,
                                                     .container = p->container };
            av_doc *doc = av_value_read(r->value, error, size);
            ok = doc &&
                 !pin_value_read_document(&storage, declaration, av_doc_root(doc), &value, error, size) &&
                 !graph_signature_set_default(asset, g, r->index, &value);
            av_doc_free(doc);
            arena_free(&storage);
            break;
        }
        case DOCUMENT_SCOPE_CALL: {
            graph *caller = identity(r->caller, &caller_id)
                              ? behavior_asset_find_graph(asset, caller_id)
                              : NULL;
            node *call = caller
                           ? behavior_asset_add_function_call_node_to_graph(asset, caller, g)
                           : NULL;
            ok = call != NULL;
            if (ok && created) {
                *created = call->id;
            }
            break;
        }
        default: break;
    }
    if (!ok) {
        if (!size || !*error) {
            fail(error, size, "Graph scope operation was refused");
        }
        goto done;
    }
    *text = behavior_asset_save(asset);
    if (size) {
        *error = 0;
    }
done:
    if (!ok && created) {
        *created = 0;
    }
    behavior_asset_free(asset);
    return ok;
}

bool document_graph_scope_edit(
    workspace_documents *documents,
    int document,
    int revision,
    const document_scope_edit *request,
    uint64_t *created,
    char *error,
    size_t size
)
{
    char *text = NULL;
    if (!document_graph_scope_prepare(documents, document, revision, request, created, &text, error, size)) {
        return false;
    }
    workspace_document_view view;
    workspace_document_status status = workspace_document_read(documents, document, &view);
    if (!status) {
        status = workspace_document_edit(
            documents,
            document,
            revision,
            0,
            (int)view.bytes,
            text
        );
    }
    free(text);
    if (status) {
        if (created) {
            *created = 0;
        }
        return fail(error, size, workspace_document_status_text(status));
    }
    return true;
}
