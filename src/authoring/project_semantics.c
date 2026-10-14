#include "authoring/project_semantics.h"

#include "authoring/project_ui_bindings.h"
#include "authoring/source_catalog_snapshot.h"
#include "authoring/source_intelligence.h"
#include "authoring/source_rename.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct project_catalog_generation
{
    source_catalog *catalog;
    arena storage;
    script_catalog custom;
    bool has_custom;
    int references, count;
    int handles[WORKSPACE_DOCUMENT_COUNT_MAX], revisions[WORKSPACE_DOCUMENT_COUNT_MAX];
} project_catalog_generation;
struct project_semantics
{
    workspace_documents *documents;
    char *root, *content;
    const script_catalog *native;
    project_semantics_catalog_fn catalog;
    void *user;
    project_semantics_candidate_fn candidate;
    project_semantics_candidate_release_fn candidate_release;
    void *candidate_user;
    project_semantics_registry_fn registry;
    void *registry_user;
    project_catalog_generation *cached;
};
typedef struct
{
    source_catalog *catalog;
    authoring_document *documents;
    script_analysis **analyses;
    authoring_navigation *navigation;
    int handles[WORKSPACE_DOCUMENT_COUNT_MAX],
        revisions[WORKSPACE_DOCUMENT_COUNT_MAX], count;
    source_catalog_edit *overlays;
    bool valid;
    arena binding_storage;
    project_ui_unresolved *uncertain;
    int uncertain_count;
} snapshot;
static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static const char *copy(arena *storage, const char *text)
{
    if (!text) {
        text = "";
    }
    char *out = arena_alloc(storage, strlen(text) + 1);
    strcpy(out, text);
    return out;
}
project_semantics *project_semantics_new_content(
    workspace_documents *documents,
    const char *root,
    const char *content,
    const script_catalog *native
)
{
    if (!documents || !root || !*root || !native ||
        !source_catalog_identity_valid(content)) {
        return NULL;
    }
    project_semantics *service = xcalloc(1, sizeof *service);
    service->documents = documents;
    size_t length = strlen(root);
    while (length > 1 && root[length - 1] == '/') {
        length--;
    }
    service->root = xmalloc(length + 1);
    memcpy(service->root, root, length);
    service->root[length] = 0;
    size_t capacity = length + strlen(content) + 2;
    service->content = xmalloc(capacity);
    snprintf(service->content, capacity, "%s/%s", service->root, content);
    service->native = native;
    return service;
}
project_semantics *project_semantics_new(
    workspace_documents *documents,
    const char *root,
    const script_catalog *native
)
{
    return project_semantics_new_content(documents, root, "content", native);
}

