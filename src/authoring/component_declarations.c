#include "authoring/component_declarations.h"

#include "language/component.h"
#include "support/array.h"
#include "support/text_file.h"
#include "support/xalloc.h"

#include <assert.h>
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
static bool located(char *error, size_t size, const char *path, script_range at, const char *message)
{
    char located[256];
    snprintf(located, sizeof located, "%d:%d: %s", at.line, at.column, message);
    return fail(error, size, path, located);
}
/* Reads the component one source declares, or sets `error`. */
static script_component *read_source(
    const char *path,
    const char *text,
    const script_catalog *native,
    char *error,
    size_t size
)
{
    script_syntax *syntax = script_parse(text);
    script_diagnostic *diagnostics = NULL;
    script_component *source = script_component_read(syntax, native, &diagnostics);
    script_syntax_free(syntax);
    if (!source) {
        assert(acount(diagnostics));
        located(error, size, path, diagnostics[0].range, diagnostics[0].message);
    }
    afree(diagnostics);
    for (int i = 0; source && i < acount(source->fields); i++) {
        const script_component_field *f = &source->fields[i];
        if (!declared_components_field_type(f->type)) {
            char message[192];
            snprintf(message, sizeof message, "A component field cannot hold a %s", f->type);
            located(error, size, path, f->range, message);
            script_component_free(source);
            source = NULL;
        }
    }
    return source;
}
/* The declaration `source` describes; its fields are the caller's to afree. */
static declared_component declaration(const script_component *source)
{
    declared_field *fields = NULL;
    for (int i = 0; i < acount(source->fields); i++) {
        const script_component_field *f = &source->fields[i];
        apush(
            fields,
            ((declared_field){ .name = f->name,
                               .type = f->type,
                               .description = f->description,
                               .unit = f->unit,
                               .min = f->min,
                               .max = f->max })
        );
    }
    return (declared_component){ .name = source->name,
                                 .description = source->description,
                                 .fields = fields,
                                 .field_count = acount(fields) };
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
    if (!dir && errno != ENOENT) {
        return fail(error, size, directory, "cannot open the component folder");
    }
    if (!dir) {
        *out = declared_components_new(NULL, 0, error, size);
        return true;
    }
    char **names = NULL;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        size_t n = strlen(entry->d_name);
        if (entry->d_name[0] != '.' && n > 6 &&
            !strcmp(entry->d_name + n - 6, ".chibi")) {
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
    script_component **sources = NULL;
    declared_component *components = NULL;
    bool valid = true;
    for (int i = 0; valid && i < acount(names); i++) {
        char path[4352];
        snprintf(path, sizeof path, "%s/%s", directory, names[i]);
        char *text = text_file_read(path);
        script_component *source = text ? read_source(path, text, native, error, size)
                                        : NULL;
        valid = text ? source != NULL
                     : fail(error, size, path, "cannot read the file");
        free(text);
        if (source) {
            apush(sources, source);
            apush(components, declaration(source));
        }
        if (valid && script_catalog_find_type(native, source->name) != -1) {
            valid = fail(error, size, path, "the name is already an engine type");
        }
    }
    if (valid) {
        *out = declared_components_new(components, acount(components), error, size);
        valid = *out != NULL;
    }
    for (int i = 0; i < acount(components); i++) {
        declared_field *fields = (declared_field *)components[i].fields;
        afree(fields);
    }
    afree(components);
    for (int i = 0; i < acount(sources); i++) {
        script_component_free(sources[i]);
    }
    afree(sources);
    for (int i = 0; i < acount(names); i++) {
        free(names[i]);
    }
    afree(names);
    return valid;
}
