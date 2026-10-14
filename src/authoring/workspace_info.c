#include "authoring/workspace_info.h"

#include "assets/project.h"
#include "authoring/workspace_documents.h"
#include "support/asset_text.h"
#include "support/rooted_files.h"
#include "support/utf8.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct workspace_info
{
    char *root;
    workspace_documents *reader;
    workspace_manifest manifest;
    bool documents_restored;
    char *pending_root;
    int pending_version;
};
static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static bool safe_path(const char *path)
{
    if (!path || !*path || *path == '/' || strlen(path) > WORKSPACE_DOCUMENT_PATH_MAX ||
        strchr(path, '\\') || strchr(path, ':')) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)path; *p; p++) {
        if (*p < 32 || *p == 127) {
            return false;
        }
    }
    for (const char *p = path; *p;) {
        const char *slash = strchr(p, '/');
        size_t length = slash ? (size_t)(slash - p) : strlen(p);
        if (!length || (length == 1 && *p == '.') ||
            (length == 2 && !memcmp(p, "..", 2))) {
            return false;
        }
        if (!slash) {
            return true;
        }
        p = slash + 1;
    }
    return false;
}
static char *relative(const char *root, const char *path)
{
    if (*path != '/') {
        return safe_path(path) ? xstrdup(path) : NULL;
    }
    size_t length = strlen(root);
    if (strncmp(root, path, length) || path[length] != '/') {
        return NULL;
    }
    return safe_path(path + length + 1) ? xstrdup(path + length + 1) : NULL;
}
void workspace_manifest_free(workspace_manifest *manifest)
{
    free(manifest->root);
    free(manifest->name);
    free(manifest->content);
    free(manifest->start_scene);
    *manifest = (workspace_manifest){ 0 };
}
bool workspace_manifest_parse(
    const char *root,
    const char *text,
    workspace_manifest *out,
    char *error,
    size_t size
)
{
    *out = (workspace_manifest){ 0 };
    if (!root || !*root || !text || strlen(text) > 65536) {
        return fail(error, size, "Workspace manifest exceeds64KiB or is missing");
    }
    av_doc *document = av_doc_read(text, error, size);
    if (!document) {
        return false;
    }
    const av_value *value = av_doc_root(document);
    const av_value *version = av_obj_get(value, "version");
    bool valid = av_is_obj(value) && av_is_int(version) &&
                 av_get_int(version) == CHIBI_PROJECT_VERSION;
    if (!valid) {
        av_doc_free(document);
        return fail(error, size, "Workspace requires a current-version project.kdl manifest");
    }
    const av_value *name_value = av_obj_get(value, "name"),
                   *content_value = av_obj_get(value, "content"),
                   *start_value = av_obj_get(value, "start_scene");
    const char *name = name_value ? av_get_str(name_value) : strrchr(root, '/');
    if (!name_value) {
        name = name ? name + 1 : root;
    }
    const char *content = content_value ? av_get_str(content_value) : "content";
    const char *start = av_get_str(start_value);
    valid = name && *name && strlen(name) <= 1024 && content && *content &&
            start && *start;
    workspace_manifest candidate = { .version = CHIBI_PROJECT_VERSION };
    if (valid) {
        candidate.root = xstrdup(root);
        candidate.name = xstrdup(name);
        candidate.content = relative(root, content);
        if (start[0] == '/' || !strncmp(start, "content/", 8)) {
            candidate.start_scene = relative(root, start);
        } else if (candidate.content) {
            size_t capacity = strlen(candidate.content) + strlen(start) + 2;
            char *path = xmalloc(capacity);
            snprintf(path, capacity, "%s/%s", candidate.content, start);
            candidate.start_scene = relative(root, path);
            free(path);
        }
        valid = candidate.content && candidate.start_scene;
    }
    av_doc_free(document);
    if (!valid) {
        workspace_manifest_free(&candidate);
        return fail(error, size, "Manifest requires a name and contained content/start_scene paths");
    }
    *out = candidate;
    if (size) {
        *error = 0;
    }
    return true;
}
workspace_info *workspace_info_idle()
{
    workspace_info *service = xcalloc(1, sizeof *service);
    service->manifest.version = CHIBI_PROJECT_VERSION;
    return service;
}
bool workspace_info_active(const workspace_info *service)
{
    return service && service->reader;
}
void workspace_info_set_documents_restored(workspace_info *service, bool restored)
{
    if (service) {
        service->documents_restored = restored && workspace_info_active(service);
    }
}
bool workspace_info_documents_restored(const workspace_info *service)
{
    return service && service->documents_restored;
}
bool workspace_info_set_pending_project(workspace_info *service, const char *root, int version)
{
    if (!service || workspace_info_active(service)) {
        return false;
    }
    size_t length = root ? strnlen(root, WORKSPACE_DOCUMENT_PATH_MAX + 1) : 0;
    if (length > WORKSPACE_DOCUMENT_PATH_MAX ||
        !utf8_text_valid(root ? root : "", length) ||
        (length ? version < 1 || version >= CHIBI_PROJECT_VERSION : version != 0)) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if ((unsigned char)root[i] < 32 || root[i] == 127) {
            return false;
        }
    }
    char *owned = length ? xstrdup(root) : NULL;
    free(service->pending_root);
    service->pending_root = owned;
    service->pending_version = version;
    return true;
}
const char *workspace_info_pending_root(const workspace_info *service)
{
    return service && !workspace_info_active(service) && service->pending_root
             ? service->pending_root
             : "";
}
int workspace_info_pending_version(const workspace_info *service)
{
    return service && !workspace_info_active(service) ? service->pending_version : 0;
}
workspace_info *workspace_info_new(const char *root, char *error, size_t size)
{
    rooted_files *directory = root ? rooted_files_open(root) : NULL;
    char *canonical = directory ? rooted_files_host_path(directory) : NULL;
    if (directory) {
        rooted_files_close(directory);
    }
    if (!canonical) {
        fail(error, size, "Workspace root cannot be resolved");
        return NULL;
    }
    workspace_document_status status;
    workspace_documents *reader = workspace_documents_new(canonical, &status);
    if (!reader) {
        free(canonical);
        fail(error, size, workspace_document_status_text(status));
        return NULL;
    }
    workspace_info *service = xcalloc(1, sizeof *service);
    service->root = canonical;
    service->reader = reader;
    if (!workspace_info_refresh(service, error, size)) {
        workspace_info_free(service);
        return NULL;
    }
    return service;
}
void workspace_info_free(workspace_info *service)
{
    if (!service) {
        return;
    }
    workspace_documents_free(service->reader);
    workspace_manifest_free(&service->manifest);
    free(service->root);
    free(service->pending_root);
    free(service);
}
bool workspace_info_refresh(workspace_info *service, char *error, size_t size)
{
    if (!workspace_info_active(service)) {
        return fail(error, size, "Open or create a project before refreshing its manifest");
    }
    int handle;
    workspace_document_status
        status = workspace_document_open(service->reader, CHIBI_PROJECT_MANIFEST, &handle);
    if (status) {
        return fail(error, size, workspace_document_status_text(status));
    }
    workspace_document_view view;
    status = workspace_document_read(service->reader, handle, &view);
    workspace_manifest manifest = { 0 };
    bool ok = status == WORKSPACE_DOCUMENT_OK &&
              workspace_manifest_parse(service->root, view.text, &manifest, error, size);
    workspace_document_close(service->reader, handle);
    if (!ok) {
        return false;
    }
    workspace_manifest_free(&service->manifest);
    service->manifest = manifest;
    return true;
}
const workspace_manifest *workspace_info_get(const workspace_info *service)
{
    return service ? &service->manifest : NULL;
}
