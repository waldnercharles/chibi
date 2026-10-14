#include "authoring/workspace_file.h"

#include "support/rooted_files.h"
#include "support/xalloc.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct workspace_files
{
    rooted_files *root;
    unsigned sequence;
};
static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
workspace_files *workspace_files_new(const char *path, char *error, size_t size)
{
    rooted_files *root = rooted_files_open(path);
    if (!root) {
        fail(error, size, "Workspace file root is unavailable or is a link");
        return NULL;
    }
    workspace_files *files = xmalloc(sizeof *files);
    files->root = root;
    files->sequence = 0;
    if (size) {
        error[0] = 0;
    }
    return files;
}
void workspace_files_free(workspace_files *files)
{
    if (!files) {
        return;
    }
    rooted_files_close(files->root);
    free(files);
}
bool workspace_file_write_new(
    workspace_files *files,
    const char *path,
    const void *data,
    size_t bytes,
    char *error,
    size_t size
)
{
    if (!files || (!data && bytes) || bytes > WORKSPACE_FILE_BYTES_MAX || !path ||
        strlen(path) >= WORKSPACE_FILE_PATH_MAX || !rooted_files_path_valid(path)) {
        return fail(error, size, "File publication has invalid paths or exceeds its 16 MiB byte budget");
    }
    if (!rooted_files_write(files->root, path, data, bytes, WORKSPACE_FILE_BYTES_MAX, false, NULL, 0)) {
        return fail(error, size, "Could not publish the new file; the destination may already exist");
    }
    if (size) {
        error[0] = 0;
    }
    return true;
}
static bool bundle_path(const char *path)
{
    if (!rooted_files_path_valid(path) || !path || !*path || *path == '/' ||
        strlen(path) >= WORKSPACE_FILE_PATH_MAX || strchr(path, '\\')) {
        return false;
    }
    int depth = 0;
    const char *part = path;
    for (;;) {
        const char *slash = strchr(part, '/');
        size_t bytes = slash ? (size_t)(slash - part) : strlen(part);
        if (!bytes || bytes > 255 || (bytes == 1 && part[0] == '.') ||
            (bytes == 2 && !memcmp(part, "..", 2)) || ++depth > 16) {
            return false;
        }
        if (!slash) {
            return true;
        }
        part = slash + 1;
    }
}
static bool bundle_preflight(const workspace_file_entry *entries, int count, char *error, size_t size)
{
    if (!entries || count < 1 || count > WORKSPACE_BUNDLE_FILES_MAX) {
        return fail(error, size, "A bundle needs 1..128 files");
    }
    size_t total = 0;
    for (int i = 0; i < count; i++) {
        if (!bundle_path(entries[i].path) || (!entries[i].data && entries[i].bytes) ||
            entries[i].bytes > WORKSPACE_FILE_BYTES_MAX ||
            entries[i].bytes > WORKSPACE_BUNDLE_BYTES_MAX - total) {
            return fail(error, size, "Bundle paths or byte budgets are invalid");
        }
        total += entries[i].bytes;
        size_t length = strlen(entries[i].path);
        for (int j = 0; j < i; j++) {
            size_t previous = strlen(entries[j].path);
            if (!strcmp(entries[i].path, entries[j].path) ||
                (previous < length && !memcmp(entries[i].path, entries[j].path, previous) &&
                 entries[i].path[previous] == '/') ||
                (length < previous && !memcmp(entries[j].path, entries[i].path, length) &&
                 entries[j].path[length] == '/')) {
                return fail(error, size, "Bundle files cannot duplicate or contain another file path");
            }
        }
    }
    return true;
}
bool workspace_file_bundle_write_new(
    workspace_files *files,
    const char *directory,
    const workspace_file_entry *entries,
    int count,
    char *error,
    size_t size
)
{
    if (!files || !bundle_path(directory)) {
        return fail(error, size, "Choose an exclusive workspace-relative bundle directory");
    }
    if (!bundle_preflight(entries, count, error, size)) {
        return false;
    }
    rooted_files *bundle = rooted_files_directory_new(files->root, directory);
    if (!bundle) {
        return fail(error, size, "Bundle directory already exists or cannot be created");
    }
    bool ok = true;
    for (int i = 0; ok && i < count; i++) {
        const char *slash = strrchr(entries[i].path, '/');
        if (slash) {
            char parent[WORKSPACE_FILE_PATH_MAX];
            size_t length = (size_t)(slash - entries[i].path);
            memcpy(parent, entries[i].path, length);
            parent[length] = 0;
            rooted_files *nested = rooted_files_directory(bundle, parent, true);
            if (!nested) {
                ok = false;
            } else {
                rooted_files_close(nested);
            }
        }
        if (ok) {
            ok = rooted_files_write(
                bundle,
                entries[i].path,
                entries[i].data,
                entries[i].bytes,
                WORKSPACE_FILE_BYTES_MAX,
                false,
                NULL,
                0
            );
        }
    }
    rooted_files_close(bundle);
    if (!ok) {
        return fail(error, size, "Bundle publication failed; the new directory is incomplete and was not replaced");
    }
    if (size) {
        error[0] = 0;
    }
    return true;
}

bool workspace_file_bundle_write_atomic(
    workspace_files *files,
    const char *directory,
    const workspace_file_entry *entries,
    int count,
    char *error,
    size_t size
)
{
    if (!files || !bundle_path(directory) ||
        strlen(directory) > WORKSPACE_FILE_PATH_MAX - 48 ||
        !bundle_preflight(entries, count, error, size)) {
        return fail(error, size, "Atomic bundle path or entries are invalid");
    }
    for (int i = 0; i < count; i++) {
        if (strchr(entries[i].path, '/')) {
            return fail(error, size, "Atomic bundles require flat file names");
        }
    }
    rooted_file_info existing;
    if (rooted_files_stat(files->root, directory, &existing) || errno != ENOENT) {
        return fail(error, size, "Atomic bundle destination exists or is unavailable");
    }
    char stage[WORKSPACE_FILE_PATH_MAX];
    rooted_files *bundle = NULL;
    for (int i = 0; i < 16 && !bundle; i++) {
        if (files->sequence == UINT_MAX) {
            return fail(error, size, "Bundle staging generations exhausted");
        }
        snprintf(stage, sizeof stage, "%s.stage-%u", directory, ++files->sequence);
        bundle = rooted_files_directory_new(files->root, stage);
        if (!bundle && errno != EEXIST) {
            break;
        }
    }
    if (!bundle) {
        return fail(error, size, "Atomic bundle stage could not be reserved");
    }
    bool ok = true;
    for (int i = 0; ok && i < count; i++) {
        ok = rooted_files_write(
            bundle,
            entries[i].path,
            entries[i].data,
            entries[i].bytes,
            WORKSPACE_FILE_BYTES_MAX,
            false,
            NULL,
            0
        );
    }
    if (ok) {
        ok = rooted_files_move_directory(files->root, stage, directory);
    }
    if (!ok) {
        bool clean = true;
        for (int i = 0; i < count; i++) {
            if (!rooted_files_remove(bundle, entries[i].path) && errno != ENOENT) {
                clean = false;
            }
        }
        rooted_files_close(bundle);
        clean = rooted_files_remove_directory(files->root, stage) && clean;
        if (!clean) {
            if (size) {
                snprintf(error, size, "Bundle refused; private stage cleanup failed: %s", stage);
            }
            return false;
        }
        return fail(error, size, "Bundle refused before exclusive directory publication");
    }
    rooted_files_close(bundle);
    if (size) {
        error[0] = 0;
    }
    return true;
}
