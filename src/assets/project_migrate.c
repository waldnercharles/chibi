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
    TSNode name, body;
    bool waits;
} function;
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
static bool same_name(const char *source, TSNode a, TSNode b)
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
           !memcmp(source + start_a, source + start_b, length_a);
}
static void collect(TSNode node, function **functions)
{
    if (is(node, "function_declaration")) {
        apush(
            *functions,
            ((function){ .name = ts_node_child_by_field_name(node, "name", 4),
                         .body = ts_node_child_by_field_name(node, "body", 4) })
        );
        return;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        collect(ts_node_named_child(node, i), functions);
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
/* A call blocked under version 10 when it called a local function that waits
 * and was not already awaited. An action's call runs when the action starts. */
static bool blocking(TSNode node, const char *source, const function *functions)
{
    if (!is(node, "call_expression") || is(outer(node), "await_expression") ||
        is(ts_node_parent(node), "action_declaration")) {
        return false;
    }
    TSNode target = ts_node_child_by_field_name(node, "function", 8);
    bool named = is(target, "identifier") || is(target, "string");
    for (int i = 0; named && i < acount(functions); i++) {
        if (same_name(source, target, functions[i].name)) {
            return functions[i].waits;
        }
    }
    return false;
}
static bool waits(TSNode node, const char *source, const function *functions)
{
    if (is(node, "await_expression") || blocking(node, source, functions)) {
        return true;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        if (waits(ts_node_named_child(node, i), source, functions)) {
            return true;
        }
    }
    return false;
}
static void edits(TSNode node, const char *source, const function *functions, insertion **out)
{
    if (blocking(node, source, functions)) {
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
        edits(ts_node_named_child(node, i), source, functions, out);
    }
}
static int by_offset(const void *a, const void *b)
{
    const insertion *x = a, *y = b;
    return x->offset != y->offset ? x->offset - y->offset : x->order - y->order;
}

char *chibi_migrate_task_calls(const char *source)
{
    script_syntax *syntax = script_parse(source);
    TSNode root = ts_tree_root_node(syntax->tree);
    function *functions = NULL;
    collect(root, &functions);
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < acount(functions); i++) {
            if (!functions[i].waits && waits(functions[i].body, source, functions)) {
                functions[i].waits = changed = true;
            }
        }
    }
    insertion *inserts = NULL;
    edits(root, source, functions, &inserts);
    char *result = NULL;
    if (acount(inserts)) {
        qsort(inserts, (size_t)acount(inserts), sizeof *inserts, by_offset);
        size_t length = strlen(source), size = length + 1;
        for (int i = 0; i < acount(inserts); i++) {
            size += strlen(inserts[i].text);
        }
        result = xmalloc(size);
        char *at = result;
        int from = 0;
        for (int i = 0; i < acount(inserts); i++) {
            at = stpcpy(
                stpncpy(at, source + from, (size_t)(inserts[i].offset - from)),
                inserts[i].text
            );
            from = inserts[i].offset;
        }
        strcpy(at, source + from);
    }
    afree(inserts);
    afree(functions);
    script_syntax_free(syntax);
    return result;
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
static int migrate_file(const char *path, int from, char *error, size_t error_size)
{
    char *source = read_text(path);
    if (!source) {
        snprintf(error, error_size, "Cannot read source %s", path);
        return -1;
    }
    char *migrated = NULL;
    switch (from) {
        case 10: migrated = chibi_migrate_task_calls(source); break;
        default: assert(false);
    }
    int status = migrated ? write_text(path, migrated, error, error_size) : 0;
    free(migrated);
    free(source);
    return status;
}
static int migrate_directory(const char *directory, int from, char *error, size_t error_size)
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
            status = migrate_directory(path, from, error, error_size);
        } else if (has_suffix(entry->d_name, ".chibi")) {
            status = migrate_file(path, from, error, error_size);
        }
        free(path);
    }
    closedir(dir);
    return status;
}
int chibi_project_migrate_content(const char *content, int from, char *error, size_t error_size)
{
    return migrate_directory(content, from, error, error_size);
}
