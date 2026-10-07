#include "assets/behavior_loader.h"

#include "assets/code_catalog.h"
#include "graph/document/serialize.h"
#include "render/shader_catalog.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void load_error(const char *path, const char *stage, const char *message)
{
    fprintf(stderr, "Could not load graph asset %s (%s): %s\n", path, stage, message);
}

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) {
        load_error(path, "read", strerror(errno));
        if (file) {
            fclose(file);
        }
        return NULL;
    }
    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        load_error(path, "read", strerror(errno));
        fclose(file);
        return NULL;
    }
    char *text = (char *)xmalloc((size_t)size + 1);
    size_t read = fread(text, 1, (size_t)size, file);
    fclose(file);
    if (read != (size_t)size) {
        load_error(path, "read", "incomplete file read");
        free(text);
        return NULL;
    }
    text[read] = '\0';
    return text;
}

static int path_compare(const void *left, const void *right)
{
    const char *const *a = (const char *const *)left;
    const char *const *b = (const char *const *)right;
    return strcmp(*a, *b);
}

static bool has_suffix(const char *text, const char *suffix)
{
    size_t text_length = strlen(text), suffix_length = strlen(suffix);
    return text_length >= suffix_length &&
           !strcmp(text + text_length - suffix_length, suffix);
}

typedef struct
{
    char *name, *path, *text;
} pending_system;

int app_behavior_fixtures_load(game_live_world *world, game_prototype_preview *preview, const char *content_root)
{
    if (!world || !content_root || !content_root[0]) {
        return -1;
    }
    char **paths = NULL;
    const char *folders[] = { "libraries", "shader-libraries", "events",
                              "systems",   "fields",           "materials",
                              "shapes",    "models",           "behaviors" };
    const char *extensions[] = {
        CODE_ASSET_EXTENSION,        DECLARATIVE_ASSET_EXTENSION,
        DECLARATIVE_ASSET_EXTENSION, CODE_ASSET_EXTENSION,
        DECLARATIVE_ASSET_EXTENSION, DECLARATIVE_ASSET_EXTENSION,
        DECLARATIVE_ASSET_EXTENSION, DECLARATIVE_ASSET_EXTENSION,
        CODE_ASSET_EXTENSION
    };
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
            if (entry->d_name[0] == '.' || !has_suffix(entry->d_name, extensions[f])) {
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
    if (acount(paths) > 1) {
        qsort(paths, (size_t)acount(paths), sizeof *paths, path_compare);
    }
    code_catalog *catalog = code_catalog_load(content_root);
    for (int i = 0; i < code_catalog_error_count(catalog); i++) {
        const code_catalog_error *error = code_catalog_error_at(catalog, i);
        load_error(error->path, "declarations", error->message);
    }
    pending_system *systems = NULL;
    game_live_definition **pending = NULL;
    int loaded = 0;
    for (int i = 0; i < acount(paths); i++) {
        char *text = read_file(paths[i]);
        char error[256];
        behavior_asset *asset = text
                                  ? code_catalog_load_path(catalog, text, paths[i], error, sizeof error)
                                  : NULL;
        if (text && !asset) {
            load_error(paths[i], "deserialize", error);
        }
        if (text && asset && strstr(paths[i], "/systems/")) {
            apush(systems, ((pending_system){ xstrdup(asset->name), paths[i], text }));
            behavior_asset_free(asset);
            continue;
        }
        free(text);
        game_live_definition *definition = asset
                                             ? game_live_definition_new(asset->name, asset)
                                             : NULL;
        if (definition) {
            game_live_definition_set_path(definition, paths[i]);
            apush(pending, definition);
        } else {
            if (asset) {
                load_error(paths[i], "create editable definition", graph_last_error());
            }
            behavior_asset_free(asset);
        }
        free(paths[i]);
    }
    afree(paths);
    const behavior_asset **models = NULL;
    for (int i = 0; i < acount(pending); i++) {
        if (pending[i]->working->domain == BEHAVIOR_DOMAIN_MODEL) {
            apush(models, pending[i]->working);
        }
    }
    bool models_ready = false;
    for (int pass = 0; pass < 4; pass++) {
        if (pass == 2) {
            shader_error model_error = { 0 };
            models_ready = !acount(models) ||
                           render_shader_models_upsert(models, acount(models), &model_error);
            if (!models_ready) {
                load_error(content_root, "model dependencies", model_error.message);
            }
        }
        for (int i = 0; i < acount(pending); i++) {
            game_live_definition *definition = pending[i];
            if (!definition) {
                continue;
            }
            behavior_domain domain = definition->working->domain;
            if (domain == BEHAVIOR_DOMAIN_MODEL && !models_ready) {
                continue;
            }
            int stage = domain == BEHAVIOR_DOMAIN_EVENT ? 3
                      : domain == BEHAVIOR_DOMAIN_MODEL ? 2
                      : (domain == BEHAVIOR_DOMAIN_MATERIAL ||
                         domain == BEHAVIOR_DOMAIN_SHAPE ||
                         domain == BEHAVIOR_DOMAIN_FIELD)
                          ? 1
                          : 0;
            if (stage != pass) {
                continue;
            }
            if (game_live_add_definition(world, definition) != 0) {
                const compile_diagnostic *error = compile_report_first_error(
                    &definition->diagnostics
                );
                load_error(definition->path, "register", error ? error->message : "definition rejected");
                continue;
            }
            pending[i] = NULL;
            if (preview && game_prototype_preview_register_behavior(
                               preview,
                               definition->name,
                               definition->working
                           ) != 0) {
                load_error(definition->path, "preview", "registration rejected; see preview diagnostic above");
                game_live_remove_definition(world, definition->name);
                continue;
            }
            loaded++;
        }
    }
    for (int i = 0; i < acount(systems); i++) {
        pending_system *system = &systems[i];
        char error[256];
        if (game_live_register_system(world, system->name, system->text, error, sizeof error) ||
            (preview && game_prototype_preview_register_system(
                            preview,
                            system->name,
                            system->text,
                            error,
                            sizeof error
                        ))) {
            load_error(system->path, "register system", error);
        } else {
            loaded++;
        }
        free(system->name);
        free(system->path);
        free(system->text);
    }
    afree(systems);
    code_catalog_free(catalog);
    afree(models);
    for (int i = 0; i < acount(pending); i++) {
        game_live_definition_free(pending[i]);
    }
    afree(pending);
    return loaded;
}
