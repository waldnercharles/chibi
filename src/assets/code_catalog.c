#include "assets/code_catalog.h"

#include "assets/project.h"
#include "graph/document/serialize.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/text_file.h"
#include "support/xalloc.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct code_catalog
{
    code_source_context context;
    const behavior_asset **libraries, **events;
    code_catalog_error *errors;
    code_reference *references;
    code_prototype *prototypes;
    bool borrowed;
};

static void rejected(code_catalog *catalog, const char *path, const char *message)
{
    apush(catalog->errors, ((code_catalog_error){ xstrdup(path), xstrdup(message) }));
}

static int path_compare(const void *left, const void *right)
{
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

static char **paths(const char *root, const char *folder, const char *extension)
{
    char **result = NULL;
    size_t size = strlen(root) + strlen(folder) + 2;
    char *directory_path = xmalloc(size);
    snprintf(directory_path, size, "%s/%s", root, folder);
    DIR *directory = opendir(directory_path);
    if (!directory) {
        free(directory_path);
        return NULL;
    }
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        size_t length = strlen(entry->d_name), suffix = strlen(extension);
        if (entry->d_name[0] == '.' || length <= suffix ||
            strcmp(entry->d_name + length - suffix, extension)) {
            continue;
        }
        size_t path_size = strlen(directory_path) + length + 2;
        char *path = xmalloc(path_size);
        snprintf(path, path_size, "%s/%s", directory_path, entry->d_name);
        struct stat info;
        if (!stat(path, &info) && S_ISREG(info.st_mode)) {
            apush(result, path);
        } else {
            free(path);
        }
    }
    closedir(directory);
    free(directory_path);
    if (acount(result) > 1) {
        qsort(result, (size_t)acount(result), sizeof *result, path_compare);
    }
    return result;
}

behavior_asset *code_catalog_load_behavior(
    const code_catalog *catalog,
    const char *text,
    char *error,
    size_t size
)
{
    return code_catalog_load_with_context(code_catalog_context(catalog), text, error, size);
}

behavior_asset *code_catalog_load_with_context(
    const code_source_context *context,
    const char *text,
    char *error,
    size_t size
)
{
    if (size) {
        error[0] = 0;
    }
    if (!code_source_is_structured(text)) {
        behavior_asset *asset = behavior_asset_load(text);
        if (!asset && size) {
            snprintf(error, size, "%s", graph_last_error());
        }
        return asset;
    }
    code_source *source = code_source_parse_context(text, NULL, context);
    behavior_asset *asset = code_source_build_behavior(source);
    if (!asset && size) {
        snprintf(
            error,
            size,
            "%s",
            acount(source->diagnostics) ? source->diagnostics[0].message : "Invalid executable source"
        );
    }
    code_source_free(source);
    return asset;
}

behavior_asset *code_catalog_load_path(
    const code_catalog *catalog,
    const char *text,
    const char *path,
    char *error,
    size_t size
)
{
    if (!code_source_is_structured(text)) {
        return code_catalog_load_behavior(catalog, text, error, size);
    }
    code_source *source = code_source_parse_context(text, NULL, code_catalog_context(catalog));
    code_source_metadata_load(source, path);
    behavior_asset *asset = code_source_build_behavior(source);
    if (!asset && size) {
        snprintf(
            error,
            size,
            "%s",
            acount(source->diagnostics) ? source->diagnostics[0].message : "Invalid executable source"
        );
    }
    code_source_free(source);
    return asset;
}

