#include "assets/project_migrate.h"

#include "language/syntax.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct
{
    int file;
    TSNode library, name, body;
    bool waits;
} function;
typedef struct
{
    const char *const *sources;
    function *functions;
} corpus;
typedef struct
{
    int offset, order;
    const char *text;
} insertion;

static bool is(TSNode node, const char *type)
{
    return !strcmp(ts_node_type(node), type);
}
/* A name is an identifier or a quoted string; compare them without quotes. */
static bool same_name(const char *a_source, TSNode a, const char *b_source, TSNode b)
{
    uint32_t start_a = ts_node_start_byte(a), length_a = ts_node_end_byte(a) - start_a;
    uint32_t start_b = ts_node_start_byte(b), length_b = ts_node_end_byte(b) - start_b;
    if (is(a, "string")) {
        start_a++, length_a -= 2;
    }
    if (is(b, "string")) {
        start_b++, length_b -= 2;
    }
    return length_a == length_b &&
           !memcmp(a_source + start_a, b_source + start_b, length_a);
}
static bool is_name(TSNode node)
{
    return is(node, "identifier") || is(node, "string");
}
static void collect(TSNode node, int file, TSNode library, function **functions)
{
    if (is(node, "library")) {
        library = ts_node_child_by_field_name(node, "name", 4);
    }
    if (is(node, "function_declaration")) {
        apush(
            *functions,
            ((function){ .file = file,
                         .library = library,
                         .name = ts_node_child_by_field_name(node, "name", 4),
                         .body = ts_node_child_by_field_name(node, "body", 4) })
        );
        return;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        collect(ts_node_named_child(node, i), file, library, functions);
    }
}
static TSNode outer(TSNode node)
{
    TSNode parent = ts_node_parent(node);
    while (is(parent, "parenthesized_expression")) {
        node = parent;
        parent = ts_node_parent(parent);
    }
    return parent;
}
/* The function a call names: a local function by name, or a library function
 * as Library.Function in any file. */
static const function *callee(const corpus *c, int file, TSNode target)
{
    const char *source = c->sources[file];
    TSNode library = { 0 }, name = target;
    if (is(target, "member_expression")) {
        library = ts_node_child_by_field_name(target, "object", 6);
        name = ts_node_child_by_field_name(target, "member", 6);
        if (!is_name(library)) {
            return NULL;
        }
    }
    for (int i = 0; is_name(name) && i < acount(c->functions); i++) {
        const function *f = &c->functions[i];
        const char *defined = c->sources[f->file];
        bool scope = ts_node_is_null(library)
                       ? f->file == file
                       : !ts_node_is_null(f->library) &&
                             same_name(source, library, defined, f->library);
        if (scope && same_name(source, name, defined, f->name)) {
            return f;
        }
    }
    return NULL;
}
/* A call blocked under version 10 when it called a function that waits and
 * was not already awaited. An action's call runs when the action starts. */
static bool blocking(const corpus *c, int file, TSNode node)
{
    if (!is(node, "call_expression") || is(outer(node), "await_expression") ||
        is(ts_node_parent(node), "action_declaration")) {
        return false;
    }
    const function *f = callee(c, file, ts_node_child_by_field_name(node, "function", 8));
    return f && f->waits;
}
static bool waits(const corpus *c, int file, TSNode node)
{
    if (is(node, "await_expression") || blocking(c, file, node)) {
        return true;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        if (waits(c, file, ts_node_named_child(node, i))) {
            return true;
        }
    }
    return false;
}
static void edits(const corpus *c, int file, TSNode node, insertion **out)
{
    if (blocking(c, file, node)) {
        TSNode parent = outer(node);
        bool wrap = (is(parent, "member_expression") || is(parent, "index_expression")) &&
                    ts_node_start_byte(
                        ts_node_child_by_field_name(parent, "object", 6)
                    ) == ts_node_start_byte(node);
        int order = acount(*out);
        apush(*out, ((insertion){ (int)ts_node_start_byte(node), order, wrap ? "(await " : "await " }));
        if (wrap) {
            apush(*out, ((insertion){ (int)ts_node_end_byte(node), order + 1, ")" }));
        }
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        edits(c, file, ts_node_named_child(node, i), out);
    }
}
static int by_offset(const void *a, const void *b)
{
    const insertion *x = a, *y = b;
    return x->offset != y->offset ? x->offset - y->offset : x->order - y->order;
}
static char *apply(const char *source, insertion *inserts)
{
    if (!acount(inserts)) {
        return NULL;
    }
    qsort(inserts, (size_t)acount(inserts), sizeof *inserts, by_offset);
    size_t size = strlen(source) + 1;
    for (int i = 0; i < acount(inserts); i++) {
        size += strlen(inserts[i].text);
    }
    char *result = xmalloc(size), *at = result;
    int from = 0;
    for (int i = 0; i < acount(inserts); i++) {
        at = stpcpy(
            stpncpy(at, source + from, (size_t)(inserts[i].offset - from)),
            inserts[i].text
        );
        from = inserts[i].offset;
    }
    strcpy(at, source + from);
    return result;
}

