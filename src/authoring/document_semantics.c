#include "authoring/document_semantics.h"

#include "assets/asset_source.h"
#include "authoring/document_layout.h"
#include "authoring/source_rename.h"
#include "graph/authoring/layout.h"
#include "graph/compiler/validation.h"
#include "graph/document/serialize.h"
#include "language/component.h"
#include "runtime/text_search.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct document_semantics
{
    int document, revision;
    char *path, *text;
    asset_source *asset;
    ui_source *ui;
    bool valid, component;
    const char *dialect;
    behavior_asset *graph;
    compile_report compiled;
    script_syntax *syntax;
    script_analysis *analysis;
    const script_catalog *catalog;
    void *catalog_owner, *catalog_user;
    document_semantics_catalog_release_fn catalog_release;
    authoring_navigation *navigation;
    authoring_source_presentation *presentation;
    script_diagnostic *diagnostics;
};

static bool failure(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}

document_semantics *document_semantics_new_content(
    const workspace_documents *documents,
    int document,
    int revision,
    const script_catalog *catalog,
    const char *content,
    char *error,
    size_t size
)
{
    if (!content || !*content) {
        failure(error, size, "Semantic content directory is required");
        return NULL;
    }
    workspace_document_view view;
    workspace_document_status status = workspace_document_read(documents, document, &view);
    if (status != WORKSPACE_DOCUMENT_OK) {
        failure(error, size, workspace_document_status_text(status));
        return NULL;
    }
    if (view.revision != revision) {
        failure(error, size, workspace_document_status_text(WORKSPACE_DOCUMENT_STALE));
        return NULL;
    }
    document_semantics *s = xcalloc(1, sizeof *s);
    s->document = document;
    s->revision = revision;
    s->catalog = catalog;
    s->dialect = "chibi";
    s->text = xstrdup(view.text);
    s->path = xstrdup(view.path);
    const char *extension = strrchr(view.path, '.');
    if (extension && (!strcmp(extension, ".rml") || !strcmp(extension, ".rcss"))) {
        s->dialect = !strcmp(extension, ".rml") ? "rml" : "rcss";
        s->ui = ui_source_parse(view.text, !strcmp(extension, ".rml") ? UI_SOURCE_RML : UI_SOURCE_RCSS);
        for (int i = 0; i < acount(s->ui->diagnostics); i++) {
            const ui_source_diagnostic *d = &s->ui->diagnostics[i];
            script_diagnostic diagnostic = {
                .range = { d->start, d->end, d->line, d->column }
            };
            snprintf(diagnostic.message, sizeof diagnostic.message, "%s", d->message);
            apush(s->diagnostics, diagnostic);
        }
        s->valid = !acount(s->diagnostics);
        if (size) {
            *error = 0;
        }
        return s;
    }
    if (extension && !strcmp(extension, ".kdl")) {
        s->dialect = "kdl";
        s->asset = asset_source_parse_path(view.text, view.path);
        for (int i = 0; i < acount(s->asset->diagnostics); i++) {
            const asset_source_diagnostic *d = &s->asset->diagnostics[i];
            script_diagnostic diagnostic = {
                .range = { d->start, d->end, d->line, d->column }
            };
            snprintf(diagnostic.message, sizeof diagnostic.message, "%s", d->message);
            apush(s->diagnostics, diagnostic);
        }
        if (s->asset->document) {
            const av_value *root = av_doc_root(s->asset->document);
            if (av_obj_get(root, "graphs") || av_obj_get(root, "domain")) {
                s->graph = behavior_asset_from_value((av_value *)root);
                if (!s->graph) {
                    script_diagnostic diagnostic = { 0 };
                    snprintf(diagnostic.message, sizeof diagnostic.message, "%s", graph_last_error());
                    apush(s->diagnostics, diagnostic);
                } else {
                    for (int i = 0; i < acount(s->graph->graphs); i++) {
                        graph_place_unplaced(s->graph->graphs[i]);
                    }
                    if (s->graph->domain != BEHAVIOR_DOMAIN_EVENT_DEFINITION) {
                        graph_asset_compile(s->graph, &s->compiled);
                        for (int i = 0; i < acount(s->compiled.items); i++) {
                            const compile_diagnostic *d = &s->compiled.items[i];
                            if (d->severity != CMPL_ERROR) {
                                continue;
                            }
                            script_diagnostic diagnostic = { 0 };
                            snprintf(diagnostic.message, sizeof diagnostic.message, "%s", d->message);
                            apush(s->diagnostics, diagnostic);
                        }
                    }
                }
            }
        }
        s->valid = !acount(s->diagnostics);
        if (size) {
            *error = 0;
        }
        return s;
    }
    if (extension && strcmp(extension, ".chibi")) {
        s->dialect = "text";
        s->valid = true;
        if (size) {
            *error = 0;
        }
        return s;
    }
    s->syntax = script_parse(view.text);
    char identity[64];
    snprintf(identity, sizeof identity, "document/%d.chibi", document);
    const char *key = view.path && *view.path ? view.path : identity;
    size_t prefix = strlen(content);
    if (!strncmp(key, content, prefix) && key[prefix] == '/') {
        key += prefix + 1;
    }
    TSNode root = ts_tree_root_node(s->syntax->tree);
    s->component = strstr(key, "components/") == key;
    for (uint32_t i = 0; i < ts_node_named_child_count(root); i++) {
        TSNode declaration = ts_node_named_child(root, i);
        if (!ts_node_is_extra(declaration)) {
            s->component |= !strcmp(ts_node_type(declaration), "component");
            break;
        }
    }
    if (s->component) {
        script_component *component = script_component_read(s->syntax, catalog, &s->diagnostics);
        s->valid = component && !acount(s->diagnostics);
        script_component_free(component);
        if (size) {
            *error = 0;
        }
        return s;
    }
    s->analysis = script_analyze_document(s->syntax, catalog, key);
    for (int i = 0; i < s->syntax->diagnostic_count; i++) {
        apush(s->diagnostics, s->syntax->diagnostics[i]);
    }
    for (int i = 0; i < s->analysis->diagnostic_count; i++) {
        apush(s->diagnostics, s->analysis->diagnostics[i]);
    }
    free(s->path);
    s->path = xstrdup(view.path && *view.path ? view.path : identity);
    authoring_document input = { .path = s->path,
                                 .syntax = s->syntax,
                                 .analysis = s->analysis };
    s->navigation = authoring_navigation_new(&input, 1, catalog);
    s->presentation = authoring_source_presentation_new(s->analysis, catalog, NULL);
    s->valid = s->analysis->valid && !s->syntax->diagnostic_count;
    if (s->presentation) {
        char ignored[128], *metadata = NULL;
        char *sidecar = document_layout_path(documents, document, revision, ignored, sizeof ignored);
        if (sidecar && !workspace_document_read_path(documents, sidecar, &metadata)) {
            authoring_source_presentation_restore(s->presentation, metadata);
        }
        free(metadata);
        free(sidecar);
        behavior_asset *view = authoring_source_presentation_view(s->presentation);
        for (int i = 0; i < acount(view->graphs); i++) {
            graph_place_unplaced(view->graphs[i]);
        }
    }
    if (size) {
        *error = 0;
    }
    return s;
}