code_catalog *code_catalog_load(const char *content_root)
{
    assert(content_root);
    code_catalog *catalog = xcalloc(1, sizeof *catalog);
    char **prototypes = paths(content_root, "prototypes", ".kdl");
    for (int i = 0; i < acount(prototypes); i++) {
        char *text = text_file_read(prototypes[i]);
        char error[256];
        av_doc *doc = text ? av_doc_read(text, error, sizeof error) : NULL;
        const av_value *root = doc ? av_doc_root(doc) : NULL;
        const av_value *name = root && av_is_obj(root) ? av_obj_get(root, "name")
                                                       : NULL;
        if (name && av_is_str(name) && av_get_str(name)[0]) {
            apush(
                catalog->references,
                ((code_reference){ CODE_REFERENCE_PROTOTYPE, xstrdup(av_get_str(name)) })
            );
            code_prototype prototype = { .name = xstrdup(av_get_str(name)) };
            const char **components = NULL, **behaviors = NULL;
            const av_value *values = av_obj_get(root, "components");
            if (av_is_obj(values)) {
                av_obj_iter iter = av_obj_iter_with(values);
                av_value *key;
                while ((key = av_obj_iter_next(&iter))) {
                    apush(components, xstrdup(av_get_str(key)));
                }
            }
            values = av_obj_get(root, "behaviors");
            if (av_is_arr(values)) {
                av_arr_iter iter = av_arr_iter_with(values);
                av_value *value;
                while ((value = av_arr_iter_next(&iter))) {
                    if (av_is_str(value)) {
                        apush(behaviors, xstrdup(av_get_str(value)));
                    }
                }
            }
            prototype.components = components;
            prototype.component_count = acount(components);
            prototype.behaviors = behaviors;
            prototype.behavior_count = acount(behaviors);
            apush(catalog->prototypes, prototype);
        } else {
            rejected(catalog, prototypes[i], "Expected a named prototype definition");
        }
        av_doc_free(doc);
        free(text);
        free(prototypes[i]);
    }
    afree(prototypes);
    catalog->context.references = catalog->references;
    catalog->context.reference_count = acount(catalog->references);
    catalog->context.prototypes = catalog->prototypes;
    catalog->context.prototype_count = acount(catalog->prototypes);
    char **events = paths(content_root, "events", ".kdl");
    for (int i = 0; i < acount(events); i++) {
        char *text = text_file_read(events[i]);
        behavior_asset *event = text ? behavior_asset_load(text) : NULL;
        if (event && event->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION) {
            apush(catalog->events, event);
        } else {
            rejected(
                catalog,
                events[i],
                event  ? "Expected an event definition"
                : text ? graph_last_error()
                       : "Could not read event source"
            );
            behavior_asset_free(event);
        }
        free(text);
        free(events[i]);
    }
    afree(events);
    catalog->context.events = catalog->events;
    catalog->context.event_count = acount(catalog->events);

    char **libraries = paths(content_root, "libraries", ".code");
    char **texts = NULL;
    const behavior_asset **declarations = NULL;
    for (int i = 0; i < acount(libraries); i++) {
        char *text = text_file_read(libraries[i]);
        apush(texts, text);
        char error[256];
        behavior_asset *asset = text ? code_source_declarations(text, error, sizeof error)
                                     : NULL;
        if (asset && asset->domain == BEHAVIOR_DOMAIN_LIBRARY) {
            apush(declarations, asset);
        } else {
            rejected(
                catalog,
                libraries[i],
                asset  ? "Expected an executable library"
                : text ? error
                       : "Could not read library source"
            );
            behavior_asset_free(asset);
        }
    }
    catalog->context.libraries = declarations;
    catalog->context.library_count = acount(declarations);
    for (int i = 0; i < acount(libraries); i++) {
        char error[256];
        behavior_asset *asset = texts[i]
                                  ? code_catalog_load_path(catalog, texts[i], libraries[i], error, sizeof error)
                                  : NULL;
        if (asset && asset->domain == BEHAVIOR_DOMAIN_LIBRARY) {
            apush(catalog->libraries, asset);
        } else if (texts[i]) {
            rejected(catalog, libraries[i], asset ? "Expected an executable library" : error);
            behavior_asset_free(asset);
        }
        free(texts[i]);
        free(libraries[i]);
    }
    afree(texts);
    afree(libraries);
    for (int i = 0; i < acount(declarations); i++) {
        behavior_asset_free((behavior_asset *)declarations[i]);
    }
    afree(declarations);
    catalog->context.libraries = catalog->libraries;
    catalog->context.library_count = acount(catalog->libraries);
    /* Authoring metadata can give complete libraries different graph/pin IDs
     * from the declaration pass. Lower their external calls against those
     * final signatures before publishing the catalog. */
    const behavior_asset **complete = NULL;
    char **final_paths = paths(content_root, "libraries", ".code");
    for (int i = 0; i < acount(final_paths); i++) {
        char *text = text_file_read(final_paths[i]);
        char error[256];
        behavior_asset *asset = text
                                  ? code_catalog_load_path(catalog, text, final_paths[i], error, sizeof error)
                                  : NULL;
        if (asset && asset->domain == BEHAVIOR_DOMAIN_LIBRARY) {
            apush(complete, asset);
        } else {
            if (text) {
                rejected(catalog, final_paths[i], asset ? "Expected an executable library" : error);
            }
            behavior_asset_free(asset);
        }
        free(text);
        free(final_paths[i]);
    }
    afree(final_paths);
    for (int i = 0; i < acount(catalog->libraries); i++) {
        behavior_asset_free((behavior_asset *)catalog->libraries[i]);
    }
    afree(catalog->libraries);
    catalog->libraries = complete;
    catalog->context.libraries = complete;
    catalog->context.library_count = acount(complete);
    return catalog;
}

