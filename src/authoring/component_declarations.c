#include "authoring/component_declarations.h"

#include "support/array.h"
#include "support/asset_text.h"
#include "support/asset_value.h"
#include "support/text_file.h"
#include "support/xalloc.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILES 1024

static bool fail(char *error, size_t size, const char *path, const char *message)
{
    if (size) {
        snprintf(error, size, "%s: %s", path, message);
    }
    return false;
}
static int compare(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}
static bool only_keys(const av_value *object, const char *const *keys, int count)
{
    for (size_t i = 0; i < av_obj_size(object); i++) {
        const char *key = av_get_str(av_obj_key_at(object, i));
        bool known = false;
        for (int j = 0; j < count && !known; j++) {
            known = !strcmp(key, keys[j]);
        }
        if (!known) {
            return false;
        }
    }
    return true;
}
static bool optional_text(const av_value *object, const char *key, const char **out)
{
    const av_value *value = av_obj_get(object, key);
    *out = value ? av_get_str(value) : NULL;
    return !value || av_is_str(value);
}
static bool component(const char *path, const av_value *root, declared_component *out, char *error, size_t size)
{
    static const char *const keys[] = { "name", "description", "fields" };
    static const char *const field_keys[] = { "name", "type", "description" };
    if (!av_is_obj(root) || !only_keys(root, keys, 3)) {
        return fail(error, size, path, "a component declares only name, description and fields");
    }
    const av_value *fields = av_obj_get(root, "fields");
    if (!optional_text(root, "name", &out->name) || !out->name ||
        !optional_text(root, "description", &out->description) || !av_is_arr(fields)) {
        return fail(error, size, path, "a component needs a name and an array of fields");
    }
    declared_field *list = NULL;
    for (size_t i = 0; i < av_arr_size(fields); i++) {
        const av_value *f = av_arr_get(fields, i);
        declared_field field = { 0 };
        if (!av_is_obj(f) || !only_keys(f, field_keys, 3) ||
            !optional_text(f, "name", &field.name) ||
            !optional_text(f, "type", &field.type) ||
            !optional_text(f, "description", &field.description) ||
            !field.name || !field.type) {
            afree(list);
            return fail(error, size, path, "each field declares a name, a type and an optional description");
        }
        apush(list, field);
    }
    out->fields = list;
    out->field_count = acount(list);
    return true;
}
bool authoring_component_declarations_load(
    const char *content,
    const script_catalog *native,
    declared_components **out,
    char *error,
    size_t size
)
{
    *out = NULL;
    char directory[4096];
    if (snprintf(directory, sizeof directory, "%s/components", content) >=
        (int)sizeof directory) {
        return fail(error, size, content, "component folder path is too long");
    }
    DIR *dir = opendir(directory);
    if (!dir) {
        return errno == ENOENT ||
               fail(error, size, directory, "cannot open the component folder");
    }
    char **names = NULL;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        size_t n = strlen(entry->d_name);
        if (entry->d_name[0] != '.' && n > 4 && !strcmp(entry->d_name + n - 4, ".kdl")) {
            apush(names, xstrdup(entry->d_name));
        }
    }
    closedir(dir);
    if (acount(names) > MAX_FILES) {
        for (int i = 0; i < acount(names); i++) {
            free(names[i]);
        }
        afree(names);
        return fail(error, size, directory, "a project declares at most 1024 components");
    }
    if (names) {
        qsort(names, (size_t)acount(names), sizeof *names, compare);
    }
    av_doc **documents = NULL;
    declared_component *components = NULL;
    bool valid = true;
    for (int i = 0; valid && i < acount(names); i++) {
        char path[4352];
        snprintf(path, sizeof path, "%s/%s", directory, names[i]);
        char *text = text_file_read(path);
        bool read = text != NULL;
        char message[256] = { 0 };
        av_doc *document = read ? av_doc_read(text, message, sizeof message) : NULL;
        free(text);
        declared_component c = { 0 };
        valid = document
                  ? component(path, av_doc_root(document), &c, error, size)
                  : fail(error, size, path, read ? message : "cannot read the file");
        if (document) {
            apush(documents, document);
        }
        if (valid && script_catalog_find_type(native, c.name) != -1) {
            valid = fail(error, size, path, "the name is already an engine type");
        }
        if (valid) {
            apush(components, c);
        }
    }
    if (valid && acount(components)) {
        *out = declared_components_new(components, acount(components), error, size);
        valid = *out != NULL;
    }
    for (int i = 0; i < acount(components); i++) {
        declared_field *fields = (declared_field *)components[i].fields;
        afree(fields);
    }
    afree(components);
    for (int i = 0; i < acount(documents); i++) {
        av_doc_free(documents[i]);
    }
    afree(documents);
    for (int i = 0; i < acount(names); i++) {
        free(names[i]);
    }
    afree(names);
    return valid;
}
