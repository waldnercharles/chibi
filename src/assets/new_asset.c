#include "assets/new_asset.h"

#include "assets/prototype.h"
#include "assets/scene_document.h"
#include "authoring/source_format.h"
#include "authoring/source_literal.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/modeling/model_compile.h"
#include "graph/shader/shader_graph.h"
#include "runtime/component_catalog.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum
{
    SHAPE_HALF_EXTENT = 32,
};

static const char *const FOLDERS[NEW_ASSET_KIND_COUNT] = {
    [NEW_ASSET_SCENE] = "scenes",
    [NEW_ASSET_PROTOTYPE] = "prototypes",
    [NEW_ASSET_RELATIONSHIP] = "relationships",
    [NEW_ASSET_BEHAVIOR] = "behaviors",
    [NEW_ASSET_EVENT] = "events",
    [NEW_ASSET_SYSTEM] = "systems",
    [NEW_ASSET_LIBRARY] = "libraries",
    [NEW_ASSET_MATERIAL] = "materials",
    [NEW_ASSET_SHAPE] = "shapes",
    [NEW_ASSET_FIELD] = "fields",
    [NEW_ASSET_SHADER_LIBRARY] = "shader-libraries",
    [NEW_ASSET_MODEL] = "models",
};

const char *new_asset_folder(new_asset_kind kind)
{
    return FOLDERS[kind];
}

const char *new_asset_extension(new_asset_kind kind)
{
    assert(kind >= 0 && kind < NEW_ASSET_KIND_COUNT);
    switch (kind) {
        case NEW_ASSET_BEHAVIOR:
        case NEW_ASSET_LIBRARY:
        case NEW_ASSET_EVENT:
        case NEW_ASSET_PROTOTYPE:
        case NEW_ASSET_RELATIONSHIP:
        case NEW_ASSET_SYSTEM: return CODE_ASSET_EXTENSION;
        default: return DECLARATIVE_ASSET_EXTENSION;
    }
}

bool new_asset_name_valid(const char *name, char *error, size_t size)
{
    bool valid = isalpha((unsigned char)name[0]) && strlen(name) <= NEW_ASSET_NAME_MAX;
    for (const char *c = name; valid && *c; c++) {
        valid = isalnum((unsigned char)*c) || *c == '_' || *c == '-';
    }
    if (!valid) {
        snprintf(error, size, name[0] ? "Start with a letter; use letters, digits, - and _" : "Enter a name");
    }
    return valid;
}

behavior_asset *new_asset_graph(behavior_domain domain, const char *name)
{
    behavior_asset *asset = behavior_asset_new(name);
    asset->domain = domain;
    node *output = NULL;
    switch (domain) {
        case BEHAVIOR_DOMAIN_MATERIAL:
            output = shader_graph_add_output(asset->graph, false);
            break;
        case BEHAVIOR_DOMAIN_SHAPE:
            asset->bounds[0] = asset->bounds[1] = SHAPE_HALF_EXTENT;
            output = shader_graph_add_output(asset->graph, true);
            break;
        case BEHAVIOR_DOMAIN_MODEL: {
            output = model_graph_add_output(asset->graph);
            node *sphere = graph_add_node(asset->graph, &NODE_ModelSphere);
            sphere->placed = false;
            int connected = graph_connect_data(asset->graph, sphere, 0, output, 0);
            assert(connected == 0);
            (void)connected;
            break;
        }
        case BEHAVIOR_DOMAIN_FIELD:
            output = graph_add_node(asset->graph, registry_node_by_name("Field Output"));
            break;
        case BEHAVIOR_DOMAIN_EVENT:
        case BEHAVIOR_DOMAIN_LIBRARY:
        case BEHAVIOR_DOMAIN_EVENT_DEFINITION:
        case BEHAVIOR_DOMAIN_SHADER_LIBRARY: return asset;
    }
    /* Automatic: the editor arranges it, and the file holds no position. */
    output->placed = false;
    return asset;
}

static char *graph_text(behavior_domain domain, const char *name)
{
    behavior_asset *asset = new_asset_graph(domain, name);
    char *text = behavior_asset_save(asset);
    behavior_asset_free(asset);
    return text;
}

static char *source_text(const char *kind, const char *name)
{
    char error[160];
    char *label = authoring_source_identifier(name, error, sizeof error);
    if (!label || strcmp(label, name) || strchr(label, '.')) {
        free(label);
        return NULL;
    }
    size_t bytes = strlen(kind) + strlen(label) + 16;
    char *raw = xmalloc(bytes);
    snprintf(raw, bytes, "%s :: %s%s {\n}\n", label, kind, !strcmp(kind, "system") ? "()" : "");
    script_syntax *syntax = script_parse(raw);
    char *text = authoring_source_format(syntax, error, sizeof error);
    script_syntax_free(syntax);
    free(label);
    free(raw);
    return text;
}

