#include "authoring/workspace_assets_scope.h"

#include "authoring/workspace_resources.h"
#include "language/component.h"
#include "runtime/component_catalog.h"
#include "runtime/declared_components.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *path, *name, *text;
} source;
typedef struct
{
    char *root;
    const script_catalog *native;
    workspace_resources *resources;
    workspace_documents *reader, *owned_documents;
    declared_components *components;
    const ecs_component_desc **descriptors;
    prototype_registry registry;
    source *sources;
    int handles[WORKSPACE_DOCUMENT_COUNT_MAX],
        revisions[WORKSPACE_DOCUMENT_COUNT_MAX], count;
    bool valid;
    char error[256];
} scope;
static bool fail(char *error, size_t size, const char *text)
{
    if (size) {
        snprintf(error, size, "%s", text);
    }
    return false;
}
static const char *include(const char *name, void *user)
{
    scope *s = user;
    for (int i = 0; i < acount(s->sources); i++) {
        if (s->sources[i].name && !strcmp(s->sources[i].name, name)) {
            return s->sources[i].text;
        }
    }
    return NULL;
}
static void clear_sources(source *rows)
{
    for (int i = 0; i < acount(rows); i++) {
        free(rows[i].path);
        free(rows[i].name);
        free(rows[i].text);
    }
    afree(rows);
}
static bool relevant(const char *path)
{
    size_t n = strlen(path);
    return (strstr(path, "/prototypes/") && n > 4 && !strcmp(path + n - 4, ".kdl")) ||
           (strstr(path, "/components/") && n > 6 && !strcmp(path + n - 6, ".chibi"));
}
static char *read_source(workspace_assets_service *service, const char *path, char *error, size_t size)
{
    scope *s = service->scope;
    int handles[WORKSPACE_DOCUMENT_COUNT_MAX];
    int count = workspace_document_handles(service->documents, handles, WORKSPACE_DOCUMENT_COUNT_MAX);
    for (int i = 0; i < count; i++) {
        workspace_document_view view;
        if (workspace_document_read(service->documents, handles[i], &view) ==
                WORKSPACE_DOCUMENT_OK &&
            !strcmp(path, view.path)) {
            return xstrdup(view.text);
        }
    }
    int handle;
    workspace_document_status status = workspace_document_open(s->reader, path, &handle);
    if (status != WORKSPACE_DOCUMENT_OK) {
        fail(error, size, workspace_document_status_text(status));
        return NULL;
    }
    workspace_document_view view;
    status = workspace_document_read(s->reader, handle, &view);
    char *text = status == WORKSPACE_DOCUMENT_OK ? xstrdup(view.text) : NULL;
    workspace_document_close(s->reader, handle);
    if (!text) {
        fail(error, size, workspace_document_status_text(status));
    }
    return text;
}
static bool add_source(
    workspace_assets_service *service,
    source **rows,
    const char *path,
    size_t *bytes,
    char *error,
    size_t size
)
{
    for (int i = 0; i < acount(*rows); i++) {
        if (!strcmp((*rows)[i].path, path)) {
            return true;
        }
    }
    if (acount(*rows) >= 1024) {
        return fail(error, size, "Asset scope exceeds1024component/prototype sources");
    }
    char *text = read_source(service, path, error, size);
    if (!text) {
        return false;
    }
    *bytes += strlen(text);
    if (*bytes > 64u * 1024u * 1024u) {
        free(text);
        return fail(error, size, "Asset scope exceeds64MiB source text");
    }
    char *name = NULL;
    if (strstr(path, "/prototypes/")) {
        av_doc *parsed = av_doc_read(text, error, size);
        const char *declared = parsed
                                 ? av_get_str(av_obj_get(av_doc_root(parsed), "name"))
                                 : NULL;
        if (!declared || !*declared) {
            av_doc_free(parsed);
            free(text);
            return fail(error, size, "Prototype requires a declared name");
        }
        name = xstrdup(declared);
        av_doc_free(parsed);
        for (int i = 0; i < acount(*rows); i++) {
            if ((*rows)[i].name && !strcmp((*rows)[i].name, name)) {
                free(name);
                free(text);
                return fail(error, size, "Prototype names must be unique in this scope");
            }
        }
    }
    apush(*rows, ((source){ xstrdup(path), name, text }));
    return true;
}
static void stamp(workspace_assets_service *service)
{
    scope *s = service->scope;
    s->count = 0;
    int handles[WORKSPACE_DOCUMENT_COUNT_MAX];
    int count = workspace_document_handles(service->documents, handles, WORKSPACE_DOCUMENT_COUNT_MAX);
    for (int i = 0; i < count; i++) {
        workspace_document_view view;
        if (workspace_document_read(service->documents, handles[i], &view) ==
                WORKSPACE_DOCUMENT_OK &&
            relevant(view.path)) {
            s->handles[s->count] = handles[i];
            s->revisions[s->count++] = view.revision;
        }
    }
}
static bool refresh(workspace_assets_service *service, bool scan_disk, char *error, size_t size)
{
    scope *s = service ? service->scope : NULL;
    if (!s) {
        return true; /* Borrowed registries are refreshed by their host. */
    }
    stamp(service);
    if (scan_disk) {
        workspace_resource_result scan = workspace_resources_refresh(s->resources);
        if (!scan.ok || !scan.complete) {
            snprintf(s->error, sizeof s->error, "%s", scan.message);
            s->valid = false;
            return fail(error, size, s->error);
        }
    }
    source *sources = NULL;
    size_t bytes = 0;
    arena storage = { 0 };
    bool ok = true;
    int first = 0;
    do {
        pin_array *page;
        workspace_resource_result
            result = workspace_resources_query(s->resources, first, 256, "", "", &storage, &page);
        if (!result.ok) {
            ok = fail(error, size, result.message);
            break;
        }
        for (int i = 0; ok && i < page->count; i++) {
            workspace_resource_row *row = page->items[i].ptr;
            if (relevant(row->path)) {
                ok = add_source(service, &sources, row->path, &bytes, error, size);
            }
        }
        first += page->count;
        if (first >= result.total) {
            break;
        }
    } while (ok);
    int handles[WORKSPACE_DOCUMENT_COUNT_MAX];
    int count = workspace_document_handles(service->documents, handles, WORKSPACE_DOCUMENT_COUNT_MAX);
    for (int i = 0; ok && i < count; i++) {
        workspace_document_view view;
        if (workspace_document_read(service->documents, handles[i], &view) ==
                WORKSPACE_DOCUMENT_OK &&
            relevant(view.path)) {
            ok = add_source(service, &sources, view.path, &bytes, error, size);
        }
    }
    script_component **parsed = NULL;
    declared_component *declarations = NULL;
    for (int i = 0; ok && i < acount(sources); i++) {
        if (!strstr(sources[i].path, "/components/")) {
            continue;
        }
        script_syntax *syntax = script_parse(sources[i].text);
        script_diagnostic *diagnostics = NULL;
        script_component *component = script_component_read(syntax, s->native, &diagnostics);
        script_syntax_free(syntax);
        if (!component) {
            ok = fail(error, size, acount(diagnostics) ? diagnostics[0].message : "Invalid component source");
            afree(diagnostics);
            break;
        }
        afree(diagnostics);
        declared_field *fields = NULL;
        for (int f = 0; f < acount(component->fields); f++) {
            script_component_field *field = &component->fields[f];
            apush(
                fields,
                ((declared_field){ field->name,
                                   field->type,
                                   field->description,
                                   field->unit,
                                   field->min,
                                   field->max })
            );
        }
        apush(parsed, component);
        apush(
            declarations,
            ((
                declared_component
            ){ component->name, component->description, fields, acount(fields) })
        );
    }
    declared_components *components = ok ? declared_components_authoring(
                                               declarations,
                                               acount(declarations),
                                               error,
                                               size
                                           )
                                         : NULL;
    ok = ok && components;
    for (int i = 0; i < acount(declarations); i++) {
        declared_field *fields = (declared_field *)declarations[i].fields;
        afree(fields);
    }
    afree(declarations);
    for (int i = 0; i < acount(parsed); i++) {
        script_component_free(parsed[i]);
    }
    afree(parsed);
    arena_free(&storage);
    if (!ok) {
        declared_components_free(components);
        clear_sources(sources);
        snprintf(s->error, sizeof s->error, "%s", error && size ? error : "Asset source scope is invalid");
        s->valid = false;
        return false;
    }
    const ecs_component_desc **descriptors = NULL;
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        apush(descriptors, REGISTRY_COMPONENTS[i]);
    }
    ecs_component_catalog custom = declared_components_ecs(components);
    for (int i = 0; i < custom.component_count; i++) {
        apush(descriptors, custom.components[i]);
    }
    declared_components_free(s->components);
    afree(s->descriptors);
    clear_sources(s->sources);
    s->components = components;
    s->descriptors = descriptors;
    s->sources = sources;
    s->registry = (prototype_registry){ .components = descriptors,
                                        .component_count = acount(descriptors),
                                        .include = include,
                                        .include_user = s };
    service->registry = &s->registry;
    s->valid = true;
    s->error[0] = 0;
    s->count = 0;
    for (int i = 0; i < count; i++) {
        workspace_document_view view;
        workspace_document_read(service->documents, handles[i], &view);
        if (relevant(view.path)) {
            s->handles[s->count] = handles[i];
            s->revisions[s->count++] = view.revision;
        }
    }
    return true;
}
bool workspace_assets_service_refresh(workspace_assets_service *service, char *error, size_t size)
{
    return refresh(service, true, error, size);
}
bool workspace_assets_service_sync(workspace_assets_service *service, char *error, size_t size)
{
    if (!service || !service->documents) {
        return fail(error, size, "Workspace asset service unavailable");
    }
    scope *s = service ? service->scope : NULL;
    if (!s) {
        return true;
    }
    int handles[WORKSPACE_DOCUMENT_COUNT_MAX];
    int count = workspace_document_handles(service->documents, handles, WORKSPACE_DOCUMENT_COUNT_MAX);
    int at = 0;
    for (int i = 0; i < count; i++) {
        workspace_document_view view;
        workspace_document_read(service->documents, handles[i], &view);
        if (!relevant(view.path)) {
            continue;
        }
        if (at >= s->count || handles[i] != s->handles[at] ||
            view.revision != s->revisions[at]) {
            return refresh(service, false, error, size);
        }
        at++;
    }
    if (at != s->count) {
        return refresh(service, false, error, size);
    }
    return s->valid ? true : fail(error, size, s->error);
}
workspace_assets_service *workspace_assets_service_new(
    workspace_documents *documents,
    const char *root,
    const script_catalog *native,
    char *error,
    size_t size
)
{
    workspace_assets_service *service = xcalloc(1, sizeof *service);
    scope *s = xcalloc(1, sizeof *s);
    service->documents = documents;
    service->scope = s;
    s->root = xstrdup(root);
    s->native = native;
    workspace_document_status status;
    s->reader = workspace_documents_new(root, &status);
    s->resources = workspace_resources_new(root, error, size);
    if (!s->reader || !s->resources) {
        workspace_assets_service_free(service);
        return NULL;
    }
    /* Syntax failures stay queryable so the editor can open and repair source. */
    workspace_resource_result inventory = workspace_resources_refresh(s->resources);
    if (inventory.code == WORKSPACE_RESOURCE_UNSAFE_PATH) {
        fail(error, size, inventory.message);
        workspace_assets_service_free(service);
        return NULL;
    }
    char initial_error[256] = { 0 };
    refresh(service, false, initial_error, sizeof initial_error);
    if (error && size) {
        error[0] = 0;
    }
    return service;
}
void workspace_assets_service_free(workspace_assets_service *service)
{
    if (!service) {
        return;
    }
    scope *s = service->scope;
    if (s) {
        workspace_resources_free(s->resources);
        workspace_documents_free(s->reader);
        workspace_documents_free(s->owned_documents);
        declared_components_free(s->components);
        afree(s->descriptors);
        clear_sources(s->sources);
        free(s->root);
        free(s);
    }
    free(service);
}

