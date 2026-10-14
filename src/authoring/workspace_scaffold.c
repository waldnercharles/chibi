#include "authoring/workspace_scaffold.h"

#include "assets/project.h"
#include "authoring/workspace_info.h"
#include "support/array.h"
#include "support/rooted_files.h"
#include "support/xalloc.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <yyjson.h>

typedef struct
{
    const char *path, *text;
    size_t bytes;
} scaffold_file;
static bool fail(char *error, size_t size, const char *text)
{
    if (size) {
        snprintf(error, size, "%s", text);
    }
    return false;
}
static bool portable_segment(const char *text, size_t bytes)
{
    if (!bytes || bytes > 255 || text[0] == ' ' || text[bytes - 1] == ' ' ||
        text[bytes - 1] == '.' || (bytes == 1 && *text == '.') ||
        (bytes == 2 && !memcmp(text, "..", 2))) {
        return false;
    }
    for (size_t i = 0; i < bytes; i++) {
        if (((unsigned char)text[i] < 32 || (unsigned char)text[i] == 127) ||
            strchr("<>:\"/\\|?*", text[i])) {
            return false;
        }
    }
    const char *dot = memchr(text, '.', bytes);
    size_t stem = dot ? (size_t)(dot - text) : bytes;
    static const char *const reserved[] = {
        "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
        "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
        "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    for (int i = 0; i < (int)(sizeof reserved / sizeof *reserved); i++) {
        if (strlen(reserved[i]) == stem && !strncasecmp(text, reserved[i], stem)) {
            return false;
        }
    }
    return true;
}
static bool portable_path(const char *path)
{
    if (!path || !*path || strlen(path) > 1024) {
        return false;
    }
    int depth = 0;
    for (const char *p = path; *p;) {
        const char *slash = strchr(p, '/');
        size_t bytes = slash ? (size_t)(slash - p) : strlen(p);
        if (!portable_segment(p, bytes) || ++depth > 16) {
            return false;
        }
        if (!slash) {
            return true;
        }
        p = slash + 1;
    }
    return false;
}
static const char *string(yyjson_val *value)
{
    const char *text = yyjson_get_str(value);
    return text && strlen(text) == yyjson_get_len(value) ? text : NULL;
}
static bool object_keys(yyjson_val *value, const char *a, const char *b)
{
    if (!yyjson_is_obj(value) || yyjson_obj_size(value) != 2) {
        return false;
    }
    bool first = false, second = false;
    size_t index, count;
    yyjson_val *key, *child;
    yyjson_obj_foreach(value, index, count, key, child)
    {
        (void)child;
        const char *name = string(key);
        if (!name) {
            return false;
        }
        if (!strcmp(name, a) && !first) {
            first = true;
        } else if (!strcmp(name, b) && !second) {
            second = true;
        } else {
            return false;
        }
    }
    return first && second;
}
static yyjson_doc *validate(
    const char *text,
    const char **name,
    scaffold_file *files,
    int *file_count,
    char *error,
    size_t size
)
{
    if (!text || strnlen(text, WORKSPACE_SCAFFOLD_BYTES_MAX + 1) >
                     WORKSPACE_SCAFFOLD_BYTES_MAX) {
        fail(error, size, "Project scaffold exceeds16KiB");
        return NULL;
    }
    yyjson_doc *doc = yyjson_read(text, strlen(text), 0);
    yyjson_val *root = yyjson_doc_get_root(doc), *rows = yyjson_obj_get(root, "Files");
    *name = string(yyjson_obj_get(root, "Name"));
    bool ok = doc && object_keys(root, "Name", "Files") && *name &&
              strlen(*name) <= 127 && portable_segment(*name, strlen(*name)) &&
              yyjson_is_arr(rows) && yyjson_arr_size(rows) > 0 &&
              yyjson_arr_size(rows) <= WORKSPACE_SCAFFOLD_FILES_MAX;
    size_t total = 0, index, count;
    yyjson_val *row;
    *file_count = 0;
    if (ok) {
        yyjson_arr_foreach(rows, index, count, row)
        {
            const char *path = string(yyjson_obj_get(row, "Path")),
                       *body = string(yyjson_obj_get(row, "Text"));
            if (!object_keys(row, "Path", "Text") || !portable_path(path) || !body) {
                ok = false;
                break;
            }
            total += strlen(body);
            if (total > WORKSPACE_SCAFFOLD_BYTES_MAX) {
                ok = false;
                break;
            }
            for (int i = 0; i < *file_count; i++) {
                const char *other = files[i].path;
                size_t a = strlen(path), b = strlen(other), shorter = a < b ? a : b;
                if (!strncasecmp(path, other, shorter) &&
                    (a == b || path[shorter] == '/' || other[shorter] == '/')) {
                    ok = false;
                    break;
                }
            }
            if (!ok) {
                break;
            }
            files[(*file_count)++] = (scaffold_file){ path, body, strlen(body) };
        }
    }
    const char *manifest_text = NULL;
    for (int i = 0; ok && i < *file_count; i++) {
        if (!strcmp(files[i].path, "project.kdl")) {
            manifest_text = files[i].text;
        }
    }
    workspace_manifest manifest = { 0 };
    if (ok) {
        ok = manifest_text &&
             workspace_manifest_parse("/new-workspace", manifest_text, &manifest, error, size) &&
             !strcmp(manifest.name, *name);
    }
    bool start = false, content = false;
    for (int i = 0; ok && i < *file_count; i++) {
        start |= !strcmp(files[i].path, manifest.start_scene);
        size_t prefix = strlen(manifest.content);
        content |= !strncmp(files[i].path, manifest.content, prefix) &&
                   files[i].path[prefix] == '/';
    }
    ok = ok && start && content;
    workspace_manifest_free(&manifest);
    if (!ok) {
        yyjson_doc_free(doc);
        if (!size || !*error) {
            fail(error, size, "Use a portable name and distinct UTF-8 files, including a current project.kdl and its start scene");
        }
        return NULL;
    }
    return doc;
}
typedef struct
{
    char *path;
    rooted_file_info info;
} owned_path;
static bool same_identity(const rooted_file_info *a, const rooted_file_info *b)
{
    return a->device == b->device && a->identity[0] == b->identity[0] &&
           a->identity[1] == b->identity[1];
}
static bool remember(rooted_files *root, const char *path, owned_path **owned)
{
    rooted_file_info info;
    if (!rooted_files_stat(root, path, &info)) {
        return false;
    }
    apush(*owned, ((owned_path){ xstrdup(path), info }));
    return true;
}
static bool create_parents(rooted_files *root, const char *path, owned_path **owned)
{
    char prefix[1025];
    snprintf(prefix, sizeof prefix, "%s", path);
    for (char *slash = strchr(prefix, '/'); slash; slash = strchr(slash + 1, '/')) {
        *slash = 0;
        rooted_files *directory = rooted_files_directory_new(root, prefix);
        if (directory) {
            rooted_files_close(directory);
            if (!remember(root, prefix, owned)) {
                return false;
            }
        } else if (errno == EEXIST) {
            directory = rooted_files_directory(root, prefix, false);
            if (!directory) {
                return false;
            }
            rooted_files_close(directory);
        } else {
            return false;
        }
        *slash = '/';
    }
    return true;
}
static void remove_owned(rooted_files *root, const owned_path *owned, bool directory)
{
    rooted_file_info info;
    if (rooted_files_stat(root, owned->path, &info) &&
        same_identity(&info, &owned->info)) {
        if (directory) {
            rooted_files_remove_directory(root, owned->path);
        } else {
            rooted_files_remove(root, owned->path);
        }
    }
}
bool workspace_scaffold_create(const char *parent, const char *text, char **result, char *error, size_t size)
{
    *result = NULL;
    if (size) {
        *error = 0;
    }
    scaffold_file files[WORKSPACE_SCAFFOLD_FILES_MAX];
    int count;
    const char *name;
    yyjson_doc *doc = validate(text, &name, files, &count, error, size);
    if (!doc) {
        return false;
    }
    rooted_files *directory = parent ? rooted_files_open(parent) : NULL;
    if (!directory) {
        yyjson_doc_free(doc);
        return fail(error, size, "Choose an existing parent folder");
    }
    rooted_files *root = rooted_files_directory_new(directory, name);
    if (!root) {
        int code = errno;
        rooted_files_close(directory);
        yyjson_doc_free(doc);
        return fail(
            error,
            size,
            code == EEXIST ? "A project folder with this name already exists"
                           : "Could not create the project folder"
        );
    }
    rooted_file_info owned = { 0 }, current;
    bool owned_root = rooted_files_stat(directory, name, &owned);
    char *path = rooted_files_host_path(root);
    bool ok = owned_root && path && strlen(path) < 4096;
    owned_path *created_dirs = NULL, *created_files = NULL;
    for (int i = 0; ok && i < count; i++) {
        ok = create_parents(root, files[i].path, &created_dirs) &&
             rooted_files_write(
                 root,
                 files[i].path,
                 files[i].text,
                 files[i].bytes,
                 WORKSPACE_SCAFFOLD_BYTES_MAX,
                 false,
                 NULL,
                 0
             );
        if (ok) {
            ok = remember(root, files[i].path, &created_files);
        }
    }
    ok = ok && rooted_files_stat(directory, name, &current) &&
         same_identity(&owned, &current);
    if (ok) {
        chibi_project project = { 0 };
        ok = !chibi_project_read(&project, path, error, size);
        chibi_project_free(&project);
    }
    if (!ok) {
        for (int i = acount(created_files) - 1; i >= 0; i--) {
            remove_owned(root, &created_files[i], false);
        }
        for (int i = acount(created_dirs) - 1; i >= 0; i--) {
            remove_owned(root, &created_dirs[i], true);
        }
        bool removed = owned_root && rooted_files_stat(directory, name, &current) &&
                       same_identity(&owned, &current) &&
                       rooted_files_remove_directory(directory, name);
        if (size) {
            snprintf(
                error,
                size,
                removed ? "Project creation failed; its new files were removed" : "Project creation failed; inspect the incomplete folder: %s",
                path ? path : name
            );
        }
    }
    for (int i = 0; i < acount(created_dirs); i++) {
        free(created_dirs[i].path);
    }
    for (int i = 0; i < acount(created_files); i++) {
        free(created_files[i].path);
    }
    afree(created_dirs);
    afree(created_files);
    rooted_files_close(root);
    rooted_files_close(directory);
    yyjson_doc_free(doc);
    if (ok) {
        *result = path;
        return true;
    }
    free(path);
    return false;
}
