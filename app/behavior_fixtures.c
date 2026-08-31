#include "behavior_fixtures.h"

#include "../core/array.h"
#include "../core/serialize.h"
#include "../core/xalloc.h"

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

int app_behavior_fixtures_load(game_live_world *world, const char *content_root)
{
    if (!world || !content_root || !content_root[0]) {
        return -1;
    }
    size_t size = strlen(content_root) + strlen("/behaviors") + 1;
    char *directory_path = (char *)xmalloc(size);
    snprintf(directory_path, size, "%s/behaviors", content_root);
    DIR *directory = opendir(directory_path);
    if (!directory) {
        free(directory_path);
        return -1;
    }
    char **paths = NULL;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        size_t path_size = strlen(directory_path) + strlen(entry->d_name) + 2;
        char *path = (char *)xmalloc(path_size);
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
    qsort(paths, (size_t)acount(paths), sizeof *paths, path_compare);

    int loaded = 0;
    for (int i = 0; i < acount(paths); i++) {
        char *json = read_file(paths[i]);
        behavior_asset *asset = json ? behavior_asset_load_json(json) : NULL;
        free(json);
        if (asset && asset->name && !game_live_find_definition(world, asset->name)) {
            game_live_definition *definition = game_live_definition_new(asset->name, asset);
            if (definition) {
                asset = NULL;
            }
            if (definition && game_live_definition_set_path(definition, paths[i]) == 0 &&
                game_live_add_definition(world, definition) == 0) {
                loaded++;
            } else {
                game_live_definition_free(definition);
            }
        }
        behavior_asset_free(asset);
        free(paths[i]);
    }
    afree(paths);
    return loaded;
}