document_semantics *document_semantics_new(
    const workspace_documents *documents,
    int document,
    int revision,
    const script_catalog *catalog,
    char *error,
    size_t size
)
{
    return document_semantics_new_content(documents, document, revision, catalog, "content", error, size);
}

void document_semantics_free(document_semantics *s)
{
    if (!s) {
        return;
    }
    authoring_source_presentation_free(s->presentation);
    authoring_navigation_free(s->navigation);
    script_analysis_free(s->analysis);
    script_syntax_free(s->syntax);
    afree(s->diagnostics);
    free(s->path);
    free(s->text);
    ui_source_free(s->ui);
    asset_source_free(s->asset);
    behavior_asset_free(s->graph);
    compile_report_free(&s->compiled);
    if (s->catalog_release) {
        s->catalog_release(s->catalog_user, s->catalog_owner);
    }
    free(s);
}
const script_catalog *document_semantics_catalog(const document_semantics *s)
{
    return s->catalog;
}
int document_semantics_revision(const document_semantics *s)
{
    return s->revision;
}
bool document_semantics_valid(const document_semantics *s)
{
    return s->valid;
}
int document_semantics_diagnostic_count(const document_semantics *s)
{
    return acount(s->diagnostics);
}
const script_diagnostic *document_semantics_diagnostic(const document_semantics *s, int index)
{
    assert(index >= 0 && index < acount(s->diagnostics));
    return &s->diagnostics[index];
}
const authoring_source_presentation *document_semantics_presentation(const document_semantics *s)
{
    return s->presentation;
}
const authoring_navigation *document_semantics_navigation(const document_semantics *s)
{
    return s->navigation;
}

static bool current(workspace_documents *documents, const document_semantics *s, char *error, size_t size)
{
    workspace_document_view view;
    workspace_document_status status = workspace_document_read(documents, s->document, &view);
    if (status != WORKSPACE_DOCUMENT_OK) {
        return failure(error, size, workspace_document_status_text(status));
    }
    if (view.revision != s->revision) {
        return failure(error, size, workspace_document_status_text(WORKSPACE_DOCUMENT_STALE));
    }
    return true;
}

