#include "authoring/source_package_routes.h"

#include "authoring/source_package_paths.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <yyjson.h>

enum
{
    MAX_DEPTH = 16
};
static char *copy(arena *storage, const char *text, size_t size)
{
    char *owned = arena_alloc(storage, size + 1);
    memcpy(owned, text, size);
    owned[size] = 0;
    return owned;
}
static bool identifier(const char *name)
{
    if (!source_package_path_bounded(name) || !*name) {
        return false;
    }
    for (int i = 0; name[i]; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c != '_' && !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
            !(i && c >= '0' && c <= '9')) {
            return false;
        }
    }
    return true;
}
static const char *relative(const char *root, const char *path)
{
    assert(source_package_path_within(root, path));
    const char *result = path + strlen(root);
    if (!strcmp(root, "/")) {
        return result;
    }
    return *result == '/' ? result + 1 : result;
}
static const char *key(arena *storage, const source_package_collection *root, const char *path)
{
    const char *tail = relative(root->root, path);
    size_t size = strlen(root->name) + strlen(tail) + 2;
    char *result = arena_alloc(storage, size);
    snprintf(result, size, "%s:%s", root->name, tail);
    return result;
}
static const char *add_root(
    arena *storage,
    source_package_collection *roots,
    int *count,
    const char *name,
    const char *path
)
{
    if (!identifier(name)) {
        return "Invalid collection name";
    }
    const char *root = source_package_path_normalize(storage, NULL, path, NULL);
    if (!root) {
        return "Invalid absolute collection root";
    }
    for (int i = 0; i < *count; i++) {
        if (!strcmp(roots[i].name, name)) {
            return "Duplicate collection name";
        }
        if (!strcmp(roots[i].root, root)) {
            return "Duplicate collection root";
        }
    }
    roots[(*count)++] = (source_package_collection){ copy(storage, name, strlen(name)),
                                                     root };
    return NULL;
}
const char *source_package_routes_roots(
    arena *storage,
    const char *project_root,
    const source_package_collection *collections,
    int collection_count,
    source_package_collection *roots,
    int *root_count
)
{
    *root_count = 0;
    const char *error = add_root(storage, roots, root_count, "project", project_root);
    if (error) {
        return error;
    }
    for (int i = 0; i < collection_count; i++) {
        error = add_root(
            storage,
            roots,
            root_count,
            collections[i].name,
            collections[i].root
        );
        if (error) {
            return error;
        }
    }
    return NULL;
}
static int owning_root(const source_package_collection *roots, int count, const char *directory)
{
    int selected = -1;
    for (int i = 0; i < count; i++) {
        if (source_package_path_within(roots[i].root, directory) &&
            (selected < 0 || strlen(roots[i].root) > strlen(roots[selected].root))) {
            selected = i;
        }
    }
    return selected;
}
const char *source_package_routes_file(
    arena *storage,
    const source_package_collection *roots,
    int root_count,
    const char *input_path,
    source_package_routed_file *out
)
{
    *out = (source_package_routed_file){ 0 };
    if (!source_package_path_bounded(input_path) || !*input_path) {
        return "Invalid package file path";
    }
    const char *path = source_package_path_normalize(
        storage,
        roots[0].root,
        input_path,
        source_package_path_absolute(input_path) ? NULL : roots[0].root
    );
    if (!path || path[strlen(path) - 1] == '/') {
        return "Invalid package file path or root escape";
    }
    const char *slash = strrchr(path, '/');
    size_t directory_size = (size_t)(slash - path);
#ifdef _WIN32
    directory_size += slash == path + 2 && path[1] == ':';
#endif
    const char *directory = slash == path ? "/" : copy(storage, path, directory_size);
    int root = owning_root(roots, root_count, directory);
    if (root < 0) {
        return "Package file is outside the configured roots";
    }
    int depth = 0;
    for (const char *part = relative(roots[root].root, directory); *part; part++) {
        depth += *part == '/';
    }
    if (depth >= MAX_DEPTH) {
        return "Package path nesting exceeds the supported depth";
    }
    *out = (source_package_routed_file){ .path = path,
                                         .directory = directory,
                                         .key = key(storage, &roots[root], path),
                                         .identity = key(storage, &roots[root], directory),
                                         .root = root };
    return NULL;
}
const char *source_package_routes_import(
    arena *storage,
    const source_package_collection *roots,
    int root_count,
    const char *directory,
    int file_root,
    const char *literal,
    size_t size,
    char **out_directory
)
{
    *out_directory = NULL;
    yyjson_doc *json = yyjson_read(literal, size, 0);
    yyjson_val *value = json ? yyjson_doc_get_root(json) : NULL;
    if (!yyjson_is_str(value) ||
        yyjson_get_len(value) != strlen(yyjson_get_str(value))) {
        yyjson_doc_free(json);
        return "Invalid import string or embedded NUL";
    }
    const char *path = yyjson_get_str(value), *base = directory;
    int root = file_root;
    const char *colon = strchr(path, ':');
    if (colon) {
        root = -1;
        for (int i = 0; i < root_count; i++) {
            if (strlen(roots[i].name) == (size_t)(colon - path) &&
                !memcmp(roots[i].name, path, (size_t)(colon - path))) {
                root = i;
                break;
            }
        }
        if (root < 0) {
            yyjson_doc_free(json);
            return "Unknown import collection";
        }
        path = colon + 1;
        base = roots[root].root;
    }
    if ((!colon && !*path) || *path == '/' || !source_package_path_bounded(path)) {
        yyjson_doc_free(json);
        return "Invalid import path";
    }
    char *resolved = source_package_path_normalize(
        storage,
        base,
        path,
        roots[root].root
    );
    yyjson_doc_free(json);
    if (!resolved) {
        return "Import path escapes its collection root or exceeds the path "
               "limit";
    }
    *out_directory = resolved;
    return NULL;
}
