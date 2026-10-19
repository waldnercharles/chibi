#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "authoring/source_package_files.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

enum
{
    MAX_PATH = 4096,
    MAX_DEPTH = 16,
    MAX_ENTRIES = 4096,
    MAX_FILE_BYTES = 512 * 1024,
    MAX_TOTAL_BYTES = 8 * 1024 * 1024,
};

static bool reject(source_package_load_error *error, const char *path, const char *message)
{
    snprintf(error->path, sizeof error->path, "%s", path ? path : "");
    snprintf(error->message, sizeof error->message, "%s", message);
    return false;
}

#ifndef _WIN32
typedef struct
{
    char **roots;
    struct stat *root_info;
    source_package_input *files;
    size_t bytes;
    int entries;
    source_package_load_error *error;
} package_files;

static bool within(const char *root, const char *path)
{
    if (!strcmp(root, "/")) {
        return path[0] == '/';
    }
    size_t size = strlen(root);
    return !strncmp(root, path, size) && (!path[size] || path[size] == '/');
}

static bool identifier(const char *name)
{
    if (!name || !*name || strnlen(name, MAX_PATH) == MAX_PATH) {
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

static bool add_root(package_files *files, const char *path)
{
    if (!path || path[0] != '/' || strnlen(path, MAX_PATH) == MAX_PATH) {
        return reject(files->error, path, "Package roots must be explicit absolute directories");
    }
    char *canonical = realpath(path, NULL);
    if (!canonical) {
        return reject(files->error, path, "Cannot canonicalize package root");
    }
    struct stat info;
    if (strlen(canonical) >= MAX_PATH || stat(canonical, &info) ||
        !S_ISDIR(info.st_mode)) {
        free(canonical);
        return reject(files->error, path, "Package root is not a supported directory");
    }
    for (int i = 0; i < acount(files->roots); i++) {
        if (!strcmp(files->roots[i], canonical) ||
            (files->root_info[i].st_dev == info.st_dev &&
             files->root_info[i].st_ino == info.st_ino)) {
            free(canonical);
            return reject(files->error, path, "Duplicate physical package root");
        }
    }
    apush(files->roots, canonical);
    apush(files->root_info, info);
    return true;
}

static bool same_file(const struct stat *a, const struct stat *b)
{
    if (a->st_dev != b->st_dev || a->st_ino != b->st_ino ||
        a->st_mode != b->st_mode || a->st_size != b->st_size ||
        a->st_mtime != b->st_mtime || a->st_ctime != b->st_ctime) {
        return false;
    }
#ifdef __APPLE__
    return a->st_mtimespec.tv_nsec == b->st_mtimespec.tv_nsec &&
           a->st_ctimespec.tv_nsec == b->st_ctimespec.tv_nsec;
#else
    return a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
           a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
#endif
}

static bool read_file(
    package_files *files,
    int directory,
    const char *name,
    const char *path,
    const struct stat *expected
)
{
    if (acount(files->files) >= SOURCE_PACKAGE_MAX_FILES) {
        return reject(files->error, path, "Package source file limit exceeded");
    }
    if (expected->st_size < 0 || expected->st_size > MAX_FILE_BYTES ||
        (size_t)expected->st_size > MAX_TOTAL_BYTES - files->bytes) {
        return reject(files->error, path, "Package source byte limit exceeded");
    }
    int fd = openat(directory, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return reject(files->error, path, "Cannot open package source without following symlinks");
    }
    struct stat before, after;
    bool ok = !fstat(fd, &before) && S_ISREG(before.st_mode) &&
              same_file(expected, &before);
    size_t size = (size_t)expected->st_size, used = 0;
    char *text = xmalloc(size + 1);
    while (ok && used < size) {
        ssize_t count = read(fd, text + used, size - used);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        ok = count > 0;
        if (ok) {
            used += (size_t)count;
        }
    }
    char extra;
    ssize_t count = -1;
    if (ok) {
        do {
            count = read(fd, &extra, 1);
        } while (count < 0 && errno == EINTR);
    }
    ok = ok && count == 0 && !fstat(fd, &after) && same_file(&before, &after) &&
         !memchr(text, 0, size);
    if (close(fd)) {
        ok = false;
    }
    if (!ok) {
        free(text);
        return reject(files->error, path, "Package source changed during read or contains NUL");
    }
    text[size] = 0;
    apush(files->files, ((source_package_input){ .path = xstrdup(path), .text = text, .size = size }));
    files->bytes += size;
    return true;
}

static bool depth_valid(const package_files *files, const char *path)
{
    const char *root = NULL;
    for (int i = 0; i < acount(files->roots); i++) {
        if (within(files->roots[i], path) &&
            (!root || strlen(files->roots[i]) > strlen(root))) {
            root = files->roots[i];
        }
    }
    assert(root);
    const char *relative = path + strlen(root);
    if (*relative == '/') {
        relative++;
    }
    int depth = *relative ? 1 : 0;
    for (const char *part = relative; *part; part++) {
        depth += *part == '/';
    }
    return depth <= MAX_DEPTH;
}

static bool explicit_root(const package_files *files, const char *path)
{
    for (int i = 0; i < acount(files->roots); i++) {
        if (!strcmp(files->roots[i], path)) {
            return true;
        }
    }
    return false;
}

static int open_root(const char *path)
{
    const int flags = O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW;
    int fd = open("/", flags);
    if (fd < 0) {
        return -1;
    }
    char components[MAX_PATH];
    memcpy(components, path, strlen(path) + 1);
    for (char *part = components + 1; *part;) {
        char *slash = strchr(part, '/');
        if (slash) {
            *slash = 0;
        }
        int next = openat(fd, part, flags);
        close(fd);
        if (next < 0) {
            return -1;
        }
        fd = next;
        if (!slash) {
            break;
        }
        part = slash + 1;
    }
    return fd;
}

/* Takes ownership of fd, including failure paths. Each descendant is opened
 * relative to its already opened parent; directory names are never followed. */
static bool scan(package_files *files, int fd, const char *path)
{
    DIR *directory = fdopendir(fd);
    if (!directory) {
        close(fd);
        return reject(files->error, path, "Cannot scan package directory");
    }
    struct stat before, after;
    bool ok = !fstat(fd, &before) && S_ISDIR(before.st_mode);
    if (!ok) {
        reject(files->error, path, "Cannot inspect package directory");
    }
    while (ok) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (!entry) {
            if (errno) {
                ok = reject(files->error, path, "Cannot read package directory");
            }
            break;
        }
        if (++files->entries > MAX_ENTRIES) {
            ok = reject(files->error, path, "Package directory entry limit exceeded");
            break;
        }
        if (entry->d_name[0] == '.') {
            continue;
        }
        char child[MAX_PATH];
        int length = snprintf(
            child,
            sizeof child,
            "%s%s%s",
            path,
            !strcmp(path, "/") ? "" : "/",
            entry->d_name
        );
        if (length < 0 || length >= (int)sizeof child) {
            ok = reject(files->error, path, "Package path limit exceeded");
            break;
        }
        struct stat info;
        if (fstatat(fd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW)) {
            ok = reject(files->error, child, "Cannot inspect package descendant");
            break;
        }
        if (S_ISLNK(info.st_mode)) {
            ok = reject(files->error, child, "Package descendants must not be symlinks");
            break;
        }
        if (S_ISDIR(info.st_mode)) {
            /* Each explicit root has its own checked descriptor walk, even
             * below a hidden directory. Ancestor scans never collect it twice. */
            if (explicit_root(files, child)) {
                continue;
            }
            if (!depth_valid(files, child)) {
                ok = reject(files->error, child, "Package directory depth limit exceeded");
                break;
            }
            int next = openat(fd, entry->d_name, O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
            struct stat opened;
            if (next < 0 || fstat(next, &opened) || !same_file(&info, &opened)) {
                if (next >= 0) {
                    close(next);
                }
                ok = reject(files->error, child, "Package directory changed during open");
                break;
            }
            ok = scan(files, next, child);
            continue;
        }
        size_t size = strlen(entry->d_name);
        if (size < 6 || strcmp(entry->d_name + size - 6, ".chibi")) {
            continue;
        }
        if (!S_ISREG(info.st_mode)) {
            ok = reject(files->error, child, "Package source must be a regular file");
            break;
        }
        ok = read_file(files, fd, entry->d_name, child, &info);
    }
    if (ok && (fstat(fd, &after) || !same_file(&before, &after))) {
        ok = reject(files->error, path, "Package directory changed during scan");
    }
    if (closedir(directory) && ok) {
        ok = reject(files->error, path, "Cannot close package directory");
    }
    return ok;
}

static int compare_files(const void *left, const void *right)
{
    const source_package_input *a = left, *b = right;
    return strcmp(a->path, b->path);
}

static void free_files(package_files *files)
{
    for (int i = 0; i < acount(files->roots); i++) {
        free(files->roots[i]);
    }
    afree(files->roots);
    afree(files->root_info);
    source_package_files_free(files->files);
}

/* Scans each root's files, then sorts them by path. */
static bool collect(package_files *files)
{
    for (int i = 0; i < acount(files->roots); i++) {
        struct stat opened;
        int fd = open_root(files->roots[i]);
        if (fd < 0 || fstat(fd, &opened) || !same_file(&files->root_info[i], &opened)) {
            if (fd >= 0) {
                close(fd);
            }
            return reject(files->error, files->roots[i], "Cannot open canonical package root");
        }
        if (!scan(files, fd, files->roots[i])) {
            return false;
        }
    }
    if (acount(files->files) > 1) {
        qsort(files->files, (size_t)acount(files->files), sizeof *files->files, compare_files);
    }
    return true;
}
#endif

void source_package_files_free(source_package_input *files)
{
    for (int i = 0; i < acount(files); i++) {
        free((char *)files[i].path);
        free((char *)files[i].text);
    }
    afree(files);
}

bool source_package_files_read(const char *root, source_package_input **files, source_package_load_error *error)
{
    assert(files && !*files && error);
    *error = (source_package_load_error){ 0 };
#ifdef _WIN32
    (void)root;
    (void)files;
    return reject(error, "", "Package filesystem loading is unsupported on Windows");
#else
    package_files found = { .error = error };
    bool ok = add_root(&found, root) && collect(&found);
    if (ok) {
        *files = found.files;
        found.files = NULL;
    }
    free_files(&found);
    return ok;
#endif
}

source_package_snapshot *source_package_snapshot_load(
    const source_package_disk_options *options,
    source_package_load_error *error
)
{
    assert(error);
    *error = (source_package_load_error){ 0 };
#ifdef _WIN32
    (void)options;
    reject(error, "", "Package filesystem loading is unsupported on Windows");
    return NULL;
#else
    if (!options || options->collection_count < 0 ||
        options->collection_count >= SOURCE_PACKAGE_MAX_FILES ||
        (options->collection_count && !options->collections)) {
        reject(error, "", "Invalid package filesystem options");
        return NULL;
    }
    package_files files = { .error = error };
    bool ok = add_root(&files, options->project_root);
    for (int i = 0; ok && i < options->collection_count; i++) {
        const source_package_collection *collection = &options->collections[i];
        ok = identifier(collection->name) && strcmp(collection->name, "project");
        for (int j = 0; ok && j < i; j++) {
            ok = strcmp(collection->name, options->collections[j].name) != 0;
        }
        if (!ok) {
            reject(error, collection->root, "Invalid or duplicate package collection name");
            break;
        }
        ok = add_root(&files, collection->root);
    }
    source_package_snapshot *snapshot = NULL;
    if (ok && collect(&files)) {
        source_package_collection collections[SOURCE_PACKAGE_MAX_FILES];
        for (int i = 0; i < options->collection_count; i++) {
            collections[i] = (source_package_collection){
                options->collections[i].name,
                files.roots[i + 1]
            };
        }
        snapshot = source_package_snapshot_new(&(source_package_snapshot_options){
            .project_root = files.roots[0],
            .files = files.files,
            .file_count = acount(files.files),
            .collections = collections,
            .collection_count = options->collection_count,
        });
    }
    free_files(&files);
    return snapshot;
#endif
}
