#include "authoring/document_graph_definition.h"
#include "execution/function.h"
#include "graph/model/registry.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "types/value_document.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *copy(arena *a, const char *value)
{
    if (!value) {
        value = "";
    }
    char *out = arena_alloc(a, strlen(value) + 1);
    strcpy(out, value);
    return out;
}
static char *identity(arena *a, uint64_t value)
{
    char text[32];
    snprintf(text, sizeof text, "%llu", (unsigned long long)value);
    return copy(a, text);
}
static bool parse(const char *text, uint64_t *value)
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
static bool request_from(int operation, const pin_value *in, document_graph_request *request)
{
    *request = (document_graph_request){ .operation = operation };
    if (!parse(in[2].str, &request->graph)) {
        return false;
    }
    if (operation == DOCUMENT_GRAPH_ADD) {
        request->type = in[3].str;
        request->x = in[4].f;
        request->y = in[5].f;
        return true;
    }
    if (operation == DOCUMENT_GRAPH_ARRANGE) {
        return true;
    }
    if (operation == DOCUMENT_GRAPH_CONNECT) {
        return parse(in[3].str, &request->from_node) &&
               parse(in[4].str, &request->from_pin) &&
               parse(in[5].str, &request->node) && parse(in[6].str, &request->pin);
    }
    if (!parse(in[3].str, &request->node)) {
        return false;
    }
    if (operation == DOCUMENT_GRAPH_REMOVE) {
        return true;
    }
    if (operation == DOCUMENT_GRAPH_PLACE) {
        request->x = in[4].f;
        request->y = in[5].f;
        request->placed = in[6].b;
        return true;
    }
    if (!parse(in[4].str, &request->pin)) {
        return false;
    }
    if (operation == DOCUMENT_GRAPH_DEFAULT || operation == DOCUMENT_GRAPH_TEXT) {
        request->value = in[5].str;
    }
    return true;
}
static pin_array *array(arena *storage, pin_value *out, const char *name, int count, size_t size)
{
    pin_array *items = pin_array_snapshot_allocate(storage, PIN_STRUCT, name, count);
    for (int i = 0; i < count; i++) {
        items->items[i] = (pin_value){ .type = PIN_STRUCT,
                                       .type_name = name,
                                       .ptr = arena_alloc(storage, size) };
    }
    out->ptr = items;
    return items;
}
static bool optional_identity(const char *text, uint64_t *out)
{
    if (!text || !*text) {
        *out = 0;
        return true;
    }
    return parse(text, out);
}
static const char *source_edit(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out,
    bool prepare
)
{
    document_semantics_service *service = host;
    arena *storage = context->value_storage;
    char error[256] = { 0 };
    char *metadata = NULL;
    authoring_edit_result candidate = { 0 };
    out[0].b = false;
    out[1].str = copy(storage, "");
    out[2].str = copy(storage, "");
    if (prepare) {
        out[3].str = copy(storage, "");
    }
    const document_source_edit *request = in[2].ptr;
    const pin_array *selection = in[3].ptr;
    uint64_t origins[4096];
    authoring_edit_request edit = { 0 };
    bool valid = request && request->kind >= AUTHORING_EDIT_EXPRESSION &&
                 request->kind <= AUTHORING_EDIT_RETYPE_VARIABLE &&
                 request->binding >= AUTHORING_BIND_SYMBOL &&
                 request->binding <= AUTHORING_BIND_GLOBAL &&
                 optional_identity(request->origin, &edit.origin) &&
                 optional_identity(request->producer, &edit.producer) &&
                 optional_identity(request->previous, &edit.previous) &&
                 optional_identity(request->capability, &edit.capability) &&
                 optional_identity(request->symbol, &edit.symbol) &&
                 optional_identity(request->component, &edit.component) &&
                 optional_identity(request->member, &edit.member) &&
                 (!selection || (selection->count >= 0 && selection->count <= 4096));
    if (valid && selection) {
        for (int i = 0; i < selection->count; i++) {
            if (selection->items[i].type != PIN_STR ||
                !parse(selection->items[i].str, &origins[i])) {
                valid = false;
                break;
            }
        }
    }
    if (!valid) {
        out[1].str = copy(storage, "Invalid typed source graph request or selection");
        return NULL;
    }
    edit.kind = request->kind;
    edit.producer_result = request->result;
    edit.binding = request->binding;
    edit.origins = origins;
    edit.origin_count = selection ? selection->count : 0;
    edit.expression = request->expression;
    edit.fragment = request->fragment;
    edit.capability_name = request->capability_name;
    edit.name = request->name;
    edit.reference = request->reference;
    edit.field = request->field;
    edit.signature = request->signature;
    edit.disabled = request->disabled;
    document_semantics *
        snapshot = document_semantics_service_snapshot(service, in[0].i, in[1].i, error, sizeof error);
    arena values = { 0 };
    av_doc *literal = NULL;
    pin_value value = { 0 };
    if (snapshot) {
        const authoring_source_presentation *presentation = document_semantics_presentation(
            snapshot
        );
        if (presentation) {
            edit.generation = authoring_source_edit_generation(presentation);
        }
        bool typed = true;
        if (edit.kind == AUTHORING_EDIT_VALUE) {
            const source_presentation_origin *target = NULL;
            for (int i = 0;
                 presentation &&
                 i < authoring_source_presentation_origin_count(presentation);
                 i++) {
                const source_presentation_origin
                    *candidate = authoring_source_presentation_origin_at(presentation, i);
                if (candidate->identity == edit.origin) {
                    target = candidate;
                    break;
                }
            }
            const char *name = NULL;
            if (target) {
                for (int i = 0; document_semantics_catalog(snapshot) &&
                                i < document_semantics_catalog(snapshot)->type_count;
                     i++) {
                    if (document_semantics_catalog(snapshot)->types[i].id ==
                        target->type.nominal) {
                        name = document_semantics_catalog(snapshot)->types[i].name;
                    }
                }
            }
            literal = request->value
                        ? av_value_read(request->value, error, sizeof error)
                        : NULL;
            pin_value declaration = target
                                      ? (pin_value){ .type = target->type.storage,
                                                     .type_name = name,
                                                     .container = target->type.container }
                                      : (pin_value){ 0 };
            typed = target && literal &&
                    !pin_value_read_document(
                        &values,
                        declaration,
                        av_doc_root(literal),
                        &value,
                        error,
                        sizeof error
                    );
            if (typed) {
                edit.value = &value;
            } else if (!*error) {
                snprintf(error, sizeof error, "Select a typed value expression and provide its KDL literal");
            }
        }
        if (typed && prepare) {
            out[0].b = document_semantics_prepare_edit(snapshot, &edit, &candidate, error, sizeof error);
            if (out[0].b) {
                metadata = candidate.metadata;
                candidate.metadata = NULL;
                out[3].str = copy(storage, candidate.text);
            }
        } else if (typed) {
            out[0].b = document_semantics_edit_metadata(
                service->documents,
                snapshot,
                &edit,
                &metadata,
                error,
                sizeof error
            );
        }
    }
    out[1].str = copy(storage, error);
    out[2].str = copy(storage, metadata);
    free(metadata);
    authoring_edit_result_free(&candidate);
    av_doc_free(literal);
    arena_free(&values);
    document_semantics_free(snapshot);
    return NULL;
}
static const char *run(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out,
    int operation
)
{
    document_semantics_service *available = host;
    if (!available || !available->documents) {
        arena *storage = context->value_storage;
        if (operation == 8 || operation == 9) {
            array(
                storage,
                &out[0],
                operation == 8 ? "DocumentGraphChoice" : "DocumentGraphScope",
                0,
                operation == 8 ? sizeof(document_graph_choice) : sizeof(document_graph_scope)
            );
        } else {
            out[0].b = false;
            if (operation == 0 || operation >= 10) {
                out[2].str = copy(storage, "");
            }
            if (operation == 12 || operation == 13 || operation == 14) {
                out[3].str = copy(storage, "");
            }
        }
        out[1].str = copy(storage, "Semantic document service is unavailable");
        return NULL;
    }
    if (operation == 13) {
        return source_edit(context, host, in, out, true);
    }
    if (operation == 12 || operation == 14) {
        document_semantics_service *service = host;
        char error[256] = { 0 }, *text = NULL;
        uint64_t id = 0;
        out[0].b = false;
        if (operation == 14) {
            out[0].b = document_graph_scope_prepare(
                service->documents,
                in[0].i,
                in[1].i,
                in[2].ptr,
                &id,
                &text,
                error,
                sizeof error
            );
        } else {
            const document_graph_edit_value *value = in[2].ptr;
            document_graph_request request = { 0 };
            bool valid = value && value->operation >= DOCUMENT_GRAPH_ADD &&
                         value->operation <= DOCUMENT_GRAPH_TEXT &&
                         optional_identity(value->graph, &request.graph) &&
                         optional_identity(value->node, &request.node) &&
                         optional_identity(value->pin, &request.pin) &&
                         optional_identity(value->from_node, &request.from_node) &&
                         optional_identity(value->from_pin, &request.from_pin);
            if (valid) {
                request.operation = value->operation;
                request.type = value->type;
                request.value = value->value;
                request.x = value->x;
                request.y = value->y;
                request.placed = value->placed;
                out[0].b = document_graph_prepare(
                    service->documents,
                    in[0].i,
                    in[1].i,
                    &request,
                    &id,
                    &text,
                    error,
                    sizeof error
                );
            } else {
                snprintf(error, sizeof error, "Invalid typed graph edit request");
            }
        }
        out[1].str = copy(context->value_storage, error);
        out[2].str = identity(context->value_storage, id);
        out[3].str = copy(context->value_storage, text);
        free(text);
        return NULL;
    }
    if (operation == 11) {
        char error[256] = { 0 };
        uint64_t id = 0;
        document_semantics_service *service = host;
        out[0].b = document_graph_scope_edit(
            service->documents,
            in[0].i,
            in[1].i,
            in[2].ptr,
            &id,
            error,
            sizeof error
        );
        out[1].str = copy(context->value_storage, error);
        out[2].str = identity(context->value_storage, id);
        return NULL;
    }
    if (operation == 10) {
        return source_edit(context, host, in, out, false);
    }
    document_semantics_service *service = host;
    arena *storage = context->value_storage;
    char error[256] = { 0 };
    if (operation < 8) {
        document_graph_request request;
        uint64_t created = 0;
        if (!request_from(operation, in, &request)) {
            out[0].b = false;
            snprintf(error, sizeof error, "Graph identities must be unsigned decimal text");
        } else {
            out[0].b = document_graph_edit(
                service->documents,
                in[0].i,
                in[1].i,
                &request,
                &created,
                error,
                sizeof error
            );
        }
        out[1].str = copy(storage, error);
        if (operation == DOCUMENT_GRAPH_ADD) {
            out[2].str = identity(storage, created);
        }
        return NULL;
    }
    array(
        storage,
        &out[0],
        operation == 8 ? "DocumentGraphChoice" : "DocumentGraphScope",
        0,
        operation == 8 ? sizeof(document_graph_choice) : sizeof(document_graph_scope)
    );
    out[1].str = copy(storage, "");
    document_semantics *
        snapshot = document_semantics_service_snapshot(service, in[0].i, in[1].i, error, sizeof error);
    if (!snapshot) {
        out[1].str = copy(storage, error);
        return NULL;
    }
    const behavior_asset *asset = document_semantics_graph(snapshot);
    const script_analysis *analysis = document_semantics_analysis(snapshot);
    if (operation == 8) {
        int count = 0;
        if (analysis) {
            count = document_semantics_catalog(snapshot)->capability_count;
        } else if (asset) {
            for (int i = 0; i < registry_node_count(); i++) {
                if (document_graph_node_allowed(asset->domain, registry_node(i))) {
                    count++;
                }
            }
        }
        pin_array *choices = array(storage, &out[0], "DocumentGraphChoice", count, sizeof(document_graph_choice));
        int at = 0;
        if (analysis) {
            for (int i = 0; i < document_semantics_catalog(snapshot)->capability_count;
                 i++) {
                const script_catalog_capability
                    *cap = &document_semantics_catalog(snapshot)->capabilities[i];
                *(document_graph_choice *)choices->items[at++].ptr = (document_graph_choice){
                    copy(storage, cap->qualified_name ? cap->qualified_name : cap->name),
                    copy(storage, cap->name),
                    copy(storage, cap->category),
                    copy(storage, cap->keywords),
                    copy(storage, cap->description)
                };
            }
        }
        if (!analysis && asset) {
            for (int i = 0; i < registry_node_count(); i++) {
                const node_desc *desc = registry_node(i);
                if (!document_graph_node_allowed(asset->domain, desc)) {
                    continue;
                }
                *(document_graph_choice *)choices->items[at++].ptr = (document_graph_choice){
                    copy(storage, desc->name),
                    copy(storage, desc->name),
                    copy(storage, desc->category),
                    copy(storage, desc->keywords),
                    copy(storage, desc->description)
                };
            }
        }
    }
    if (operation == 9 && asset) {
        pin_array *scopes = array(
            storage,
            &out[0],
            "DocumentGraphScope",
            acount(asset->graphs),
            sizeof(document_graph_scope)
        );
        const authoring_source_presentation *presentation = document_semantics_presentation(
            snapshot
        );
        for (int i = 0; i < acount(asset->graphs); i++) {
            const graph *g = asset->graphs[i];
            document_graph_scope *row = scopes->items[i].ptr;
            *row = (document_graph_scope){
                .graph = identity(storage, g->graph_id),
                .name = copy(storage, g->name),
                .kind = copy(storage, graph_kind_name(g->kind)),
                .origin = copy(storage, "0"),
                .body = copy(storage, "0"),
                .signature = copy(storage, ""),
                .pure = behavior_domain_pure(asset->domain)
            };
            for (int j = 0;
                 presentation &&
                 j < authoring_source_presentation_origin_count(presentation);
                 j++) {
                const source_presentation_origin
                    *o = authoring_source_presentation_origin_at(presentation, j);
                if (o->graph != g->graph_id || o->function < 0 ||
                    o->function >= analysis->function_count) {
                    continue;
                }
                const script_function *function = &analysis->functions[o->function];
                if (o->kind == SOURCE_PRESENTATION_FUNCTION) {
                    row->origin = identity(storage, o->identity);
                    row->start = o->range.start;
                    row->end = o->range.end;
                    row->disabled = function->disabled;
                    row->pure = function->pure;
                    char *signature = document_semantics_hover(
                        snapshot,
                        o->range.start,
                        service->ui_project
                    );
                    row->signature = copy(storage, signature);
                    free(signature);
                }
                if (o->kind == SOURCE_PRESENTATION_STATEMENT &&
                    o->record == function->body) {
                    row->body = identity(storage, o->identity);
                }
            }
        }
    }
    document_semantics_free(snapshot);
    return NULL;
}
#define CALLBACK(N)                                                                           \
    static const char *                                                                       \
    call##N(const behavior_function_context *c, void *s, const pin_value *in, pin_value *out) \
    {                                                                                         \
        return run(c, s, in, out, N);                                                         \
    }
CALLBACK(0) CALLBACK(1) CALLBACK(2) CALLBACK(3) CALLBACK(4) CALLBACK(5) CALLBACK(6) CALLBACK(7) CALLBACK(8) CALLBACK(9) CALLBACK(10) CALLBACK(11) CALLBACK(12) CALLBACK(13) CALLBACK(14)
#define CALL(N)                                                                \
    { .service = "document_semantics",                                         \
      .optional_service = true,                                                \
      .missing_service = "Semantic document service is unavailable",           \
      .context = call##N },
const node_host_call DOCUMENT_GRAPH_CALLS[15]={CALL(0) CALL(1) CALL(2) CALL(3) CALL(4) CALL(5) CALL(6) CALL(7) CALL(8) CALL(9) CALL(10) CALL(11) CALL(12) CALL(13) CALL(14)};
const node_access DOCUMENT_GRAPH_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
const node_access DOCUMENT_GRAPH_WRITE_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
