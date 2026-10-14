#include "authoring/workspace_resources.h"

#include "support/array.h"
#include "support/asset_json.h"
#include "support/asset_text.h"
#include "support/rooted_files.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define FILE_BYTES_MAX (16 * 1024 * 1024)
#define TOTAL_BYTES_MAX (64 * 1024 * 1024)
#define SOURCE_BYTES_MAX (512 * 1024)
#define DEPTH_MAX 16
struct workspace_resources
{
    rooted_files *files;
    int revision, visited;
    bool annotated;
    char *root, *content;
    workspace_resource_row *rows;
    size_t remaining;
    uint64_t hash;
    workspace_resource_code code;
    char message[256];
};
static char *join(const char *a, const char *b)
{
    size_t size = strlen(a) + strlen(b) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", a, b);
    return path;
}
static void problem(workspace_resources *w, workspace_resource_code code, const char *message)
{
    if (w->code == WORKSPACE_RESOURCE_OK) {
        w->code = code;
        snprintf(w->message, sizeof w->message, "%s", message);
    }
}
static void extend(workspace_resources *w, const void *bytes, size_t size)
{
    const unsigned char *p = bytes;
    for (size_t i = 0; i < size; i++) {
        w->hash = (w->hash ^ p[i]) * UINT64_C(1099511628211);
    }
}
static void text(char const **out, const char *value)
{
    free((void *)*out);
    *out = xstrdup(value ? value : "");
}
static void clear_rows(workspace_resources *w)
{
    for (int i = 0; i < acount(w->rows); i++) {
        workspace_resource_row *r = &w->rows[i];
        free((void *)r->key);
        free((void *)r->kind);
        free((void *)r->name);
        free((void *)r->path);
        free((void *)r->reference);
        free((void *)r->status);
        free((void *)r->message);
        free((void *)r->metadata);
    }
    afree(w->rows);
}
static bool relative_path(const char *path)
{
    return rooted_files_path_valid(path);
}
static const char *suffix(const char *path)
{
    const char *dot = strrchr(path, '.');
    return dot ? dot : "";
}
static const char *file_kind(const char *path)
{
    const char *ext = suffix(path);
    if (!strcasecmp(ext, ".png") || !strcasecmp(ext, ".jpg") ||
        !strcasecmp(ext, ".jpeg")) {
        return "image";
    }
    if (!strcasecmp(ext, ".mesh")) {
        return "mesh";
    }
    if (!strcasecmp(ext, ".wav") || !strcasecmp(ext, ".ogg")) {
        return "sound";
    }
    if (!strcasecmp(ext, ".hdr")) {
        return "sky";
    }
    if (!strcasecmp(ext, ".ttf") || !strcasecmp(ext, ".otf")) {
        return "font";
    }
    if (!strcasecmp(ext, ".rml")) {
        return "ui";
    }
    if (!strcasecmp(ext, ".rcss")) {
        return "stylesheet";
    }
    if (!strcasecmp(ext, ".glb") || !strcasecmp(ext, ".gltf")) {
        return "model-source";
    }
    if (!strcmp(ext, ".chibi")) {
        if (strstr(path, "/libraries/")) {
            return "library";
        }
        if (strstr(path, "/systems/")) {
            return "system";
        }
        return "behavior";
    }
    if (!strcmp(ext, ".kdl")) {
        static const char *const directories[] = {
            "scenes", "prototypes",       "materials", "shapes", "models",
            "fields", "shader-libraries", "events",    "editor", "scenarios"
        };
        static const char *const kinds[] = {
            "scene", "prototype",      "material", "shape",  "model",
            "field", "shader-library", "event",    "editor", "scenario"
        };
        for (int i = 0; i < 10; i++) {
            char part[48];
            snprintf(part, sizeof part, "/%s/", directories[i]);
            if (strstr(path, part)) {
                return kinds[i];
            }
        }
        return "document";
    }
    return NULL;
}
static workspace_resource_row *add_row(workspace_resources *w, const char *kind, const char *path)
{
    const char *slash = strrchr(path, '/');
    char *name = xstrdup(slash ? slash + 1 : path);
    char *dot = strrchr(name, '.');
    if (dot) {
        *dot = 0;
    }
    size_t content_length = strlen(w->content);
    bool concrete = strlen(path) > content_length &&
                    !strncmp(path, w->content, content_length) &&
                    path[content_length] == '/';
    workspace_resource_row row = {
        .reference = concrete ? join("content", path + content_length + 1)
                              : xstrdup(""),
        .key = join(kind, path),
        .kind = xstrdup(kind),
        .name = name,
        .path = xstrdup(path),
        .status = xstrdup("unvalidated"),
        .message = xstrdup("Not decoded or compiled"),
        .metadata = xstrdup("")
    };
    apush(w->rows, row);
    return &alast(w->rows);
}
static unsigned char *read_at(
    workspace_resources *w,
    rooted_files *parent,
    const char *name,
    size_t limit,
    size_t *size
)
{
    unsigned char *bytes = NULL;
    size_t bound = limit < w->remaining ? limit : w->remaining;
    if (!rooted_files_read(parent, name, bound, &bytes, size)) {
        workspace_resource_code code = errno == EFBIG ? WORKSPACE_RESOURCE_LIMIT
                                     : errno == ELOOP || errno == EINVAL
                                         ? WORKSPACE_RESOURCE_UNSAFE_PATH
                                         : WORKSPACE_RESOURCE_IO;
        problem(
            w,
            code,
            code == WORKSPACE_RESOURCE_LIMIT
                ? "Workspace source/file/total byte limit exceeded"
                : "Workspace file could not be read as a stable regular entry"
        );
        return NULL;
    }
    w->remaining -= *size;
    return bytes;
}
static void inspect(workspace_resource_row *row, const unsigned char *bytes, size_t size)
{
    if (!strcmp(suffix(row->path), ".kdl")) {
        char error[256];
        av_doc *doc = memchr(bytes, 0, size)
                        ? NULL
                        : av_doc_read((const char *)bytes, error, sizeof error);
        if (!doc) {
            text(&row->status, "failed");
            text(&row->message, "KDL syntax is invalid; semantic validation not performed");
            return;
        }
        const char *name = av_get_str(av_obj_get(av_doc_root(doc), "name"));
        if (name && name[0]) {
            text(&row->name, name);
        }
        text(&row->status, "syntax-valid");
        text(&row->message, "KDL parsed; semantic validation not performed");
        av_doc_free(doc);
    }
    if (!strcmp(row->kind, "image") && size >= 24 &&
        !memcmp(bytes, "\x89PNG\r\n\x1a\n", 8)) {
        uint32_t width = (uint32_t)bytes[16] << 24 | (uint32_t)bytes[17] << 16 |
                         (uint32_t)bytes[18] << 8 | bytes[19];
        uint32_t height = (uint32_t)bytes[20] << 24 | (uint32_t)bytes[21] << 16 |
                          (uint32_t)bytes[22] << 8 | bytes[23];
        if (width <= INT_MAX && height <= INT_MAX) {
            row->width = (int)width;
            row->height = (int)height;
            text(&row->status, "metadata");
            text(&row->message, "PNG dimensions from header; pixel decode not performed");
        }
    }
}
static void scan(workspace_resources *w, rooted_files *parent, const char *relative, int depth)
{
    rooted_file_entry *entries = NULL;
    int count = 0;
    if (!rooted_files_list(parent, "", 65536, &entries, &count)) {
        problem(
            w,
            errno == EFBIG ? WORKSPACE_RESOURCE_LIMIT : WORKSPACE_RESOURCE_IO,
            "Cannot enumerate workspace directory within scan limits"
        );
        return;
    }
    for (int i = 0; i < count; i++) {
        const rooted_file_entry *entry = &entries[i];
        if (entry->name[0] == '.') {
            continue;
        }
        if (++w->visited > WORKSPACE_RESOURCE_ROWS_MAX) {
            problem(w, WORKSPACE_RESOURCE_LIMIT, "Workspace directory-entry limit exceeded");
            break;
        }
        char *path = join(relative, entry->name);
        const rooted_file_info *info = &entry->info;
        if (strlen(path) >= 4096) {
            problem(w, WORKSPACE_RESOURCE_LIMIT, "Workspace relative path exceeds limit");
        } else if (info->link || (!info->regular && !info->directory)) {
            problem(w, WORKSPACE_RESOURCE_UNSAFE_PATH, "Symlink or nonregular workspace entry excluded");
        } else if (info->directory) {
            rooted_files *child = rooted_files_directory(parent, entry->name, false);
            if (depth < DEPTH_MAX && child) {
                scan(w, child, path, depth + 1);
            } else {
                problem(
                    w,
                    child ? WORKSPACE_RESOURCE_LIMIT : WORKSPACE_RESOURCE_UNSAFE_PATH,
                    "Workspace directory cannot be traversed within scan limits"
                );
            }
            if (child) {
                rooted_files_close(child);
            }
        } else {
            const char *kind = file_kind(path);
            if (kind) {
                extend(w, path, strlen(path) + 1);
                uint64_t encoded_size = info->bytes;
                extend(w, &encoded_size, sizeof encoded_size);
                workspace_resource_row *row = add_row(w, kind, path);
                row->bytes = (double)info->bytes;
                size_t size = 0;
                const char *ext = suffix(path);
                bool source = !strcmp(ext, ".kdl") || !strcmp(ext, ".chibi") ||
                              !strcmp(ext, ".rml") || !strcmp(ext, ".rcss");
                unsigned char *bytes = read_at(
                    w,
                    parent,
                    entry->name,
                    source ? SOURCE_BYTES_MAX : FILE_BYTES_MAX,
                    &size
                );
                if (bytes) {
                    extend(w, bytes, size);
                    inspect(row, bytes, size);
                    free(bytes);
                } else {
                    text(&row->status, w->code == WORKSPACE_RESOURCE_LIMIT ? "limit_exceeded" : "failed");
                    text(&row->message, w->message);
                }
            }
        }
        free(path);
    }
    rooted_file_entries_free(entries, count);
}
static int compare_rows(const void *a, const void *b)
{
    const workspace_resource_row *x = a, *y = b;
    int result = strcmp(x->kind, y->kind);
    if (!result) {
        result = strcmp(x->name, y->name);
    }
    return result ? result : strcmp(x->path, y->path);
}
static const char *code_name(int code)
{
    static const char *const names[] = { "ok",          "invalid-query",
                                         "io",          "limit",
                                         "unsafe-path", "not-found",
                                         "unavailable" };
    assert(code >= 0 && code < (int)(sizeof names / sizeof *names));
    return names[code];
}
static workspace_resource_result result(const workspace_resources *w)
{
    if (!w) {
        return (workspace_resource_result){
            .ok = false,
            .code = WORKSPACE_RESOURCE_UNAVAILABLE,
            .status = "unavailable",
            .message = "Workspace resources host service is unavailable"
        };
    }
    return (workspace_resource_result){ .ok = w->code == WORKSPACE_RESOURCE_OK,
                                        .code = w->code,
                                        .status = code_name(w->code),
                                        .message = w->message,
                                        .revision = w->revision,
                                        .total = acount(w->rows),
                                        .complete = w->code == WORKSPACE_RESOURCE_OK };
}
workspace_resources *workspace_resources_new(const char *root, char *error, size_t size)
{
    assert(root && (!size || error));
    if (size) {
        error[0] = 0;
    }
    rooted_files *files = rooted_files_open(root);
    char *canonical = files ? rooted_files_host_path(files) : NULL;
    if (!canonical) {
        if (files) {
            rooted_files_close(files);
        }
        if (size) {
            snprintf(error, size, "Workspace root cannot be opened: %s", strerror(errno));
        }
        return NULL;
    }
    workspace_resources *w = xcalloc(1, sizeof *w);
    w->root = canonical;
    w->files = files;
    w->content = xstrdup("content");
    w->code = WORKSPACE_RESOURCE_IO;
    snprintf(w->message, sizeof w->message, "Workspace has not been scanned");
    return w;
}
void workspace_resources_free(workspace_resources *w)
{
    if (!w) {
        return;
    }
    clear_rows(w);
    rooted_files_close(w->files);
    free(w->root);
    free(w->content);
    free(w);
}
workspace_resource_result workspace_resources_refresh(workspace_resources *w)
{
    if (!w) {
        return result(NULL);
    }
    uint64_t old_hash = w->hash;
    bool old_annotated = w->annotated;
    w->annotated = false;
    workspace_resource_code old_code = w->code;
    clear_rows(w);
    w->hash = UINT64_C(14695981039346656037);
    w->code = WORKSPACE_RESOURCE_OK;
    w->message[0] = 0;
    w->remaining = TOTAL_BYTES_MAX;
    w->visited = 0;
    free(w->content);
    w->content = xstrdup("content");
    rooted_file_info manifest;
    if (rooted_files_stat(w->files, "project.kdl", &manifest)) {
        size_t size = 0;
        unsigned char *bytes = read_at(w, w->files, "project.kdl", SOURCE_BYTES_MAX, &size);
        av_doc *doc = bytes && !memchr(bytes, 0, size)
                        ? av_doc_read((const char *)bytes, NULL, 0)
                        : NULL;
        if (!doc) {
            problem(w, WORKSPACE_RESOURCE_IO, "Workspace manifest is unreadable or invalid KDL");
        } else {
            const char *content = av_get_str(av_obj_get(av_doc_root(doc), "content"));
            if (content && relative_path(content)) {
                text((const char **)&w->content, content);
            } else if (content) {
                problem(w, WORKSPACE_RESOURCE_UNSAFE_PATH, "Workspace content must be a relative path without traversal");
            }
        }
        if (bytes) {
            extend(w, bytes, size);
        }
        av_doc_free(doc);
        free(bytes);
    } else if (errno != ENOENT) {
        problem(w, errno == ELOOP ? WORKSPACE_RESOURCE_UNSAFE_PATH : WORKSPACE_RESOURCE_IO, "Cannot inspect workspace manifest");
    }
    rooted_files *directory = w->code == WORKSPACE_RESOURCE_OK
                                ? rooted_files_directory(w->files, w->content, false)
                                : NULL;
    if (directory) {
        scan(w, directory, w->content, 0);
        rooted_files_close(directory);
    } else if (w->code == WORKSPACE_RESOURCE_OK) {
        problem(w, WORKSPACE_RESOURCE_UNSAFE_PATH, "Configured content folder cannot be opened without symlink traversal");
    }
    if (acount(w->rows) > 1) {
        asort(w->rows, compare_rows);
    }
    if (w->hash != old_hash || old_code != w->code || old_annotated) {
        w->revision = w->revision == INT_MAX ? 1 : w->revision + 1;
    }
    return result(w);
}
static unsigned char fold(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}
static bool contains(const char *text, const char *query)
{
    for (; *text; text++) {
        size_t at = 0;
        while (query[at] && text[at] &&
               fold((unsigned char)text[at]) == fold((unsigned char)query[at])) {
            at++;
        }
        if (!query[at]) {
            return true;
        }
    }
    return !query[0];
}
static char *arena_text(arena *a, const char *text)
{
    size_t n = strlen(text) + 1;
    char *copy = arena_alloc(a, n);
    memcpy(copy, text, n);
    return copy;
}
static void copy_row(arena *a, workspace_resource_row *out, const workspace_resource_row *row)
{
    *out = *row;
#define COPY(FIELD) out->FIELD = arena_text(a, row->FIELD)
    COPY(key);
    COPY(kind);
    COPY(name);
    COPY(path);
    COPY(reference);
    COPY(status);
    COPY(message);
    COPY(metadata);
#undef COPY
}
workspace_resource_result workspace_resources_query(
    const workspace_resources *w,
    int first,
    int count,
    const char *kind,
    const char *query,
    arena *a,
    pin_array **rows
)
{
    assert(a && rows);
    *rows = pin_array_snapshot_allocate(a, PIN_STRUCT, "WorkspaceResourceRow", 0);
    workspace_resource_result state = result(w);
    if (!w) {
        return state;
    }
    if (!kind || !query || first < 0 || count < 0 || count > WORKSPACE_RESOURCE_PAGE_MAX ||
        strlen(query) > WORKSPACE_RESOURCE_QUERY_MAX || strlen(kind) > 64) {
        state.ok = false;
        state.code = WORKSPACE_RESOURCE_INVALID_QUERY;
        state.status = code_name(state.code);
        state.message = "Resource query must have bounded "
                        "first/count/kind/search values";
        return state;
    }
    workspace_resource_row *matched[WORKSPACE_RESOURCE_PAGE_MAX];
    int total = 0, found = 0;
    for (int i = 0; i < acount(w->rows); i++) {
        workspace_resource_row *row = &w->rows[i];
        if ((kind[0] && strcmp(kind, row->kind)) ||
            (!contains(row->name, query) && !contains(row->path, query))) {
            continue;
        }
        if (total >= first && found < count) {
            matched[found++] = row;
        }
        total++;
    }
    state.total = total;
    *rows = pin_array_snapshot_allocate(a, PIN_STRUCT, "WorkspaceResourceRow", found);
    for (int i = 0; i < found; i++) {
        workspace_resource_row *copy = arena_alloc(a, sizeof *copy);
        copy_row(a, copy, matched[i]);
        (*rows)->items[i] = (pin_value){ .type = PIN_STRUCT,
                                         .type_name = "WorkspaceResourceRow",
                                         .ptr = copy };
    }
    return state;
}
workspace_resource_result workspace_resources_lookup(
    const workspace_resources *w,
    const char *key,
    arena *a,
    workspace_resource_row *row
)
{
    assert(a && row);
    workspace_resource_row empty = { .key = "",
                                     .kind = "",
                                     .name = "",
                                     .path = "",
                                     .reference = "",
                                     .status = "missing",
                                     .message = "Resource key is absent",
                                     .metadata = "" };
    copy_row(a, row, &empty);
    workspace_resource_result state = result(w);
    if (!w) {
        row->status = state.status;
        row->message = state.message;
        return state;
    }
    if (!key || strlen(key) >= 4096 + 64) {
        state.ok = false;
        state.code = WORKSPACE_RESOURCE_INVALID_QUERY;
        state.status = code_name(state.code);
        state.message = "Resource key exceeds limit";
        return state;
    }
    for (int i = 0; i < acount(w->rows); i++) {
        if (!strcmp(w->rows[i].key, key)) {
            copy_row(a, row, &w->rows[i]);
            state.total = 1;
            return state;
        }
    }
    state.ok = false;
    state.code = WORKSPACE_RESOURCE_NOT_FOUND;
    state.status = code_name(state.code);
    state.message = "Resource key is absent from inventory";
    state.total = 0;
    return state;
}
static workspace_resource_row *by_path(workspace_resources *w, const char *path)
{
    for (int i = 0; i < acount(w->rows); i++) {
        if (!strcmp(w->rows[i].path, path)) {
            return &w->rows[i];
        }
    }
    return NULL;
}
void workspace_resources_catalog(workspace_resources *w, const av_doc *catalog)
{
    assert(w && catalog);
    av_value *resources = av_obj_get(av_doc_root(catalog), "resources");
    for (size_t i = 0; i < av_arr_size(resources); i++) {
        av_value *record = av_arr_get(resources, i);
        const char *key = av_get_str(av_obj_get(record, "key"));
        if (!key || strncmp(key, "content/", 8) || !relative_path(key + 8)) {
            continue;
        }
        char *path = join(w->content, key + 8);
        workspace_resource_row *row = by_path(w, path);
        free(path);
        if (!row) {
            continue;
        }
        const char *status = av_get_str(av_obj_get(record, "status")),
                   *message = av_get_str(av_obj_get(record, "message"));
        if (status) {
            text(&row->status, status);
        }
        if (message) {
            text(&row->message, message);
        }
        av_value *meta = av_obj_get(record, "metadata");
        char *json = meta ? av_json_write(meta) : NULL;
        if (json && strlen(json) <= 4096) {
            text(&row->metadata, json);
        }
        free(json);
        row->width = av_get_int(av_obj_get(meta, "width"));
        row->height = av_get_int(av_obj_get(meta, "height"));
        row->vertices = av_get_int(av_obj_get(meta, "vertices"));
        row->triangles = av_get_int(av_obj_get(meta, "triangles"));
    }
    w->annotated = true;
    w->revision = w->revision == INT_MAX ? 1 : w->revision + 1;
}
void workspace_resources_references(workspace_resources *w, const reference_index *index)
{
    assert(w && index);
    static const char *const kinds[] = { "scene", "prototype", "behavior",
                                         "event", "material",  "shape",
                                         "field", "library",   "system",
                                         "ui" };
    int count = reference_index_asset_count(index);
    if (count > WORKSPACE_RESOURCE_ROWS_MAX) {
        problem(w, WORKSPACE_RESOURCE_LIMIT, "Reference annotation count exceeds workspace limit");
        count = WORKSPACE_RESOURCE_ROWS_MAX;
    }
    for (int i = 0; i < count; i++) {
        const reference_asset *asset = reference_index_asset_at(index, i);
        if ((unsigned)asset->kind >= sizeof kinds / sizeof *kinds) {
            continue;
        }
        const char *path = asset->path;
        if (path && strlen(path) > strlen(w->root) &&
            !strncmp(path, w->root, strlen(w->root)) && path[strlen(w->root)] == '/') {
            path += strlen(w->root) + 1;
        }
        workspace_resource_row *row = path && relative_path(path) ? by_path(w, path)
                                                                  : NULL;
        if (!row && asset->unresolved && acount(w->rows) < WORKSPACE_RESOURCE_ROWS_MAX) {
            char *key = join(kinds[asset->kind], asset->name);
            for (int j = 0; j < acount(w->rows); j++) {
                if (!strcmp(w->rows[j].key, key)) {
                    row = &w->rows[j];
                    break;
                }
            }
            if (!row) {
                row = add_row(w, kinds[asset->kind], "");
                text(&row->key, key);
            }
            free(key);
        }
        if (row) {
            text(&row->name, asset->name);
            row->unresolved = asset->unresolved;
            if (asset->unresolved) {
                text(&row->status, "missing");
            }
        }
    }
    if (acount(w->rows) > 1) {
        asort(w->rows, compare_rows);
    }
    w->annotated = true;
    w->revision = w->revision == INT_MAX ? 1 : w->revision + 1;
}
