#include "authoring/mesh_prepare.h"

#include "runtime/geometry/mesh_prepared.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define MESH_FILE_LIMIT (64 * 1024 * 1024)

static char *content_root;

bool authoring_mesh_mount_content(const char *root, char *error, size_t size)
{
    struct stat info;
    if (!root || stat(root, &info) || !S_ISDIR(info.st_mode)) {
        if (size) {
            snprintf(error, size, "Mesh content root is not a directory");
        }
        return false;
    }
    free(content_root);
    content_root = xstrdup(root);
    return true;
}

void authoring_mesh_unmount_content()
{
    free(content_root);
    content_root = NULL;
}

static bool mesh_path(const char *source)
{
    size_t length = strlen(source);
    return length > sizeof "content/.mesh" - 1 && !strncmp(source, "content/", 8) &&
           !strcmp(source + length - 5, ".mesh") && !strstr(source, "..") &&
           !strchr(source, '\\');
}

const render_mesh_asset *authoring_mesh_prepare(const char *source, char *error, size_t size)
{
    assert(source);
    const render_mesh_asset *resident = render_mesh_asset_load(source, error, size);
    if (resident) {
        return resident;
    }
    if (!mesh_path(source)) {
        if (size) {
            snprintf(error, size, "Mesh %s must be a content/... .mesh path.", source);
        }
        return NULL;
    }
    if (!content_root) {
        if (size) {
            snprintf(error, size, "Mesh content root is not mounted");
        }
        return NULL;
    }
    size_t path_size = strlen(content_root) + strlen(source + 8) + 2;
    char *path = xmalloc(path_size);
    snprintf(path, path_size, "%s/%s", content_root, source + 8);
    FILE *file = fopen(path, "rb");
    free(path);
    bool valid = file && !fseek(file, 0, SEEK_END);
    long length = valid ? ftell(file) : -1;
    valid = length >= 0 && length <= MESH_FILE_LIMIT && !fseek(file, 0, SEEK_SET);
    unsigned char *bytes = valid ? xmalloc((size_t)length + 1) : NULL;
    if (valid) {
        valid = fread(bytes, 1, (size_t)length, file) == (size_t)length &&
                fgetc(file) == EOF;
    }
    if (file) {
        valid = fclose(file) == 0 && valid;
    }
    if (!valid) {
        if (size) {
            snprintf(error, size, "Cannot read mesh %s (maximum %d bytes).", source, MESH_FILE_LIMIT);
        }
        free(bytes);
        return NULL;
    }
    render_mesh_asset asset = { 0 };
    size_t budget = SIZE_MAX;
    char reason[128];
    valid = render_mesh_asset_prepared_read_budget(bytes, (size_t)length, &budget, &asset, reason, sizeof reason);
    free(bytes);
    if (!valid) {
        if (size) {
            snprintf(error, size, "Mesh %s is not a valid mesh file: %s.", source, reason);
        }
        return NULL;
    }
    render_mesh_asset_publish(source, &asset);
    return render_mesh_asset_load(source, error, size);
}