char *new_asset_text(new_asset_kind kind, const char *name)
{
    switch (kind) {
        case NEW_ASSET_SCENE: {
            scene_document *scene = scene_document_new(name);
            char *text = scene_document_save(
                scene,
                (ecs_component_catalog){ REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT }
            );
            scene_document_free(scene);
            return text;
        }
        case NEW_ASSET_PROTOTYPE: return source_text("prototype", name);
        case NEW_ASSET_RELATIONSHIP: return source_text("relationship", name);
        case NEW_ASSET_SYSTEM: return source_text("system", name);
        case NEW_ASSET_BEHAVIOR: return source_text("behavior", name);
        case NEW_ASSET_EVENT: return source_text("struct", name);
        case NEW_ASSET_LIBRARY: return source_text("library", name);
        case NEW_ASSET_MATERIAL:
            return graph_text(BEHAVIOR_DOMAIN_MATERIAL, name);
        case NEW_ASSET_SHAPE: return graph_text(BEHAVIOR_DOMAIN_SHAPE, name);
        case NEW_ASSET_FIELD: return graph_text(BEHAVIOR_DOMAIN_FIELD, name);
        case NEW_ASSET_SHADER_LIBRARY:
            return graph_text(BEHAVIOR_DOMAIN_SHADER_LIBRARY, name);
        case NEW_ASSET_MODEL: return graph_text(BEHAVIOR_DOMAIN_MODEL, name);
        case NEW_ASSET_KIND_COUNT: break;
    }
    assert(!"a new asset kind");
    return NULL;
}

/* Like the catalog, writes never follow a link out of the project. */
bool new_asset_real_folder(const char *path, char *error, size_t size)
{
    struct stat info;
    if (lstat(path, &info) != 0) {
        snprintf(error, size, "Cannot inspect %s: %s", path, strerror(errno));
        return false;
    }
    if (S_ISDIR(info.st_mode)) {
        return true;
    }
    snprintf(error, size, "%s is not a folder", path);
    errno = ENOTDIR;
    return false;
}

char *new_asset_create(const char *content, new_asset_kind kind, const char *name, char *error, size_t size)
{
    if (!new_asset_name_valid(name, error, size)) {
        errno = EINVAL;
        return NULL;
    }
    if (!new_asset_real_folder(content, error, size)) {
        return NULL;
    }
    size_t bytes = strlen(content) + strlen("shader-libraries") + strlen(name) + 16;
    char *path = xmalloc(bytes);
    snprintf(path, bytes, "%s/%s", content, FOLDERS[kind]);
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        snprintf(error, size, "Cannot create %s: %s", path, strerror(errno));
        free(path);
        return NULL;
    }
    if (!new_asset_real_folder(path, error, size)) {
        free(path);
        return NULL;
    }
    errno = 0;
    /* Models share the shapes namespace. Prototypes also admit KDL, so a
     * new source prototype must not duplicate its existing KDL asset. */
    int rival = kind == NEW_ASSET_MODEL     ? NEW_ASSET_SHAPE
              : kind == NEW_ASSET_SHAPE     ? NEW_ASSET_MODEL
              : kind == NEW_ASSET_PROTOTYPE ? NEW_ASSET_PROTOTYPE
                                            : -1;
    if (rival >= 0) {
        snprintf(path, bytes, "%s/%s/%s%s", content, FOLDERS[rival], name, DECLARATIVE_ASSET_EXTENSION);
        if (access(path, F_OK) == 0) {
            snprintf(error, size, "%s%s already exists", name, DECLARATIVE_ASSET_EXTENSION);
            errno = EEXIST;
            free(path);
            return NULL;
        }
        if (errno != ENOENT && errno != ENOTDIR) {
            snprintf(error, size, "Cannot check %s: %s", path, strerror(errno));
            free(path);
            return NULL;
        }
    }
    snprintf(path, bytes, "%s/%s/%s%s", content, FOLDERS[kind], name, new_asset_extension(kind));
    char *text = new_asset_text(kind, name);
    if (!text) {
        snprintf(error, size, "Cannot create this asset with the source identifier '%s'", name);
        errno = EINVAL;
        free(path);
        return NULL;
    }
    /* "x" refuses a file that is already there, so nothing is overwritten. */
    FILE *file = fopen(path, "wbx");
    if (!file) {
        snprintf(
            error,
            size,
            errno == EEXIST ? "%s%s already exists" : "Cannot create %s%s",
            name,
            new_asset_extension(kind)
        );
        free(text);
        free(path);
        return NULL;
    }
    size_t length = strlen(text);
    bool written = fwrite(text, 1, length, file) == length;
    written = fclose(file) == 0 && written;
    free(text);
    if (!written) {
        snprintf(error, size, "Cannot finish writing %s", path);
        unlink(path);
        free(path);
        return NULL;
    }
    return path;
}