code_catalog *code_catalog_load_project(const char *project_root)
{
    if (!project_root || !project_root[0]) {
        return NULL;
    }
    chibi_project project = { 0 };
    char error[256];
    if (!chibi_project_open(&project, project_root, error, sizeof error)) {
        code_catalog *catalog = code_catalog_load(project.content);
        chibi_project_free(&project);
        return catalog;
    }
    /* In-memory authoring fixtures may supply a content tree before creating
     * their manifest. It uses the same conventional project directory. */
    size_t size = strlen(project_root) + sizeof "/content";
    char *content = xmalloc(size);
    snprintf(content, size, "%s/content", project_root);
    code_catalog *catalog = code_catalog_load(content);
    free(content);
    return catalog;
}

code_catalog *code_catalog_from_assets(const behavior_asset *const *assets, int count)
{
    code_catalog *catalog = xcalloc(1, sizeof *catalog);
    catalog->borrowed = true;
    for (int i = 0; i < count; i++) {
        const behavior_asset *asset = assets[i];
        if (asset->domain == BEHAVIOR_DOMAIN_LIBRARY) {
            apush(catalog->libraries, asset);
        }
        if (asset->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION) {
            apush(catalog->events, asset);
        }
    }
    catalog->context = (code_source_context){ catalog->libraries,
                                              acount(catalog->libraries),
                                              catalog->events,
                                              acount(catalog->events) };
    return catalog;
}

code_catalog *code_catalog_from_texts(const char *const *texts, int count)
{
    code_catalog *catalog = xcalloc(1, sizeof *catalog);
    for (int i = 0; i < count; i++) {
        if (!texts[i]) {
            continue;
        }
        char error[256];
        behavior_asset *asset = code_source_is_structured(texts[i])
                                  ? code_source_declarations(texts[i], error, sizeof error)
                                  : behavior_asset_load(texts[i]);
        if (!asset) {
            continue;
        }
        if (asset->domain == BEHAVIOR_DOMAIN_LIBRARY) {
            apush(catalog->libraries, asset);
        } else if (asset->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION) {
            apush(catalog->events, asset);
        } else {
            behavior_asset_free(asset);
        }
    }
    catalog->context = (code_source_context){ catalog->libraries,
                                              acount(catalog->libraries),
                                              catalog->events,
                                              acount(catalog->events) };
    return catalog;
}

