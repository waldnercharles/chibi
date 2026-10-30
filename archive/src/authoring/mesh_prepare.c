#include "authoring/mesh_prepare.h"

#include "runtime/component_catalog.h"
#include "runtime/geometry/mesh_prepared.h"
#include "support/xalloc.h"
#include "types/value_wire.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

/* Model export wrote .mesh.json files before meshes were indexed binaries. */
static bool retired_json(const char *source)
{
    size_t length = strlen(source);
    return length >= sizeof ".mesh.json" - 1 &&
           !strcmp(source + length - 10, ".mesh.json");
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
            snprintf(
                error,
                size,
                "Mesh %s must be a content/... .mesh path%s",
                source,
                retired_json(source) ? "; export the model again to replace "
                                       "the retired .mesh.json format."
                                     : "."
            );
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
    valid = length >= 0 && length <= EXECUTION_WIRE_MAX_BYTES &&
            !fseek(file, 0, SEEK_SET);
    unsigned char *bytes = valid ? xmalloc((size_t)length) : NULL;
    if (valid) {
        valid = fread(bytes, 1, (size_t)length, file) == (size_t)length &&
                fgetc(file) == EOF;
    }
    if (file) {
        valid = fclose(file) == 0 && valid;
    }
    if (!valid) {
        if (size) {
            snprintf(error, size, "Cannot read mesh %s (maximum %d bytes).", source, EXECUTION_WIRE_MAX_BYTES);
        }
        free(bytes);
        return NULL;
    }
    render_mesh_asset asset = { 0 };
    size_t budget = SIZE_MAX;
    char reason[256];
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

const char *authoring_component_mesh(const ecs_component_desc *desc, const void *data)
{
    const char *source = NULL;
    if (desc->id == COMPONENT_MeshAsset.id) {
        source = ((const MeshAsset *)data)->source;
    }
    if (desc->id == COMPONENT_Collider3D.id) {
        const Collider3D *collider = data;
        if (collider->shape == COLLIDER3D_HULL || collider->shape == COLLIDER3D_MESH) {
            source = collider->mesh;
        }
    }
    return source && source[0] ? source : NULL;
}
