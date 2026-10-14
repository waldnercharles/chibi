#include "authoring/workspace_css.h"

#include "authoring/ui_source_provider.h"
#include "authoring/workspace_documents.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/rcss.h"
#include "support/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CSS_ROWS_MAX 4096
static const char *own_n(arena *a, const char *text, size_t size)
{
    char *copy = arena_alloc(a, size + 1);
    memcpy(copy, text, size);
    copy[size] = 0;
    return copy;
}
static const char *own(arena *a, const char *text)
{
    return own_n(a, text, strlen(text));
}
typedef struct
{
    arena *storage;
    workspace_css_row *rows;
} inventory;
static bool visit(void *user, const char *path, const char *text)
{
    inventory *list = user;
    rcss_variable *variables = NULL;
    rcss_variables(text, &variables);
    if (acount(variables) > CSS_ROWS_MAX - acount(list->rows)) {
        afree(variables);
        return false;
    }
    char hash[65];
    sha256_hex(text, strlen(text), hash);
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *extension = strrchr(base, '.');
    for (int i = 0; i < acount(variables); i++) {
        const rcss_variable *v = &variables[i];
        apush(
            list->rows,
            ((workspace_css_row){
                .path = own(list->storage, path),
                .file = own_n(list->storage, base, extension ? (size_t)(extension - base) : strlen(base)),
                .selector = own(list->storage, v->selector),
                .name = own(list->storage, v->name),
                .value = own_n(list->storage, text + v->start, (size_t)(v->end - v->start)),
                .fingerprint = own(list->storage, hash),
                .start = v->start,
                .end = v->end })
        );
    }
    afree(variables);
    return true;
}
bool workspace_css_query(
    ui_source_provider *provider,
    const char *page,
    arena *a,
    workspace_css_row **rows,
    int *count,
    char *error,
    size_t size
)
{
    *rows = NULL;
    *count = 0;
    if (size) {
        error[0] = 0;
    }
    inventory list = { .storage = a };
    bool ok = provider && ui_source_provider_refresh(provider, error, size) &&
              ui_source_provider_styles(provider, page, visit, &list, error, size);
    if (ok && acount(list.rows)) {
        *count = acount(list.rows);
        *rows = arena_alloc(a, (size_t)*count * sizeof **rows);
        memcpy(*rows, list.rows, (size_t)*count * sizeof **rows);
    }
    if (!ok && size && !error[0]) {
        snprintf(error, size, "UI stylesheet source is unavailable");
    }
    afree(list.rows);
    return ok;
}
static bool refuse(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
bool workspace_css_edit(
    workspace_documents *documents,
    const char *path,
    const char *fingerprint,
    int start,
    int end,
    const char *old_value,
    const char *value,
    int *document,
    int *revision,
    char *error,
    size_t size
)
{
    *document = *revision = 0;
    if (!path || strlen(path) < 6 || strcmp(path + strlen(path) - 5, ".rcss") ||
        !fingerprint || !old_value || !value || start < 0 || end < start) {
        return refuse(error, size, "Invalid stylesheet edit");
    }
    char *source = NULL;
    workspace_document_status status = workspace_document_read_path(documents, path, &source);
    if (status != WORKSPACE_DOCUMENT_OK) {
        return refuse(error, size, workspace_document_status_text(status));
    }
    size_t bytes = strlen(source);
    char hash[65];
    sha256_hex(source, bytes, hash);
    bool same = !strcmp(hash, fingerprint) && (size_t)end <= bytes &&
                (size_t)(end - start) == strlen(old_value) &&
                !memcmp(source + start, old_value, strlen(old_value));
    free(source);
    if (!same) {
        return refuse(error, size, "Stylesheet changed; refresh before editing");
    }
    status = workspace_document_open(documents, path, document);
    workspace_document_view view;
    if (status == WORKSPACE_DOCUMENT_OK) {
        status = workspace_document_read(documents, *document, &view);
    }
    if (status != WORKSPACE_DOCUMENT_OK) {
        return refuse(error, size, workspace_document_status_text(status));
    }
    sha256_hex(view.text, view.bytes, hash);
    if (strcmp(hash, fingerprint)) {
        return refuse(error, size, "Stylesheet changed while opening");
    }
    *revision = view.revision;
    status = workspace_document_edit(documents, *document, *revision, start, end, value);
    if (status == WORKSPACE_DOCUMENT_OK) {
        status = workspace_document_read(documents, *document, &view);
    }
    if (status == WORKSPACE_DOCUMENT_OK) {
        *revision = view.revision;
        status = workspace_document_save(documents, *document, *revision);
    }
    if (status != WORKSPACE_DOCUMENT_OK) {
        return refuse(error, size, workspace_document_status_text(status));
    }
    if (size) {
        error[0] = 0;
    }
    return true;
}

bool workspace_css_document_query(
    workspace_css_service *service,
    int document,
    int revision,
    arena *storage,
    workspace_css_row **rows,
    int *count,
    char *error,
    size_t size
)
{
    *rows = NULL;
    *count = 0;
    workspace_document_view scene;
    workspace_document_status status = workspace_document_read(service->documents, document, &scene);
    if (status != WORKSPACE_DOCUMENT_OK || scene.revision != revision) {
        return refuse(error, size, "UI source document is stale");
    }
    av_doc *source = av_doc_read(scene.text, error, size);
    if (!source) {
        return false;
    }
    const char *page = av_get_str(av_obj_get(av_doc_root(source), "ui"));
    char *manifest = NULL;
    av_doc *project = NULL;
    if (!page || !*page) {
        status = workspace_document_read_path(service->documents, "project.kdl", &manifest);
        if (status != WORKSPACE_DOCUMENT_OK) {
            av_doc_free(source);
            return refuse(error, size, workspace_document_status_text(status));
        }
        project = av_doc_read(manifest, error, size);
        if (!project) {
            av_doc_free(source);
            free(manifest);
            return false;
        }
        page = av_get_str(av_obj_get(av_doc_root(project), "ui"));
    }
    bool ok = true;
    if (page && *page) {
        ok = workspace_css_query(service->provider, page, storage, rows, count, error, size);
    } else if (size) {
        error[0] = 0;
    }
    av_doc_free(project);
    av_doc_free(source);
    free(manifest);
    return ok;
}
