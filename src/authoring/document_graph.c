#include "authoring/document_graph.h"

#include "graph/authoring/graph_edit.h"
#include "graph/authoring/layout.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/modeling/model_compile.h"
#include "graph/nodes/field_nodes.h"
#include "graph/shader/shader_graph.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "types/array.h"
#include "types/value_document.h"

#include <math.h>
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
static bool member(const registry_pack *pack, const node_desc *desc)
{
    for (int i = 0; i < pack->node_count; i++) {
        if (pack->nodes[i] == desc) {
            return true;
        }
    }
    return false;
}
bool document_graph_node_allowed(behavior_domain domain, const node_desc *desc)
{
    if (!desc || desc->metadata_only) {
        return false;
    }
    switch (domain) {
        case BEHAVIOR_DOMAIN_MATERIAL:
            return member(&SHADER_PACK, desc) &&
                   strcmp(desc->name, "Shader Shape Output");
        case BEHAVIOR_DOMAIN_SHAPE:
            return member(&SHADER_PACK, desc) &&
                   strcmp(desc->name, "Shader Surface Output");
        case BEHAVIOR_DOMAIN_SHADER_LIBRARY:
            return behavior_library_node_allowed(domain, desc);
        case BEHAVIOR_DOMAIN_MODEL: return member(&MODEL_PACK, desc);
        case BEHAVIOR_DOMAIN_FIELD: return member(&FIELD_NODE_PACK, desc);
        default: return false;
    }
}
static int pin_index(const node *n, uint64_t id, bool output)
{
    if (!n) {
        return -1;
    }
    const pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    for (int i = 0; i < count; i++) {
        if (pins[i].id == id) {
            return i;
        }
    }
    return -1;
}
static bool disconnect(graph *g, node *n, uint64_t pin)
{
    int input = pin_index(n, pin, false), output = pin_index(n, pin, true);
    if (input < 0 && output < 0) {
        return false;
    }
    for (int i = acount(g->edges) - 1; i >= 0; i--) {
        edge e = g->edges[i];
        if ((input >= 0 && e.to_id == n->id && e.to_pin_id == pin) ||
            (output >= 0 && e.from_id == n->id && e.from_pin_id == pin)) {
            node *from = graph_node_by_id(g, e.from_id),
                 *to = graph_node_by_id(g, e.to_id);
            if (graph_remove_edge(
                    g,
                    from,
                    pin_index(from, e.from_pin_id, true),
                    to,
                    pin_index(to, e.to_pin_id, false)
                )) {
                return false;
            }
        }
    }
    return true;
}
static bool finite_value(const pin_value *value)
{
    if (value->container != PIN_CONTAINER_SINGLE) {
        const pin_array *array = value->ptr;
        for (int i = 0; array && i < array->count; i++) {
            if (!finite_value(&array->items[i])) {
                return false;
            }
        }
        return true;
    }
    if (value->type == PIN_F32) {
        return isfinite(value->f);
    }
    if (value->type == PIN_F64) {
        return isfinite(value->d);
    }
    if (value->type == PIN_STRUCT && value->ptr) {
        return type_value_is_finite(type_catalog_find(value->type_name), value->ptr);
    }
    return true;
}
static bool set_value(graph *g, node *n, uint64_t id, const char *literal, bool as_text, char *error, size_t size)
{
    int input = pin_index(n, id, false),
        output = input >= 0 ? -1 : pin_index(n, id, true);
    if (input < 0 && output < 0) {
        return fail(error, size, "Graph pin no longer exists");
    }
    bool side = output >= 0;
    if (side && !graph_output_default_editable(n, output)) {
        return fail(error, size, "Computed graph outputs have no editable default");
    }
    int index = side ? output : input;
    const pin_spec *pin = side ? &n->output_pins[index] : &n->input_pins[index];
    if (pin->kind != PIN_DATA) {
        return fail(error, size, "Execution pins have no default value");
    }
    av_doc *doc = as_text ? av_doc_new() : av_value_read(literal, error, size);
    if (as_text) {
        av_doc_set_root(doc, av_str(doc, literal));
    }
    if (!doc) {
        return false;
    }
    arena storage = { 0 };
    pin_value value = { 0 };
    pin_value declaration = { .type = pin->type,
                              .type_name = pin->type_name,
                              .container = pin->container };
    bool ok = !pin_value_read_document(&storage, declaration, av_doc_root(doc), &value, error, size);
    if (ok && !finite_value(&value)) {
        ok = fail(error, size, "Graph defaults must be finite");
    }
    if (ok && graph_set_pin_default(g, n, side, index, &value)) {
        ok = fail(error, size, "Graph pin default does not accept this value");
    }
    arena_free(&storage);
    av_doc_free(doc);
    return ok;
}
bool document_graph_prepare(
    workspace_documents *documents,
    int document,
    int revision,
    const document_graph_request *request,
    uint64_t *created,
    char **text,
    char *error,
    size_t size
)
{
    *created = 0;
    *text = NULL;
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
    if (!behavior_domain_pure(asset->domain)) {
        fail(error, size, "Executable source graphs use source authoring edits");
        goto done;
    }
    graph *g = behavior_asset_find_graph(asset, request->graph);
    if (!g) {
        fail(error, size, "Graph scope no longer exists");
        goto done;
    }
    node *n = graph_node_by_id(g, request->node);
    switch (request->operation) {
        case DOCUMENT_GRAPH_ADD: {
            const node_desc *desc = registry_node_by_name(request->type);
            if (!document_graph_node_allowed(asset->domain, desc)) {
                fail(error, size, "Node type is unavailable in this graph domain");
                break;
            }
            if (acount(g->nodes) >= 4096) {
                fail(error, size, "Graph authoring node limit exceeded");
                break;
            }
            if (!isfinite(request->x) || !isfinite(request->y)) {
                fail(error, size, "Graph coordinates must be finite");
                break;
            }
            n = graph_add_node(g, desc);
            if (!n) {
                fail(error, size, "Could not add graph node");
                break;
            }
            if ((asset->domain == BEHAVIOR_DOMAIN_MATERIAL ||
                 asset->domain == BEHAVIOR_DOMAIN_SHAPE ||
                 asset->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) &&
                !shader_graph_init_node(g, n)) {
                fail(error, size, "Shader node defaults could not be initialized");
                break;
            }
            n->x = request->x;
            n->y = request->y;
            n->placed = true;
            *created = n->id;
            ok = true;
            break;
        }
        case DOCUMENT_GRAPH_CONNECT: {
            node *from = graph_node_by_id(g, request->from_node);
            int output = pin_index(from, request->from_pin, true),
                input = pin_index(n, request->pin, false);
            ok = from && n && output >= 0 && input >= 0 &&
                 !graph_connect(g, from, output, n, input);
            if (!ok) {
                fail(error, size, "Pins are missing or incompatible");
            }
            break;
        }
        case DOCUMENT_GRAPH_DISCONNECT:
            ok = n && disconnect(g, n, request->pin);
            if (!ok) {
                fail(error, size, "Graph pin no longer exists");
            }
            break;
        case DOCUMENT_GRAPH_DEFAULT:
        case DOCUMENT_GRAPH_TEXT:
            ok = n &&
                 set_value(g, n, request->pin, request->value, request->operation == DOCUMENT_GRAPH_TEXT, error, size);
            if (!n) {
                fail(error, size, "Graph node no longer exists");
            }
            break;
        case DOCUMENT_GRAPH_REMOVE:
            ok = n && !graph_remove_node_tree(g, n);
            if (!ok) {
                fail(error, size, "Graph node cannot be removed");
            }
            break;
        case DOCUMENT_GRAPH_PLACE:
            if (!n || !isfinite(request->x) || !isfinite(request->y)) {
                fail(error, size, "Graph node or position is invalid");
                break;
            }
            n->x = request->x;
            n->y = request->y;
            n->placed = request->placed;
            ok = true;
            break;
        case DOCUMENT_GRAPH_ARRANGE:
            graph_layout(g);
            ok = true;
            break;
        default: fail(error, size, "Unknown graph edit"); break;
    }
    if (ok) {
        *text = behavior_asset_save(asset);
    }
done:
    if (!ok) {
        *created = 0;
    }
    if (ok && size) {
        *error = 0;
    }
    behavior_asset_free(asset);
    return ok;
}

bool document_graph_edit(
    workspace_documents *documents,
    int document,
    int revision,
    const document_graph_request *request,
    uint64_t *created,
    char *error,
    size_t size
)
{
    char *text = NULL;
    if (!document_graph_prepare(documents, document, revision, request, created, &text, error, size)) {
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
        *created = 0;
        return fail(error, size, workspace_document_status_text(status));
    }
    return true;
}