const script_catalog *workspace_assets_service_component_catalog(const workspace_assets_service *service)
{
    const scope *s = service ? service->scope : NULL;
    return s && s->valid && s->components ? declared_components_catalog(s->components)
                                          : NULL;
}

bool workspace_assets_service_prototype_source(
    workspace_assets_service *service,
    const char *name,
    const char **path,
    const char **text,
    char *error,
    size_t size
)
{
    *path = NULL;
    *text = NULL;
    if (!workspace_assets_service_sync(service, error, size)) {
        return false;
    }
    scope *s = service ? service->scope : NULL;
    if (!s || !name || !*name) {
        return fail(error, size, "Prototype source lookup requires a project source scope and a declared name");
    }
    for (int i = 0; i < acount(s->sources); i++) {
        if (s->sources[i].name && !strcmp(name, s->sources[i].name)) {
            *path = s->sources[i].path;
            *text = s->sources[i].text;
            return true;
        }
    }
    return fail(error, size, "The declared prototype source is unavailable");
}

workspace_assets_service *workspace_assets_service_candidate(
    workspace_assets_service *service,
    const workspace_document_replacement *replacements,
    int count,
    char *error,
    size_t size
)
{
    scope *s = service ? service->scope : NULL;
    if (!s || count < 0 || count > WORKSPACE_DOCUMENT_COUNT_MAX ||
        (count && !replacements)) {
        fail(error, size, "Candidate registry requires a project scope and bounded replacements");
        return NULL;
    }
    workspace_document_status status = count ? workspace_document_validate_replace_anchored(
                                                   service->documents,
                                                   replacements,
                                                   count,
                                                   0
                                               )
                                             : WORKSPACE_DOCUMENT_OK;
    if (status) {
        fail(error, size, workspace_document_status_text(status));
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        workspace_document_view view;
        workspace_document_read(service->documents, replacements[i].document, &view);
        if (!*view.path) {
            fail(error, size, "Candidate declarations require named workspace sources");
            return NULL;
        }
    }
    workspace_documents *documents = workspace_documents_new(s->root, &status);
    if (!documents) {
        fail(error, size, workspace_document_status_text(status));
        return NULL;
    }
    int handles[WORKSPACE_DOCUMENT_COUNT_MAX];
    int total = workspace_document_handles(service->documents, handles, WORKSPACE_DOCUMENT_COUNT_MAX);
    for (int i = 0; i < total; i++) {
        workspace_document_view source;
        workspace_document_read(service->documents, handles[i], &source);
        if (!*source.path) {
            continue;
        }
        const char *text = source.text;
        for (int j = 0; j < count; j++) {
            if (replacements[j].document == handles[i]) {
                text = replacements[j].text;
                break;
            }
        }
        bool exists;
        int handle, revision;
        status = workspace_document_path(documents, source.path, &exists, &handle, &revision);
        if (status) {
            break;
        }
        if (exists) {
            status = workspace_document_open(documents, source.path, &handle);
            if (status) {
                break;
            }
            workspace_document_view view;
            workspace_document_read(documents, handle, &view);
            status = workspace_document_edit(
                documents,
                handle,
                view.revision,
                0,
                (int)view.bytes,
                text
            );
        } else {
            status = workspace_document_draft(documents, source.path, text, &handle);
        }
        if (status) {
            break;
        }
    }
    if (status) {
        fail(error, size, workspace_document_status_text(status));
        workspace_documents_free(documents);
        return NULL;
    }
    workspace_assets_service
        *candidate = workspace_assets_service_new(documents, s->root, s->native, error, size);
    if (!candidate) {
        workspace_documents_free(documents);
        return NULL;
    }
    ((scope *)candidate->scope)->owned_documents = documents;
    if (!workspace_assets_service_sync(candidate, error, size)) {
        workspace_assets_service_free(candidate);
        return NULL;
    }
    return candidate;
}