void project_semantics_free(project_semantics *service)
{
    if (!service) {
        return;
    }
    project_semantics_refresh(service);
    free(service->root);
    free(service->content);
    free(service);
}
void project_semantics_set_catalog(
    project_semantics *service,
    project_semantics_catalog_fn catalog,
    void *user
)
{
    project_semantics_refresh(service);
    service->catalog = catalog;
    service->user = user;
}
void project_semantics_set_candidate_catalog(
    project_semantics *service,
    project_semantics_candidate_fn candidate,
    project_semantics_candidate_release_fn release,
    void *user
)
{
    assert(!candidate == !release);
    service->candidate = candidate;
    service->candidate_release = release;
    service->candidate_user = user;
}
void project_semantics_set_registry(
    project_semantics *service,
    project_semantics_registry_fn registry,
    void *user
)
{
    service->registry = registry;
    service->registry_user = user;
}
static void release(snapshot *s)
{
    authoring_navigation_free(s->navigation);
    arena_free(&s->binding_storage);
    for (int i = 0; i < acount(s->analyses); i++) {
        script_analysis_free(s->analyses[i]);
    }
    afree(s->analyses);
    afree(s->documents);
    source_catalog_free(s->catalog);
    for (int i = 0; i < acount(s->overlays); i++) {
        free((char *)s->overlays[i].path);
        free((char *)s->overlays[i].text);
    }
    afree(s->overlays);
    *s = (snapshot){ 0 };
}
static const char *relative(const project_semantics *service, const char *path)
{
    size_t n = strlen(service->root);
    return !strncmp(service->root, path, n) && path[n] == '/' ? path + n + 1 : path;
}
static bool analyze(snapshot *s, const prototype_registry *registry, char *error, size_t size)
{
    s->valid = source_catalog_diagnostic_count(s->catalog) == 0;
    const script_catalog *catalog = source_catalog_view(s->catalog);
    for (int i = 0; i < source_catalog_source_count(s->catalog); i++) {
        const source_catalog_source *source = source_catalog_source_at(s->catalog, i);
        script_analysis
            *analysis = script_analyze_document(source->syntax, catalog, source->identity);
        apush(s->analyses, analysis);
        apush(s->documents, ((authoring_document){ source->path, source->syntax, analysis }));
        s->valid &= analysis->valid;
    }
    source_catalog_reference *ui = NULL;
    int ui_count = 0;
    if (!project_ui_bindings(
            s->catalog,
            (const script_analysis *const *)s->analyses,
            acount(s->analyses),
            registry,
            &s->binding_storage,
            &ui,
            &ui_count,
            &s->uncertain,
            &s->uncertain_count,
            error,
            size
        )) {
        return false;
    }
    int original_count = source_catalog_reference_count(s->catalog);
    source_catalog_reference *references = arena_alloc(
        &s->binding_storage,
        (size_t)(original_count + ui_count) * sizeof *references
    );
    if (original_count) {
        memcpy(references, source_catalog_references(s->catalog), (size_t)original_count * sizeof *references);
    }
    if (ui_count) {
        memcpy(references + original_count, ui, (size_t)ui_count * sizeof *references);
    }
    s->navigation = authoring_navigation_new_project(
        s->documents,
        acount(s->documents),
        catalog,
        source_catalog_definitions(s->catalog),
        source_catalog_definition_count(s->catalog),
        references,
        original_count + ui_count
    );
    return s->navigation != NULL;
}
static source_catalog *catalog_with_custom(
    project_semantics *service,
    const script_catalog *custom,
    const source_catalog_edit *overlays,
    int count,
    char *error,
    size_t size
)
{
    (void)error;
    (void)size;
    script_catalog native = *service->native;
    script_catalog_type *types = NULL;
    if (custom) {
        for (int i = 0; i < native.type_count; i++) {
            apush(types, native.types[i]);
        }
        for (int i = 0; i < custom->type_count; i++) {
            bool duplicate = false;
            for (int j = 0; j < acount(types); j++) {
                if (types[j].id == custom->types[i].id ||
                    !strcmp(types[j].name, custom->types[i].name)) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                apush(types, custom->types[i]);
            }
        }
        native.types = types;
        native.type_count = acount(types);
    }
    source_catalog *out = source_catalog_edited(service->content, &native, overlays, count);
    afree(types);
    return out;
}
static source_catalog *catalog(
    project_semantics *service,
    const source_catalog_edit *overlays,
    int count,
    char *error,
    size_t size
)
{
    const script_catalog *custom = service->catalog
                                     ? service->catalog(service->user, error, size)
                                     : NULL;
    if (service->catalog && !custom) {
        return NULL;
    }
    return catalog_with_custom(service, custom, overlays, count, error, size);
}
void project_semantics_catalog_release(void *service, void *owner)
{
    (void)service;
    project_catalog_generation *generation = owner;
    if (!generation || --generation->references) {
        return;
    }
    source_catalog_free(generation->catalog);
    arena_free(&generation->storage);
    free(generation);
}
void project_semantics_refresh(project_semantics *service)
{
    if (!service) {
        return;
    }
    project_semantics_catalog_release(NULL, service->cached);
    service->cached = NULL;
}
bool project_semantics_catalog_acquire(
    void *host,
    const script_catalog **view,
    void **owner,
    char *error,
    size_t size
)
{
    project_semantics *service = host;
    *view = NULL;
    *owner = NULL;
    if (size) {
        *error = 0;
    }
    if (!service) {
        return fail(error, size, "Project semantic service is unavailable");
    }
    const script_catalog *custom = service->catalog
                                     ? service->catalog(service->user, error, size)
                                     : NULL;
    if (service->catalog && !custom) {
        return false;
    }
    int handles[WORKSPACE_DOCUMENT_COUNT_MAX], revisions[WORKSPACE_DOCUMENT_COUNT_MAX];
    int count = workspace_document_handles(service->documents, handles, WORKSPACE_DOCUMENT_COUNT_MAX);
    bool changed = !service->cached || count != service->cached->count ||
                   (custom != NULL) != (service->cached && service->cached->has_custom);
    if (!changed && custom) {
        changed = !source_catalog_metadata_equal(custom, &service->cached->custom);
    }
    for (int i = 0; i < count; i++) {
        workspace_document_view document;
        workspace_document_read(service->documents, handles[i], &document);
        revisions[i] = document.revision;
        if (!changed) {
            changed = handles[i] != service->cached->handles[i] ||
                      revisions[i] != service->cached->revisions[i];
        }
    }
    if (changed) {
        project_catalog_generation *generation = xcalloc(1, sizeof *generation);
        generation->references = 1;
        generation->count = count;
        generation->has_custom = custom != NULL;
        memcpy(generation->handles, handles, (size_t)count * sizeof *handles);
        memcpy(generation->revisions, revisions, (size_t)count * sizeof *revisions);
        if (custom &&
            !source_catalog_snapshot(&generation->storage, custom, &generation->custom)) {
            project_semantics_catalog_release(NULL, generation);
            return fail(error, size, "Could not retain project component declarations");
        }
        source_catalog_edit *overlays = NULL;
        for (int i = 0; i < count; i++) {
            workspace_document_view document;
            workspace_document_read(service->documents, handles[i], &document);
            size_t capacity = strlen(service->root) + strlen(document.path) + 2;
            char *path = xmalloc(capacity);
            snprintf(path, capacity, "%s/%s", service->root, document.path);
            if (source_catalog_edit_path(service->content, path)) {
                apush(overlays, ((source_catalog_edit){ path, document.text, NULL }));
            } else {
                free(path);
            }
        }
        generation->catalog = catalog_with_custom(service, custom, overlays, acount(overlays), error, size);
        for (int i = 0; i < acount(overlays); i++) {
            free((char *)overlays[i].path);
        }
        afree(overlays);
        if (!generation->catalog) {
            project_semantics_catalog_release(NULL, generation);
            return false;
        }
        project_semantics_refresh(service);
        service->cached = generation;
    }
    service->cached->references++;
    *view = source_catalog_view(service->cached->catalog);
    *owner = service->cached;
    return true;
}
static bool capture(
    project_semantics *service,
    int document,
    int revision,
    snapshot *s,
    int *selected,
    char *error,
    size_t size
)
{
    workspace_document_view view;
    workspace_document_status status = workspace_document_read(service->documents, document, &view);
    if (status) {
        return fail(error, size, workspace_document_status_text(status));
    }
    if (view.revision != revision) {
        return fail(error, size, workspace_document_status_text(WORKSPACE_DOCUMENT_STALE));
    }
    if (!*view.path) {
        return fail(error, size, "Save this source in the workspace before querying project semantics");
    }
    s->count = workspace_document_handles(service->documents, s->handles, WORKSPACE_DOCUMENT_COUNT_MAX);
    char *path = xmalloc(strlen(service->root) + strlen(view.path) + 2);
    snprintf(
        path,
        strlen(service->root) + strlen(view.path) + 2,
        "%s/%s",
        service->root,
        view.path
    );
    for (int i = 0; i < s->count; i++) {
        workspace_document_read(service->documents, s->handles[i], &view);
        s->revisions[i] = view.revision;
        char *absolute = xmalloc(strlen(service->root) + strlen(view.path) + 2);
        snprintf(
            absolute,
            strlen(service->root) + strlen(view.path) + 2,
            "%s/%s",
            service->root,
            view.path
        );
        if (source_catalog_edit_path(service->content, absolute)) {
            apush(s->overlays, ((source_catalog_edit){ absolute, xstrdup(view.text), NULL }));
        } else {
            free(absolute);
        }
    }
    s->catalog = catalog(service, s->overlays, acount(s->overlays), error, size);
    const prototype_registry
        *registry = service->registry
                      ? service->registry(service->registry_user, NULL, error, size)
                      : NULL;
    bool ok = s->catalog && (!service->registry || registry) &&
              analyze(s, registry, error, size);
    *selected = -1;
    for (int i = 0; ok && i < authoring_navigation_document_count(s->navigation); i++) {
        if (!strcmp(authoring_navigation_document_at(s->navigation, i)->path, path)) {
            *selected = i;
            break;
        }
    }
    free(path);
    if (!ok) {
        if (!size || !*error) {
            fail(error, size, "Could not build project semantic snapshot");
        }
        return false;
    }
    return *selected >= 0 ||
           fail(error, size, "Document is outside the project source/declaration folders");
}
static bool page(int first, int count, char *error, size_t size)
{
    return (first >= 0 && count >= 0 && count <= 512) ||
           fail(error, size, "Semantic page requires First>=0 and Count in0..512");
}
bool project_semantics_references(
    project_semantics *service,
    int document,
    int revision,
    int offset,
    int first,
    int count,
    arena *storage,
    project_reference **rows,
    int *row_count,
    int *total,
    char *error,
    size_t size
)
{
    *rows = NULL;
    *row_count = *total = 0;
    if (size) {
        *error = 0;
    }
    if (!service) {
        return fail(error, size, "Project semantic service is unavailable");
    }
    if (!page(first, count, error, size)) {
        return false;
    }
    snapshot s = { 0 };
    int selected;
    bool ok = capture(service, document, revision, &s, &selected, error, size);
    const authoring_reference *at = ok ? authoring_navigation_at(s.navigation, selected, offset)
                                       : NULL;
    if (ok && !at) {
        ok = fail(error, size, "Select a resolved symbol");
    }
    if (ok) {
        *rows = count ? arena_alloc(storage, (size_t)count * sizeof **rows) : NULL;
        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; i < authoring_navigation_reference_count(s.navigation);
                 i++) {
                const authoring_reference
                    *reference = authoring_navigation_reference_at(s.navigation, i);
                if ((reference->kind == AUTHORING_DEFINE) != (pass == 0) ||
                    !authoring_target_equal(reference->target, at->target)) {
                    continue;
                }
                int index = (*total)++;
                if (index < first || index - first >= count) {
                    continue;
                }
                const authoring_document *source = authoring_navigation_document_at(
                    s.navigation,
                    reference->location.document
                );
                (*rows)[(*row_count)++] = (project_reference){
                    copy(storage, relative(service, source->path)),
                    copy(
                        storage,
                        authoring_navigation_name(s.navigation, reference->target)
                    ),
                    reference->location.range.start,
                    reference->location.range.end,
                    reference->kind
                };
            }
        }
    }
    release(&s);
    return ok;
}
bool project_semantics_calls(
    project_semantics *service,
    int document,
    int revision,
    int offset,
    int direction,
    int first,
    int count,
    arena *storage,
    document_call **rows,
    int *row_count,
    int *total,
    char *error,
    size_t size
)
{
    *rows = NULL;
    *row_count = *total = 0;
    if (size) {
        *error = 0;
    }
    if (!service) {
        return fail(error, size, "Project semantic service is unavailable");
    }
    if (!page(first, count, error, size)) {
        return false;
    }
    if (direction < 0 || direction > 1) {
        return fail(error, size, "Call direction must be0 incoming or1 outgoing");
    }
    snapshot s = { 0 };
    int selected;
    bool ok = capture(service, document, revision, &s, &selected, error, size);
    const authoring_reference *reference = ok ? authoring_navigation_at(s.navigation, selected, offset)
                                              : NULL;
    authoring_target target = { 0 };
    if (reference) {
        target = authoring_navigation_callable(s.navigation, reference->target);
    }
    if (ok && !target.id) {
        const authoring_document *source = authoring_navigation_document_at(s.navigation, selected);
        const script_analysis *analysis = source->analysis;
        for (int i = 0; analysis && i < analysis->function_count; i++) {
            const script_function *function = &analysis->functions[i];
            if (offset >= function->range.start && offset < function->range.end) {
                const script_symbol *symbol = &analysis->symbols[function->symbol];
                target = (authoring_target){ AUTHORING_CALLABLE,
                                             -1,
                                             symbol->capability ? symbol->capability
                                                                : symbol->id,
                                             0 };
                break;
            }
        }
    }
    if (ok && !target.id) {
        ok = fail(error, size, "Select a resolved function or call");
    }
    if (ok) {
        *rows = count ? arena_alloc(storage, (size_t)count * sizeof **rows) : NULL;
        for (int i = 0; i < authoring_navigation_call_count(s.navigation); i++) {
            const authoring_call *call = authoring_navigation_call_at(s.navigation, i);
            authoring_target caller = authoring_navigation_caller(s.navigation, call);
            if (!authoring_target_equal(direction ? caller : call->callee, target)) {
                continue;
            }
            int index = (*total)++;
            if (index < first || index - first >= count) {
                continue;
            }
            authoring_target other = direction ? call->callee : caller;
            const authoring_reference
                *definition = authoring_navigation_definition(s.navigation, other);
            const authoring_document *source = authoring_navigation_document_at(
                s.navigation,
                call->location.document
            );
            const authoring_document *destination = definition
                                                      ? authoring_navigation_document_at(
                                                            s.navigation,
                                                            definition->location.document
                                                        )
                                                      : NULL;
            char *signature = authoring_source_hover(
                s.navigation,
                definition ? definition->location.document : call->location.document,
                definition ? definition->location.range.start
                           : call->location.range.start
            );
            (*rows)[(*row_count)++] = (document_call){
                copy(storage, relative(service, source->path)),
                copy(storage, authoring_navigation_name(s.navigation, other)),
                copy(storage, signature),
                copy(storage, destination ? relative(service, destination->path) : ""),
                call->location.range.start,
                call->location.range.end,
                definition ? definition->location.range.start : 0,
                definition ? definition->location.range.end : 0
            };
            free(signature);
        }
    }
    release(&s);
    return ok;
}