bool document_semantics_prepare_edit(
    const document_semantics *snapshot,
    const authoring_edit_request *request,
    authoring_edit_result *result,
    char *error,
    size_t size
)
{
    if (!snapshot->presentation) {
        return failure(error, size, "Document has no semantic source graph");
    }
    return authoring_source_edit(
        snapshot->analysis,
        snapshot->catalog,
        snapshot->presentation,
        request,
        result,
        error,
        size
    );
}
bool document_semantics_edit_metadata(
    workspace_documents *documents,
    const document_semantics *s,
    const authoring_edit_request *request,
    char **metadata,
    char *error,
    size_t size
)
{
    if (metadata) {
        *metadata = NULL;
    }
    if (!current(documents, s, error, size)) {
        return false;
    }
    if (!s->presentation) {
        return failure(error, size, "Document has no semantic graph presentation");
    }
    authoring_edit_result result = { 0 };
    if (!document_semantics_prepare_edit(s, request, &result, error, size)) {
        return false;
    }
    workspace_document_status status = workspace_document_edit(
        documents,
        s->document,
        s->revision,
        0,
        (int)strlen(s->text),
        result.text
    );
    if (status == WORKSPACE_DOCUMENT_OK && metadata) {
        *metadata = result.metadata;
        result.metadata = NULL;
    }
    authoring_edit_result_free(&result);
    if (status != WORKSPACE_DOCUMENT_OK) {
        return failure(error, size, workspace_document_status_text(status));
    }
    if (size) {
        *error = 0;
    }
    return true;
}

bool document_semantics_edit(
    workspace_documents *documents,
    const document_semantics *snapshot,
    const authoring_edit_request *request,
    char *error,
    size_t size
)
{
    return document_semantics_edit_metadata(documents, snapshot, request, NULL, error, size);
}

bool document_semantics_replace(
    workspace_documents *documents,
    const document_semantics *s,
    int start,
    int end,
    const char *replacement,
    bool validate,
    char *error,
    size_t size
)
{
    if (!current(documents, s, error, size)) {
        return false;
    }
    size_t bytes = strlen(s->text);
    if (start < 0 || end < start || (size_t)end > bytes) {
        return failure(error, size, "Invalid source edit range");
    }
    if (((unsigned char)s->text[start] & 0xc0) == 0x80 ||
        ((unsigned char)s->text[end] & 0xc0) == 0x80) {
        return failure(error, size, "Source edit splits a UTF-8 character");
    }
    if (validate && !s->syntax) {
        text_edit edit = { start, end, replacement };
        char *candidate = text_edits_apply(s->text, &edit, 1);
        bool valid = true;
        if (s->asset) {
            asset_source *parsed = asset_source_parse_path(candidate, s->path);
            valid = !acount(parsed->diagnostics);
            if (!valid) {
                failure(error, size, parsed->diagnostics[0].message);
            }
            asset_source_free(parsed);
        }
        if (s->ui) {
            ui_source *parsed = ui_source_parse(candidate, s->ui->language);
            valid = !acount(parsed->diagnostics);
            if (!valid) {
                failure(error, size, parsed->diagnostics[0].message);
            }
            ui_source_free(parsed);
        }
        free(candidate);
        if (!valid) {
            return false;
        }
    }
    if (validate && s->component) {
        script_syntax *syntax = script_edit(s->syntax, start, end, replacement);
        if (!syntax) {
            return failure(error, size, "Invalid component edit range or UTF-8");
        }
        script_diagnostic *diagnostics = NULL;
        script_component *component = script_component_read(syntax, s->catalog, &diagnostics);
        bool valid = component && !syntax->diagnostic_count && !acount(diagnostics);
        if (!valid) {
            failure(
                error,
                size,
                syntax->diagnostic_count ? syntax->diagnostics[0].message
                : acount(diagnostics)    ? diagnostics[0].message
                                         : "Component validation failed"
            );
        }
        script_component_free(component);
        afree(diagnostics);
        script_syntax_free(syntax);
        if (!valid) {
            return false;
        }
    }
    if (validate && s->syntax && !s->component) {
        script_syntax *syntax = script_edit(s->syntax, start, end, replacement);
        if (!syntax) {
            return failure(error, size, "Invalid source edit range or UTF-8");
        }
        script_analysis
            *analysis = script_analyze_document(syntax, s->catalog, s->analysis->identity);
        bool valid = analysis->valid && !syntax->diagnostic_count;
        if (!valid) {
            const char *message = syntax->diagnostic_count
                                    ? syntax->diagnostics[0].message
                                : analysis->diagnostic_count
                                    ? analysis->diagnostics[0].message
                                    : "Source validation failed";
            failure(error, size, message);
        }
        script_analysis_free(analysis);
        script_syntax_free(syntax);
        if (!valid) {
            return false;
        }
    }
    workspace_document_status
        status = workspace_document_edit(documents, s->document, s->revision, start, end, replacement);
    if (status != WORKSPACE_DOCUMENT_OK) {
        return failure(error, size, workspace_document_status_text(status));
    }
    if (size) {
        *error = 0;
    }
    return true;
}

