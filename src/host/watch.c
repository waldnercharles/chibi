#include "host/watch_internal.h"
#include "support/xalloc.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

bool bp_watch_excluded(const char *path)
{
    for (const char *part = path; *part;) {
        if (*part == '/') {
            part++;
            continue;
        }
        size_t size = strcspn(part, "/");
        if ((size == 4 && !strncmp(part, ".git", size)) ||
            (size == 11 && !strncmp(part, ".blueprints", size)) ||
            (size == 10 && !strncmp(part, "CMakeFiles", size)) ||
            (size == 11 && !strncmp(part, "__pycache__", size)) ||
            (size >= 5 && !strncmp(part, "build", 5) && (size == 5 || part[5] == '-'))) {
            return true;
        }
        part += size;
    }
    return false;
}

static bool relevant(bp_watch *watch, const char *path)
{
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    if (!strcmp(name, "Makefile") || !strcmp(name, "CMakeLists.txt")) {
        return true;
    }
    const char *suffix = strrchr(name, '.');
    for (int i = 0; suffix && i < watch->suffix_count; i++) {
        if (!strcmp(suffix, watch->suffixes[i])) {
            return true;
        }
    }
    return false;
}

bool bp_watch_matches(bp_watch *watch, const char *path, bool directory)
{
    for (int i = 0; i < watch->count; i++) {
        bp_watch_root *root = &watch->roots[i];
        if (!strcmp(root->path, path)) {
            return true;
        }
        size_t size = strlen(root->path);
        if (root->directory && !strncmp(root->path, path, size) &&
            (size == 1 || path[size] == '/') && !bp_watch_excluded(path + size) &&
            (directory || relevant(watch, path))) {
            return true;
        }
    }
    return false;
}

bp_watch *bp_watch_create(const char *const *paths, int count, const char *const *suffixes, int suffix_count)
{
    assert(paths && count > 0 && suffix_count >= 0 && (suffixes || suffix_count == 0));
    bp_watch *watch = xcalloc(1, sizeof *watch);
    atomic_init(&watch->revision, 0);
    watch->suffixes = xcalloc((size_t)suffix_count, sizeof *watch->suffixes);
    watch->suffix_count = suffix_count;
    for (int i = 0; i < suffix_count; i++) {
        watch->suffixes[i] = xstrdup(suffixes[i]);
    }
    watch->roots = xcalloc((size_t)count, sizeof *watch->roots);
    for (int i = 0; i < count; i++) {
        char *path = realpath(paths[i], NULL);
        struct stat info;
        if (!path || stat(path, &info) != 0) {
            int error = errno;
            free(path);
            bp_watch_free(watch);
            errno = error;
            return NULL;
        }
        watch->roots[watch->count++] = (bp_watch_root){ path, S_ISDIR(info.st_mode) };
    }
    if (!bp_watch_start(watch)) {
        int error = errno;
        bp_watch_free(watch);
        errno = error;
        return NULL;
    }
    return watch;
}

uint64_t bp_watch_revision(bp_watch *watch)
{
    return atomic_load_explicit(&watch->revision, memory_order_relaxed);
}

void bp_watch_free(bp_watch *watch)
{
    if (!watch) {
        return;
    }
    if (watch->backend) {
        bp_watch_stop(watch);
    }
    for (int i = 0; i < watch->count; i++) {
        free(watch->roots[i].path);
    }
    for (int i = 0; i < watch->suffix_count; i++) {
        free(watch->suffixes[i]);
    }
    free(watch->suffixes);
    free(watch->roots);
    free(watch);
}
