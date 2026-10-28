#include "host/native_libraries.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#if defined(_WIN32)
#define LIBRARY_SUFFIX ".dll"
#elif defined(__APPLE__)
#define LIBRARY_SUFFIX ".dylib"
#else
#define LIBRARY_SUFFIX ".so"
#endif
#define MANIFEST_PREFIX "chibi_natives_"

struct native_libraries
{
    void **handles;
    const execution_native_manifest **manifests;
};

static void *library_open(const char *path)
{
#ifdef _WIN32
    return LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}
static void *library_symbol(void *handle, const char *name)
{
#ifdef _WIN32
    return (void *)GetProcAddress(handle, name);
#else
    return dlsym(handle, name);
#endif
}
static void library_close(void *handle)
{
#ifdef _WIN32
    FreeLibrary(handle);
#else
    dlclose(handle);
#endif
}
static void library_error(const char *path, char *error, size_t size)
{
#ifdef _WIN32
    snprintf(error, size, "native library %s: Windows error %lu", path, GetLastError());
#else
    const char *reason = dlerror();
    snprintf(error, size, "native library %s: %s", path, reason ? reason : "cannot be loaded");
#endif
}

/* The library a file name holds, or NULL when it names no library. */
static char *library_name(const char *file)
{
    size_t length = strlen(file), suffix = strlen(LIBRARY_SUFFIX);
    if (length <= suffix || strcmp(file + length - suffix, LIBRARY_SUFFIX)) {
        return NULL;
    }
    char *name = xmalloc(length - suffix + 1);
    memcpy(name, file, length - suffix);
    name[length - suffix] = 0;
    return name;
}
static bool load(native_libraries *libraries, const char *folder, const char *file, char *error, size_t size)
{
    char *name = library_name(file);
    if (!name) {
        return true;
    }
    size_t path_size = strlen(folder) + strlen(file) + 2,
           symbol_size = strlen(MANIFEST_PREFIX) + strlen(name) + 1;
    char *path = xmalloc(path_size), *symbol = xmalloc(symbol_size);
    snprintf(path, path_size, "%s/%s", folder, file);
    snprintf(symbol, symbol_size, "%s%s", MANIFEST_PREFIX, name);
    void *handle = library_open(path);
    const execution_native_manifest *manifest = handle ? library_symbol(handle, symbol)
                                                       : NULL;
    if (!handle) {
        library_error(path, error, size);
    } else if (!manifest) {
        snprintf(error, size, "native library %s has no %s", path, symbol);
        library_close(handle);
    } else {
        apush(libraries->handles, handle);
        apush(libraries->manifests, manifest);
    }
    free(symbol);
    free(path);
    free(name);
    return manifest;
}

static int compare_names(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}
native_libraries *native_libraries_open(const char *folder, char *error, size_t size)
{
    native_libraries *libraries = xcalloc(1, sizeof *libraries);
    DIR *dir = opendir(folder);
    if (!dir) {
        if (errno == ENOENT) {
            return libraries;
        }
        snprintf(error, size, "native libraries %s: %s", folder, strerror(errno));
        free(libraries);
        return NULL;
    }
    /* Sorted, so every machine loads them, and reports a failure, alike. */
    char **files = NULL;
    for (struct dirent *entry; (entry = readdir(dir));) {
        char *file = xmalloc(strlen(entry->d_name) + 1);
        strcpy(file, entry->d_name);
        apush(files, file);
    }
    closedir(dir);
    asort(files, compare_names);
    bool loaded = true;
    for (int i = 0; i < acount(files); i++) {
        loaded = loaded && load(libraries, folder, files[i], error, size);
        free(files[i]);
    }
    afree(files);
    if (!loaded) {
        native_libraries_close(libraries);
        return NULL;
    }
    return libraries;
}
const execution_native_manifest *const *native_libraries_manifests(
    const native_libraries *libraries,
    int *count
)
{
    *count = acount(libraries->manifests);
    return libraries->manifests;
}
void native_libraries_close(native_libraries *libraries)
{
    if (!libraries) {
        return;
    }
    for (int i = 0; i < acount(libraries->handles); i++) {
        library_close(libraries->handles[i]);
    }
    afree(libraries->handles);
    afree(libraries->manifests);
    free(libraries);
}