char **chibi_migrate_task_calls(const char *const *sources, int count)
{
    corpus c = { .sources = sources };
    script_syntax **syntax = xmalloc((size_t)(count + 1) * sizeof *syntax);
    for (int i = 0; i < count; i++) {
        syntax[i] = script_parse(sources[i]);
        collect(ts_tree_root_node(syntax[i]->tree), i, (TSNode){ 0 }, &c.functions);
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < acount(c.functions); i++) {
            function *f = &c.functions[i];
            if (!f->waits && waits(&c, f->file, f->body)) {
                f->waits = changed = true;
            }
        }
    }
    char **migrated = xmalloc((size_t)(count + 1) * sizeof *migrated);
    for (int i = 0; i < count; i++) {
        insertion *inserts = NULL;
        edits(&c, i, ts_tree_root_node(syntax[i]->tree), &inserts);
        migrated[i] = apply(sources[i], inserts);
        afree(inserts);
    }
    for (int i = 0; i < count; i++) {
        script_syntax_free(syntax[i]);
    }
    free(syntax);
    afree(c.functions);
    return migrated;
}

static char *read_text(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    char *text = NULL;
    size_t size = 0;
    if (!fseek(file, 0, SEEK_END)) {
        long end = ftell(file);
        if (end >= 0 && !fseek(file, 0, SEEK_SET)) {
            size = (size_t)end;
            text = xmalloc(size + 1);
            if (fread(text, 1, size, file) != size) {
                free(text);
                text = NULL;
            }
        }
    }
    fclose(file);
    if (text) {
        text[size] = '\0';
    }
    return text;
}
static int write_text(const char *path, const char *text, char *error, size_t error_size)
{
    FILE *file = fopen(path, "wb");
    size_t size = strlen(text);
    bool written = file && fwrite(text, 1, size, file) == size;
    written = file && !fclose(file) && written;
    if (!written) {
        snprintf(error, error_size, "Cannot write migrated source %s", path);
    }
    return written ? 0 : -1;
}
static bool has_suffix(const char *name, const char *suffix)
{
    size_t length = strlen(name), suffix_length = strlen(suffix);
    return length > suffix_length && !strcmp(name + length - suffix_length, suffix);
}
static int collect_sources(const char *directory, char ***paths, char *error, size_t error_size)
{
    DIR *dir = opendir(directory);
    if (!dir) {
        snprintf(error, error_size, "Cannot open %s: %s", directory, strerror(errno));
        return -1;
    }
    int status = 0;
    for (struct dirent *entry; !status && (entry = readdir(dir));) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        size_t size = strlen(directory) + strlen(entry->d_name) + 2;
        char *path = xmalloc(size);
        snprintf(path, size, "%s/%s", directory, entry->d_name);
        struct stat info;
        if (stat(path, &info)) {
            snprintf(error, error_size, "Cannot inspect %s", path);
            status = -1;
        } else if (S_ISDIR(info.st_mode)) {
            status = collect_sources(path, paths, error, error_size);
        } else if (has_suffix(entry->d_name, ".chibi")) {
            apush(*paths, path);
            continue;
        }
        free(path);
    }
    closedir(dir);
    return status;
}
int chibi_project_migrate_content(const char *content, int from, char *error, size_t error_size)
{
    char **paths = NULL;
    int status = collect_sources(content, &paths, error, error_size);
    int count = acount(paths);
    char **sources = xmalloc((size_t)(count + 1) * sizeof *sources);
    for (int i = 0; i < count; i++) {
        sources[i] = status ? NULL : read_text(paths[i]);
        if (!status && !sources[i]) {
            snprintf(error, error_size, "Cannot read source %s", paths[i]);
            status = -1;
        }
    }
    char **migrated = NULL;
    if (!status) {
        switch (from) {
            case 10:
                migrated = chibi_migrate_task_calls((const char *const *)sources, count);
                break;
            default: assert(false);
        }
    }
    for (int i = 0; i < count; i++) {
        if (!status && migrated[i]) {
            status = write_text(paths[i], migrated[i], error, error_size);
        }
        free(migrated ? migrated[i] : NULL);
        free(sources[i]);
        free(paths[i]);
    }
    free(migrated);
    free(sources);
    afree(paths);
    return status;
}
