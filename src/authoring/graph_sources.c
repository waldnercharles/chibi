#include "authoring/graph_sources.h"

#include "authoring/source_asset.h"
#include "capabilities/catalog.h"
#include "graph/document/serialize.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
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
    if (size < 0 || size > 512 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
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

/* Graph assets link against an event's payload; the event itself is a Chibi
 * record, so the linkable definition is made from it. */
behavior_asset *authoring_event_definition(const char *text)
{
    char error[160];
    av_doc *document = authoring_event_source_read(text, error, sizeof error);
    if (!document) {
        load_error("event", "parse", error);
        return NULL;
    }
    const av_value *root = av_doc_root(document), *variables = av_obj_get(root, "variables");
    behavior_asset *asset = behavior_asset_new(av_get_str(av_obj_get(root, "name")));
    asset->domain = BEHAVIOR_DOMAIN_EVENT_DEFINITION;
    const char *description = av_get_str(av_obj_get(root, "description"));
    if (description) {
        behavior_asset_set_description(asset, description);
    }
    for (size_t i = 0; i < av_arr_size(variables); i++) {
        const av_value *variable = av_arr_get(variables, i);
        const char *type_name = av_get_str(av_obj_get(variable, "type_name"));
        script_type type = script_builtin_type(type_name);
        bool builtin = !(type.flags & SCRIPT_TYPE_ERROR);
        pin_container container = av_get_str(av_obj_get(variable, "container")) ? PIN_CONTAINER_ARRAY
                                                                                : PIN_CONTAINER_SINGLE;
        behavior_asset_add_variable(
            asset,
            av_get_str(av_obj_get(variable, "name")),
            builtin ? type.storage : PIN_STRUCT,
            type.nominal ? "ecs.entity" : type_name,
            container,
            NULL,
            "Payload"
        );
    }
    av_doc_free(document);
    return asset;
}

bool authoring_graph_sources_load(const char *content_root, authoring_graph_source **out)
{
    assert(out && !*out);
    if (!content_root || !*content_root) {
        return false;
    }
    char **paths = NULL;
    size_t total_bytes = 0;
    const char *folders[] = { "shader-libraries", "events", "fields",
                              "materials",        "shapes", "models" };
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
            if (entry->d_name[0] == '.' ||
                !has_suffix(entry->d_name, f == 1 ? CODE_ASSET_EXTENSION : DECLARATIVE_ASSET_EXTENSION)) {
                continue;
            }
            size_t path_size = strlen(directory_path) + strlen(entry->d_name) + 2;
            char *path = xmalloc(path_size);
            snprintf(path, path_size, "%s/%s", directory_path, entry->d_name);
            struct stat info;
            if (stat(path, &info) == 0 && S_ISREG(info.st_mode)) {
                if (acount(paths) == NATIVE_GRAPH_DOCUMENTS_MAX ||
                    info.st_size < 0 || info.st_size > 512 * 1024 ||
                    (size_t)info.st_size > 32 * 1024 * 1024 - total_bytes) {
                    free(path);
                    closedir(directory);
                    free(directory_path);
                    for (int i = 0; i < acount(paths); i++) {
                        free(paths[i]);
                    }
                    afree(paths);
                    load_error(content_root, "limits", "Native graph documents exceed file/count/byte limits");
                    return false;
                }
                total_bytes += (size_t)info.st_size;
                apush(paths, path);
            } else {
                free(path);
            }
        }
        closedir(directory);
        free(directory_path);
    }
    if (acount(paths) > 1) {
        asort(paths, path_compare);
    }
    bool valid = true;
    authoring_graph_source *result = NULL;
    for (int i = 0; i < acount(paths); i++) {
        char *text = read_file(paths[i]);
        bool event = has_suffix(paths[i], CODE_ASSET_EXTENSION);
        behavior_asset *asset = !text ? NULL : event ? authoring_event_definition(text) : behavior_asset_load(text);
        if (!asset) {
            load_error(paths[i], "deserialize", text ? graph_last_error() : "Cannot read asset");
            valid = false;
        }
        free(text);
        apush(result, ((authoring_graph_source){ paths[i], asset }));
    }
    afree(paths);
    if (!valid) {
        authoring_graph_sources_free(result);
        return false;
    }
    *out = result;
    return true;
}
void authoring_graph_sources_free(authoring_graph_source *sources)
{
    for (int i = 0; i < acount(sources); i++) {
        behavior_asset_free(sources[i].asset);
        free(sources[i].path);
    }
    afree(sources);
}