bool code_catalog_restore_metadata(
    code_catalog *catalog,
    const char *const *texts,
    const char *const *metadata,
    int count
)
{
    assert(catalog && !catalog->borrowed);
    /* The first pass resolves bodies against declarations. The second pass
     * uses restored signatures, including foreign callable parameter IDs. */
    for (int pass = 0; pass < 2; pass++) {
        const behavior_asset **complete = NULL;
        bool valid = true;
        for (int i = 0; i < count && valid; i++) {
            if (!texts[i] || !code_source_is_structured(texts[i])) {
                continue;
            }
            char error[256];
            behavior_asset *declaration = code_source_declarations(texts[i], error, sizeof error);
            bool library = declaration && declaration->domain == BEHAVIOR_DOMAIN_LIBRARY;
            behavior_asset_free(declaration);
            if (!library) {
                continue;
            }
            code_source
                *source = code_source_parse_context(texts[i], NULL, &catalog->context);
            valid = source->asset &&
                    (!metadata[i] ||
                     code_source_metadata_restore(source, metadata[i]));
            behavior_asset *asset = valid ? code_source_build_behavior(source) : NULL;
            valid = asset != NULL;
            code_source_free(source);
            if (asset) {
                apush(complete, asset);
            }
        }
        if (!valid) {
            for (int i = 0; i < acount(complete); i++) {
                behavior_asset_free((behavior_asset *)complete[i]);
            }
            afree(complete);
            return false;
        }
        for (int i = 0; i < acount(catalog->libraries); i++) {
            behavior_asset_free((behavior_asset *)catalog->libraries[i]);
        }
        afree(catalog->libraries);
        catalog->libraries = complete;
        catalog->context.libraries = complete;
        catalog->context.library_count = acount(complete);
    }
    return true;
}

void code_catalog_add_reference(code_catalog *catalog, code_reference_kind kind, const char *name)
{
    assert(catalog && kind != CODE_REFERENCE_NONE && name && *name);
    apush(catalog->references, ((code_reference){ kind, xstrdup(name) }));
    catalog->context.references = catalog->references;
    catalog->context.reference_count = acount(catalog->references);
}

void code_catalog_free(code_catalog *catalog)
{
    if (!catalog) {
        return;
    }
    for (int i = 0; i < acount(catalog->libraries); i++) {
        if (!catalog->borrowed) {
            behavior_asset_free((behavior_asset *)catalog->libraries[i]);
        }
    }
    for (int i = 0; i < acount(catalog->events); i++) {
        if (!catalog->borrowed) {
            behavior_asset_free((behavior_asset *)catalog->events[i]);
        }
    }
    for (int i = 0; i < acount(catalog->errors); i++) {
        free((char *)catalog->errors[i].path);
        free((char *)catalog->errors[i].message);
    }
    afree(catalog->libraries);
    afree(catalog->events);
    afree(catalog->errors);
    for (int i = 0; i < acount(catalog->references); i++) {
        free((char *)catalog->references[i].name);
    }
    afree(catalog->references);
    for (int i = 0; i < acount(catalog->prototypes); i++) {
        code_prototype *prototype = &catalog->prototypes[i];
        free((char *)prototype->name);
        for (int j = 0; j < prototype->component_count; j++) {
            free((char *)prototype->components[j]);
        }
        for (int j = 0; j < prototype->behavior_count; j++) {
            free((char *)prototype->behaviors[j]);
        }
        afree(prototype->components);
        afree(prototype->behaviors);
    }
    afree(catalog->prototypes);
    free(catalog);
}

const code_source_context *code_catalog_context(const code_catalog *catalog)
{
    return catalog ? &catalog->context : NULL;
}
int code_catalog_error_count(const code_catalog *catalog)
{
    return catalog ? acount(catalog->errors) : 0;
}
const code_catalog_error *code_catalog_error_at(const code_catalog *catalog, int index)
{
    assert(catalog && index >= 0 && index < acount(catalog->errors));
    return &catalog->errors[index];
}