static unsigned char folded(unsigned char c, bool ignorecase)
{
    return ignorecase && c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c;
}
bool document_semantics_find(
    const workspace_documents *documents,
    int document,
    int revision,
    const char *query,
    int from,
    bool ignorecase,
    int *start,
    int *end,
    char *error,
    size_t size
)
{
    *start = -1;
    *end = -1;
    workspace_document_view view;
    workspace_document_status status = workspace_document_read(documents, document, &view);
    if (status != WORKSPACE_DOCUMENT_OK) {
        return failure(error, size, workspace_document_status_text(status));
    }
    if (view.revision != revision) {
        return failure(error, size, workspace_document_status_text(WORKSPACE_DOCUMENT_STALE));
    }
    if (from < 0 || (size_t)from > view.bytes ||
        ((unsigned char)view.text[from] & 0xc0) == 0x80) {
        return failure(error, size, "Find offset must be a UTF-8 byte boundary");
    }
    if (!query || !text_search_contains(query, query, false)) {
        return failure(error, size, "Find query must be valid UTF-8 and at most 256 bytes");
    }
    size_t length = strlen(query);
    if (!length) {
        if (size) {
            *error = 0;
        }
        return true;
    }
    for (size_t at = (size_t)from; at + length <= view.bytes; at++) {
        if (((unsigned char)view.text[at] & 0xc0) == 0x80) {
            continue;
        }
        size_t matched = 0;
        while (matched < length &&
               folded((unsigned char)query[matched], ignorecase) ==
                   folded((unsigned char)view.text[at + matched], ignorecase)) {
            matched++;
        }
        if (matched == length) {
            *start = (int)at;
            *end = (int)(at + length);
            break;
        }
    }
    if (size) {
        *error = 0;
    }
    return true;
}
bool document_semantics_rename(
    workspace_documents *documents,
    const document_semantics *s,
    int offset,
    const char *name,
    char *error,
    size_t size
)
{
    if (!current(documents, s, error, size)) {
        return false;
    }
    if (!s->navigation) {
        return failure(error, size, "This language requires its own project-wide rename service");
    }
    const authoring_reference *reference = authoring_navigation_at(s->navigation, 0, offset);
    if (!reference) {
        return failure(error, size, "No resolved symbol at this offset");
    }
    /* A single-source view cannot prove callers outside it remain valid. */
    if (reference->target.kind != AUTHORING_SYMBOL) {
        return failure(error, size, "This symbol requires project-wide rename validation");
    }
    const script_symbol *symbol = NULL;
    for (int i = 0; i < s->analysis->symbol_count; i++) {
        if (s->analysis->symbols[i].id == reference->target.id) {
            symbol = &s->analysis->symbols[i];
            break;
        }
    }
    if (!symbol ||
        (symbol->kind != SCRIPT_LOCAL && symbol->kind != SCRIPT_QUERY_VARIABLE)) {
        return failure(error, size, "This declaration requires project-wide rename validation");
    }
    authoring_rename_plan
        *plan = authoring_source_rename(s->navigation, reference->target, name, error, size);
    if (!plan) {
        return false;
    }
    char *candidate = authoring_rename_document(plan, 0, s->syntax->text, error, size);
    authoring_rename_plan_free(plan);
    if (!candidate) {
        return false;
    }
    bool result = document_semantics_replace(documents, s, 0, (int)strlen(s->text), candidate, true, error, size);
    free(candidate);
    return result;
}

