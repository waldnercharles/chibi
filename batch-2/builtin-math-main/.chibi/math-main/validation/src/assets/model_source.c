#include "assets/model_source.h"

#include "graph/document/library.h"
#include "graph/document/problems.h"
#include "graph/document/serialize.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/file_io.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int compare_paths(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static bool load_folder(
    const char *root,
    const char *folder,
    behavior_domain domain,
    model_saved_source **saved,
    char *error,
    size_t size
)
{
    size_t length = strlen(root) + strlen(folder) + 2;
    char *directory = xmalloc(length);
    snprintf(directory, length, "%s/%s", root, folder);
    DIR *dir = opendir(directory);
    if (!dir) {
        bool missing = errno == ENOENT;
        if (!missing) {
            snprintf(error, size, "%s: %s", directory, strerror(errno));
        }
        free(directory);
        return missing;
    }
    char **paths = NULL;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        size_t name_length = strlen(entry->d_name);
        if (entry->d_name[0] == '.' ||
            name_length < sizeof DECLARATIVE_ASSET_EXTENSION - 1 ||
            strcmp(entry->d_name + name_length - (sizeof DECLARATIVE_ASSET_EXTENSION - 1), DECLARATIVE_ASSET_EXTENSION)) {
            continue;
        }
        size_t path_length = strlen(directory) + name_length + 2;
        char *path = xmalloc(path_length);
        snprintf(path, path_length, "%s/%s", directory, entry->d_name);
        apush(paths, path);
    }
    closedir(dir);
    free(directory);
    if (acount(paths) > 1) {
        asort(paths, compare_paths);
    }
    bool valid = true;
    for (int i = 0; i < acount(paths); i++) {
        if (valid) {
            char *text = file_read_text(paths[i]);
            behavior_asset *asset = text ? behavior_asset_load(text) : NULL;
            if (!asset || asset->domain != domain) {
                snprintf(
                    error,
                    size,
                    "%s: %s",
                    paths[i],
                    !text    ? "Could not read saved source"
                    : !asset ? graph_last_error()
                             : "Asset domain does not match its folder"
                );
                behavior_asset_free(asset);
                valid = false;
            } else {
                apush(*saved, ((model_saved_source){ .asset = asset, .path = paths[i], .text = text }));
                text = NULL;
                paths[i] = NULL;
            }
            free(text);
        }
        free(paths[i]);
    }
    afree(paths);
    return valid;
}

bool model_source_load(const char *root, model_source_catalog *s, char *error, size_t size)
{
    *s = (model_source_catalog){ 0 };
    bool valid = load_folder(root, "models", BEHAVIOR_DOMAIN_MODEL, &s->saved, error, size);
    s->model_count = acount(s->saved);
    if (valid && s->model_count) {
        valid = load_folder(root, "shader-libraries", BEHAVIOR_DOMAIN_SHADER_LIBRARY, &s->saved, error, size) &&
                load_folder(root, "materials", BEHAVIOR_DOMAIN_MATERIAL, &s->saved, error, size) &&
                load_folder(root, "shapes", BEHAVIOR_DOMAIN_SHAPE, &s->saved, error, size);
    }
    for (int i = 0; valid && i < acount(s->saved); i++) {
        for (int j = i + 1; j < acount(s->saved); j++) {
            const behavior_asset *a = s->saved[i].asset, *b = s->saved[j].asset;
            bool shape_pair = (a->domain == BEHAVIOR_DOMAIN_MODEL ||
                               a->domain == BEHAVIOR_DOMAIN_SHAPE) &&
                              (b->domain == BEHAVIOR_DOMAIN_MODEL ||
                               b->domain == BEHAVIOR_DOMAIN_SHAPE);
            if (!strcmp(a->name, b->name) && (a->domain == b->domain || shape_pair)) {
                snprintf(
                    error,
                    size,
                    "Ambiguous saved asset '%s': %s and %s",
                    a->name,
                    s->saved[i].path,
                    s->saved[j].path
                );
                valid = false;
                break;
            }
        }
    }
    for (int i = 0; i < acount(s->saved); i++) {
        apush(s->catalog, s->saved[i].asset);
    }
    for (int i = 0; valid && i < acount(s->saved); i++) {
        if (s->saved[i].asset->domain != BEHAVIOR_DOMAIN_MATERIAL) {
            continue;
        }
        behavior_asset *linked = behavior_asset_link_libraries(
            s->saved[i].asset,
            s->catalog,
            acount(s->catalog),
            error,
            size
        );
        if (!linked) {
            valid = false;
            break;
        }
        apush(s->linked, linked);
        s->catalog[i] = linked;
    }
    return valid;
}
void model_source_free(model_source_catalog *s)
{
    afree(s->catalog);
    for (int i = 0; i < acount(s->linked); i++) {
        behavior_asset_free(s->linked[i]);
    }
    afree(s->linked);
    for (int i = 0; i < acount(s->saved); i++) {
        behavior_asset_free(s->saved[i].asset);
        free(s->saved[i].path);
        free(s->saved[i].text);
    }
    afree(s->saved);
    *s = (model_source_catalog){ 0 };
}
