#include "assets/behavior_loader.h"

#include "graph/document/serialize.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) {
        if (file) {
            fclose(file);
        }
        return NULL;
    }
    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char *json = (char *)xmalloc((size_t)size + 1);
    size_t read = fread(json, 1, (size_t)size, file);
    fclose(file);
    if (read != (size_t)size) {
        free(json);
        return NULL;
    }
    json[read] = '\0';
    return json;
}

static int path_compare(const void *left, const void *right)
{
    const char *const *a = (const char *const *)left;
    const char *const *b = (const char *const *)right;
    return strcmp(*a, *b);
}

int app_behavior_fixtures_load(game_live_world *world, game_prototype_preview *preview, const char *content_root)
{
    if (!world || !content_root || !content_root[0]) {
        return -1;
    }
    char **paths = NULL;
    const char *folders[] = { "libraries", "shader-libraries", "events",
                              "materials", "shapes",           "behaviors" };
    for (int f = 0; f < (int)(sizeof folders / sizeof *folders); f++) {
        size_t size = strlen(content_root) + strlen(folders[f]) + 2;
        char *directory_path = xmalloc(size);
        snprintf(directory_path, size, "%s/%s", content_root, folders[f]);
        DIR *directory = opendir(directory_path);
        if (!directory) {
            free(directory_path);
            continue;
        }
        struct dirent *entry;
        while ((entry = readdir(directory))) {
            if (entry->d_name[0] == '.') {
                continue;
            }
            size_t path_size = strlen(directory_path) + strlen(entry->d_name) + 2;
            char *path = xmalloc(path_size);
            snprintf(path, path_size, "%s/%s", directory_path, entry->d_name);
            struct stat info;
            if (stat(path, &info) == 0 && S_ISREG(info.st_mode)) {
                apush(paths, path);
            } else {
                free(path);
            }
        }
        closedir(directory);
        free(directory_path);
    }
    qsort(paths, (size_t)acount(paths), sizeof *paths, path_compare);
    game_live_definition **pending = NULL;
    for (int i = 0; i < acount(paths); i++) {
        char *json = read_file(paths[i]);
        behavior_asset *asset = json ? behavior_asset_load_json(json) : NULL;
        free(json);
        game_live_definition *definition = asset
                                             ? game_live_definition_new(asset->name, asset)
                                             : NULL;
        if (definition) {
            game_live_definition_set_path(definition, paths[i]);
            apush(pending, definition);
        } else {
            behavior_asset_free(asset);
        }
        free(paths[i]);
    }
    afree(paths);
    int loaded = 0;
    for (int pass = 0; pass < 3; pass++) {
        for (int i = 0; i < acount(pending); i++) {
            game_live_definition *definition = pending[i];
            if (!definition) {
                continue;
            }
            behavior_domain domain = definition->working->domain;
            int stage = domain == BEHAVIOR_DOMAIN_EVENT ? 2
                      : (domain == BEHAVIOR_DOMAIN_MATERIAL || domain == BEHAVIOR_DOMAIN_SHAPE)
                          ? 1
                          : 0;
            if (stage != pass) {
                continue;
            }
            if (game_live_add_definition(world, definition) != 0) {
                continue;
            }
            pending[i] = NULL;
            if (preview && game_prototype_preview_register_behavior(
                               preview,
                               definition->name,
                               definition->working
                           ) != 0) {
                game_live_remove_definition(world, definition->name);
                continue;
            }
            loaded++;
        }
    }
    for (int i = 0; i < acount(pending); i++) {
        game_live_definition_free(pending[i]);
    }
    afree(pending);
    return loaded;
}