const ui_source *document_semantics_ui(const document_semantics *s)
{
    return s->ui;
}
authoring_highlight *document_semantics_highlights(const document_semantics *s, int *count)
{
    if (s->navigation) {
        return authoring_source_highlights(s->navigation, 0, count);
    }
    if (s->syntax) {
        return authoring_syntax_highlights(s->syntax, count);
    }
    *count = s->ui    ? acount(s->ui->spans)
           : s->asset ? acount(s->asset->spans)
                      : 0;
    authoring_highlight *out = xcalloc((size_t)*count, sizeof *out);
    for (int i = 0; i < *count; i++) {
        out[i].range.start = s->ui ? s->ui->spans[i].start : s->asset->spans[i].start;
        out[i].range.end = s->ui ? s->ui->spans[i].end : s->asset->spans[i].end;
        out[i].cls = s->ui ? ui_source_kind_class(s->ui->spans[i].kind)
                           : s->asset->spans[i].cls;
    }
    return out;
}
void document_semantics_complete(
    const document_semantics *s,
    int offset,
    const ui_source_project *project,
    authoring_completions *out
)
{
    *out = (authoring_completions){ .range = { .start = offset, .end = offset } };
    if (offset < 0 || (size_t)offset > strlen(s->text)) {
        return;
    }
    if (s->navigation) {
        authoring_source_complete(s->navigation, 0, offset, out);
        return;
    }
    if (s->asset) {
        asset_source_completions result = { 0 };
        asset_source_complete(s->asset, offset, s->path, NULL, 0, &result);
        out->range.start = result.start;
        out->range.end = result.end;
        out->count = acount(result.items);
        asetlen(out->items, out->count);
        for (int i = 0; i < out->count; i++) {
            out->items[i] = (authoring_completion){
                .label = xstrdup(result.items[i].label),
                .detail = xstrdup(result.items[i].detail ? result.items[i].detail : ""),
                .insert = xstrdup(result.items[i].insert),
                .kind = AUTHORING_COMPLETE_FIELD
            };
        }
        asset_source_completions_free(&result);
        return;
    }
    if (s->ui) {
        ui_source_project empty = { 0 };
        ui_source_completions result = { 0 };
        ui_source_complete(s->ui, offset, project ? project : &empty, &result);
        out->range.start = result.start;
        out->range.end = result.end;
        out->count = acount(result.items);
        asetlen(out->items, out->count);
        for (int i = 0; i < out->count; i++) {
            out->items[i] = (authoring_completion){
                .label = xstrdup(result.items[i].label),
                .detail = xstrdup(""),
                .insert = xstrdup(result.items[i].label),
                .kind = AUTHORING_COMPLETE_FIELD
            };
        }
        ui_source_completions_free(&result);
    }
}
char *document_semantics_hover(const document_semantics *s, int offset, const ui_source_project *project)
{
    if (offset < 0 || (size_t)offset > strlen(s->text)) {
        return NULL;
    }
    if (s->navigation) {
        return authoring_source_hover(s->navigation, 0, offset);
    }
    if (s->asset) {
        return asset_source_hover(s->asset, offset);
    }
    ui_source_project empty = { 0 };
    return s->ui ? ui_source_hover(s->ui, offset, project ? project : &empty) : NULL;
}

const behavior_asset *document_semantics_graph(const document_semantics *s)
{
    return s->graph        ? s->graph
         : s->presentation ? authoring_source_presentation_view(s->presentation)
                           : NULL;
}
const compile_report *document_semantics_compile(const document_semantics *s)
{
    return &s->compiled;
}
const char *document_semantics_dialect(const document_semantics *s)
{
    return s->dialect;
}

const script_analysis *document_semantics_analysis(const document_semantics *s)
{
    return s->analysis;
}

document_semantics *document_semantics_service_snapshot(
    document_semantics_service *service,
    int document,
    int revision,
    char *error,
    size_t size
)
{
    if (!service) {
        failure(error, size, "Semantic document service is unavailable");
        return NULL;
    }
    const script_catalog *catalog = service->catalog;
    void *owner = NULL;
    if (service->catalog_acquire && !service->catalog_release) {
        failure(error, size, "Semantic catalog acquisition requires an owning release callback");
        return NULL;
    }
    if (service->catalog_acquire &&
        !service->catalog_acquire(service->catalog_user, &catalog, &owner, error, size)) {
        return NULL;
    }
    if (!catalog) {
        if (service->catalog_release) {
            service->catalog_release(service->catalog_user, owner);
        }
        failure(error, size, "Semantic catalog provider returned no declarations");
        return NULL;
    }
    document_semantics *snapshot = document_semantics_new_content(
        service->documents,
        document,
        revision,
        catalog,
        service->content_prefix ? service->content_prefix : "content",
        error,
        size
    );
    if (!snapshot) {
        if (service->catalog_release) {
            service->catalog_release(service->catalog_user, owner);
        }
        return NULL;
    }
    snapshot->catalog_owner = owner;
    snapshot->catalog_user = service->catalog_user;
    snapshot->catalog_release = service->catalog_release;
    return snapshot;
}