bool project_semantics_rename(
    project_semantics *service,
    int document,
    int revision,
    int offset,
    const char *name,
    int *changed,
    int *changed_count,
    char *error,
    size_t size
)
{
    *changed_count = 0;
    if (size) {
        *error = 0;
    }
    if (!service) {
        return fail(error, size, "Project semantic service is unavailable");
    }
    snapshot before = { 0 }, after = { 0 };
    int selected;
    bool ok = capture(service, document, revision, &before, &selected, error, size);
    if (ok && !before.valid) {
        ok = fail(error, size, "Resolve project semantic diagnostics before renaming");
    }
    const authoring_reference *reference = ok ? authoring_navigation_at(before.navigation, selected, offset)
                                              : NULL;
    if (ok && !reference) {
        ok = fail(error, size, "Select a resolved project symbol");
    }
    if (ok && reference->target.kind == AUTHORING_FIELD) {
        const char *field_name = NULL;
        for (int i = 0; i < source_catalog_definition_count(before.catalog); i++) {
            const source_catalog_definition *d = &source_catalog_definitions(
                before.catalog
            )[i];
            if (d->field && d->id == reference->target.id &&
                d->owner == reference->target.owner) {
                field_name = d->name;
                break;
            }
        }
        for (int i = 0; ok && field_name && i < before.uncertain_count; i++) {
            const project_ui_unresolved *u = &before.uncertain[i];
            if (!strcmp(u->reference.name, field_name) &&
                (!u->reference.owner || u->reference.owner == reference->target.owner)) {
                if (size) {
                    snprintf(
                        error,
                        size,
                        "Cannot prove UI binding receiver for '%s' at %s:%d:%d",
                        field_name,
                        u->path,
                        u->reference.range.line,
                        u->reference.range.column
                    );
                }
                ok = false;
            }
        }
    }
    authoring_rename_plan *
        plan = ok ? authoring_source_rename(before.navigation, reference->target, name, error, size)
                  : NULL;
    if (ok && !plan) {
        ok = false;
    }
    source_catalog_edit *overlays = NULL;
    char *candidates[WORKSPACE_DOCUMENT_COUNT_MAX] = { 0 };
    const char *paths[WORKSPACE_DOCUMENT_COUNT_MAX] = { 0 };
    int indexes[WORKSPACE_DOCUMENT_COUNT_MAX], count = 0;
    int opened[WORKSPACE_DOCUMENT_COUNT_MAX], opened_count = 0;
    workspace_document_replacement replacements[WORKSPACE_DOCUMENT_COUNT_MAX];
    for (int i = 0; ok && i < plan->count; i++) {
        int index = plan->edits[i].document;
        bool duplicate = false;
        for (int j = 0; j < count; j++) {
            duplicate |= indexes[j] == index;
        }
        if (duplicate) {
            continue;
        }
        if (count == WORKSPACE_DOCUMENT_COUNT_MAX) {
            ok = fail(error, size, "Rename exceeds the64open-document atomic edit limit");
            break;
        }
        const authoring_document
            *source = authoring_navigation_document_at(before.navigation, index);
        const char *text = authoring_navigation_document_text(before.navigation, index);
        char *candidate = authoring_rename_document(plan, index, text, error, size);
        if (!candidate) {
            ok = false;
            break;
        }
        paths[count] = source->path;
        indexes[count] = index;
        candidates[count++] = candidate;
    }
    for (int i = 0; ok && i < count; i++) {
        int handle = 0;
        const char *path = relative(service, paths[i]);
        int handles[WORKSPACE_DOCUMENT_COUNT_MAX];
        int handle_count = workspace_document_handles(service->documents, handles, WORKSPACE_DOCUMENT_COUNT_MAX);
        workspace_document_view view;
        for (int j = 0; j < handle_count; j++) {
            workspace_document_read(service->documents, handles[j], &view);
            if (!strcmp(view.path, path)) {
                handle = handles[j];
                break;
            }
        }
        if (!handle) {
            workspace_document_status
                status = workspace_document_open(service->documents, path, &handle);
            if (status) {
                ok = fail(error, size, workspace_document_status_text(status));
                break;
            }
            opened[opened_count++] = handle;
        }
        workspace_document_read(service->documents, handle, &view);
        const char *expected = authoring_navigation_document_text(before.navigation, indexes[i]);
        if (strcmp(view.text, expected)) {
            ok = fail(error, size, "Rename source changed while preparing its complete project candidate");
            break;
        }
        for (int j = 0; j < before.count; j++) {
            if (before.handles[j] == handle && before.revisions[j] != view.revision) {
                ok = fail(error, size, workspace_document_status_text(WORKSPACE_DOCUMENT_STALE));
                break;
            }
        }
        replacements[i] = (workspace_document_replacement){ handle,
                                                            view.revision,
                                                            candidates[i] };
    }
    if (ok && count) {
        for (int i = 0; i < acount(before.overlays); i++) {
            apush(overlays, before.overlays[i]);
        }
        for (int i = 0; i < count; i++) {
            bool replaced = false;
            for (int j = 0; j < acount(overlays); j++) {
                if (!strcmp(overlays[j].path, paths[i])) {
                    overlays[j].text = candidates[i];
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                apush(overlays, ((source_catalog_edit){ paths[i], candidates[i], NULL }));
            }
        }
        void *owner = NULL;
        const script_catalog *custom = service->candidate
                                         ? service->candidate(
                                               service->candidate_user,
                                               replacements,
                                               count,
                                               &owner,
                                               error,
                                               size
                                           )
                                         : NULL;
        bool schema = reference->target.kind == AUTHORING_TYPE ||
                      (reference->target.kind == AUTHORING_FIELD &&
                       reference->target.owner !=
                           script_catalog_id("type", "SceneGlobals"));
        if (schema && !service->candidate) {
            ok = fail(error, size, "Schema rename requires an unpublished candidate component registry");
        } else if (service->candidate && !custom) {
            ok = false;
        } else {
            after.catalog = service->candidate
                              ? catalog_with_custom(service, custom, overlays, acount(overlays), error, size)
                              : catalog(service, overlays, acount(overlays), error, size);
            const prototype_registry
                *registry = service->registry
                              ? service->registry(service->registry_user, owner, error, size)
                              : NULL;
            ok = after.catalog && (!service->registry || registry) &&
                 analyze(&after, registry, error, size) && after.valid;
        }
        if (owner && service->candidate_release) {
            service->candidate_release(service->candidate_user, owner);
        }
        if (!ok && (!size || !*error)) {
            fail(error, size, "Renamed project does not pass complete semantic validation");
        }
    }
    if (ok && count) {
        workspace_document_status
            status = workspace_document_replace_many(service->documents, replacements, count);
        ok = status == WORKSPACE_DOCUMENT_OK;
        if (!ok) {
            fail(error, size, workspace_document_status_text(status));
        }
    }
    if (ok) {
        for (int i = 0; i < count; i++) {
            changed[i] = replacements[i].document;
        }
        *changed_count = count;
    } else {
        for (int i = 0; i < opened_count; i++) {
            workspace_document_close(service->documents, opened[i]);
        }
    }
    for (int i = 0; i < count; i++) {
        free(candidates[i]);
    }
    afree(overlays);
    authoring_rename_plan_free(plan);
    release(&after);
    release(&before);
    return ok;
}

bool project_semantics_diagnostics(
    project_semantics *service,
    int first,
    int count,
    arena *storage,
    project_diagnostic **rows,
    int *row_count,
    int *total,
    char *error,
    size_t size
)
{
    *rows = NULL;
    *row_count = *total = 0;
    if (size) {
        *error = 0;
    }
    if (!service) {
        return fail(error, size, "Project semantic service is unavailable");
    }
    if (!page(first, count, error, size)) {
        return false;
    }
    const script_catalog *view;
    void *owner = NULL;
    if (!project_semantics_catalog_acquire(service, &view, &owner, error, size)) {
        return false;
    }
    (void)view;
    project_catalog_generation *generation = owner;
    snapshot s = { .catalog = generation->catalog };
    const prototype_registry
        *registry = service->registry
                      ? service->registry(service->registry_user, NULL, error, size)
                      : NULL;
    bool ok = (!service->registry || registry) && analyze(&s, registry, error, size);
    if (ok) {
        *rows = count ? arena_alloc(storage, (size_t)count * sizeof **rows) : NULL;
        for (int i = 0; i < source_catalog_diagnostic_count(s.catalog); i++) {
            const source_catalog_diagnostic
                *diagnostic = source_catalog_diagnostic_at(s.catalog, i);
            int index = (*total)++;
            if (index < first || index - first >= count) {
                continue;
            }
            (*rows)[(*row_count)++] = (project_diagnostic){
                copy(storage, relative(service, diagnostic->path)),
                copy(storage, diagnostic->message),
                diagnostic->range.start,
                diagnostic->range.end,
                diagnostic->range.line,
                diagnostic->range.column
            };
        }
        for (int i = 0; i < acount(s.documents); i++) {
            const script_analysis *analysis = s.documents[i].analysis;
            for (int j = 0; j < analysis->diagnostic_count; j++) {
                const script_diagnostic *diagnostic = &analysis->diagnostics[j];
                int index = (*total)++;
                if (index < first || index - first >= count) {
                    continue;
                }
                (*rows)[(*row_count)++] = (project_diagnostic){
                    copy(storage, relative(service, s.documents[i].path)),
                    copy(storage, diagnostic->message),
                    diagnostic->range.start,
                    diagnostic->range.end,
                    diagnostic->range.line,
                    diagnostic->range.column
                };
            }
        }
    }
    s.catalog = NULL;
    release(&s);
    project_semantics_catalog_release(NULL, owner);
    return ok;
}
