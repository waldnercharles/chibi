#include "authoring/project_open.h"

#include <stdio.h>

static bool explicit_native(native_capabilities *native, char *error, size_t size)
{
    if (native) {
        return true;
    }
    if (size) {
        snprintf(error, size, "Project authoring requires an explicit native capability table");
    }
    return false;
}

int authoring_project_open(
    chibi_project *project,
    const char *path,
    native_capabilities *native,
    char *error,
    size_t size
)
{
    if (!explicit_native(native, error, size)) {
        return -1;
    }
    return chibi_project_read(project, path, error, size);
}

int authoring_project_open_content(
    chibi_project *project,
    const char *path,
    native_capabilities *native,
    char *error,
    size_t size
)
{
    if (!explicit_native(native, error, size)) {
        return -1;
    }
    return chibi_project_open_content_migrating(project, path, NULL, NULL, error, size);
}

int authoring_project_migrate(const char *path, native_capabilities *native, char *error, size_t size)
{
    if (!explicit_native(native, error, size)) {
        return -1;
    }
    return chibi_project_migrate(path, error, size);
}
