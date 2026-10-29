#include "authoring/source_package_paths.h"

#include <stdio.h>
#include <string.h>

bool source_package_path_bounded(const char *path)
{
    if (!path) {
        return false;
    }
    for (int i = 0; i < SOURCE_PACKAGE_PATH_MAX; i++) {
        if (!path[i]) {
            return !strchr(path, '\\');
        }
    }
    return false;
}
bool source_package_path_absolute(const char *path)
{
    if (!path) {
        return false;
    }
    if (path[0] == '/') {
        return true;
    }
#ifdef _WIN32
    return ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
           path[1] == ':' && path[2] == '/';
#else
    return false;
#endif
}
bool source_package_path_within(const char *root, const char *path)
{
    size_t size = strlen(root);
    return !strncmp(root, path, size) &&
           (root[size - 1] == '/' || !path[size] || path[size] == '/');
}
/* Inputs are lexical paths, not filesystem queries. Reject an intermediate ..
 * escape as well as a final escape; leaving and re-entering a root is invalid. */
char *source_package_path_normalize(arena *storage, const char *base, const char *path, const char *floor)
{
    if (!source_package_path_bounded(path)) {
        return NULL;
    }
    char joined[SOURCE_PACKAGE_PATH_MAX], result[SOURCE_PACKAGE_PATH_MAX] = "/";
    int size = source_package_path_absolute(path)
                 ? snprintf(joined, sizeof joined, "%s", path)
             : base ? snprintf(joined, sizeof joined, "%s/%s", base, path)
                    : -1;
    if (size < 0 || size >= SOURCE_PACKAGE_PATH_MAX) {
        return NULL;
    }
    int starts[SOURCE_PACKAGE_PATH_MAX / 2], depth = 0;
    size_t length = 1;
    char *part = joined;
#ifdef _WIN32
    if (joined[0] != '/' && joined[1] == ':' && joined[2] == '/') {
        length = 3;
    } else if (joined[0] == '/' && joined[1] == '/') {
        const char *server = strchr(joined + 2, '/');
        const char *share = server ? strchr(server + 1, '/') : NULL;
        if (!server || server == joined + 2 || !server[1] || server[1] == '/') {
            return NULL;
        }
        length = share ? (size_t)(share - joined) : strlen(joined);
    }
    if (length > 1) {
        memcpy(result, joined, length);
        result[length] = 0;
        part += length;
    }
#endif
    for (; *part;) {
        if (*part == '/') {
            part++;
            continue;
        }
        char *end = strchr(part, '/');
        size_t count = end ? (size_t)(end - part) : strlen(part);
        if (count == 2 && !memcmp(part, "..", 2)) {
            if (!depth) {
                return NULL;
            }
            length = (size_t)starts[--depth];
            result[length] = 0;
            if (floor && !source_package_path_within(floor, result)) {
                return NULL;
            }
        } else if (!(count == 1 && part[0] == '.')) {
            starts[depth++] = (int)length;
            if (length > 1 && result[length - 1] != '/') {
                result[length++] = '/';
            }
            memcpy(result + length, part, count);
            length += count;
            result[length] = 0;
        }
        part += count;
    }
    if (floor && !source_package_path_within(floor, result)) {
        return NULL;
    }
    char *owned = arena_alloc(storage, length + 1);
    memcpy(owned, result, length + 1);
    return owned;
}
