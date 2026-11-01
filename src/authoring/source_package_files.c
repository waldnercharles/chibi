#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "authoring/source_package_io.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

enum
{
    PACKAGE_PATH_MAX = 4096,
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

typedef struct
{
    char **roots;
#ifdef _WIN32
    struct windows_file_info *root_info;
#else
    struct stat *root_info;
#endif
    source_package_input *files;
    size_t bytes;
    int entries;
    /* The roots from this index on are collections, whose tests/ folders
     * belong to their own `chibi test` runs and never to an importer, as Go
     * leaves a package's _test files out of the packages that import it. */
    int first_collection;
    /* Set while a collection root is scanned. */
    bool skip_tests;
    source_package_load_error *error;
} package_files;

static bool within(const char *root, const char *path)
{
    size_t size = strlen(root);
    return !strncmp(root, path, size) &&
           (root[size - 1] == '/' || !path[size] || path[size] == '/');
}

static bool identifier(const char *name)
{
    if (!name || !*name || strnlen(name, PACKAGE_PATH_MAX) == PACKAGE_PATH_MAX) {
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

#ifndef _WIN32
static bool add_root(package_files *files, const char *path)
{
    if (!path || path[0] != '/' || strnlen(path, PACKAGE_PATH_MAX) == PACKAGE_PATH_MAX) {
        return reject(files->error, path, "Package roots must be explicit absolute directories");
    }
    char *canonical = realpath(path, NULL);
    if (!canonical) {
        return reject(files->error, path, "Cannot canonicalize package root");
    }
    struct stat info;
    if (strlen(canonical) >= PACKAGE_PATH_MAX || stat(canonical, &info) ||
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

static int open_root(const char *path)
{
    const int flags = O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW;
    int fd = open("/", flags);
    if (fd < 0) {
        return -1;
    }
    char components[PACKAGE_PATH_MAX];
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
        char child[PACKAGE_PATH_MAX];
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
            if (explicit_root(files, child) ||
                (files->skip_tests && !strcmp(entry->d_name, "tests"))) {
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
        files->skip_tests = i >= files->first_collection;
        if (!scan(files, fd, files->roots[i])) {
            return false;
        }
    }
    if (acount(files->files) > 1) {
        qsort(files->files, (size_t)acount(files->files), sizeof *files->files, compare_files);
    }
    return true;
}
#else
typedef struct windows_file_info
{
    BY_HANDLE_FILE_INFORMATION file;
    FILE_BASIC_INFO basic;
} windows_file_info;

static bool disk_absolute(const char *path)
{
    return path &&
           ((((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
             path[1] == ':' && (path[2] == '/' || path[2] == '\\')) ||
            ((path[0] == '/' || path[0] == '\\') &&
             (path[1] == '/' || path[1] == '\\')));
}

/* Extended paths preserve the package reader's 4096-byte limit instead of
 * acquiring the Win32 MAX_PATH limit. Public paths use forward slashes. */
static bool windows_path(const char *path, char native[PACKAGE_PATH_MAX + 8])
{
    char full[PACKAGE_PATH_MAX];
    DWORD size = GetFullPathNameA(path, sizeof full, full, NULL);
    if (!size || size >= sizeof full) {
        return false;
    }
    int length = !strncmp(full, "\\\\?\\", 4)
                   ? snprintf(native, PACKAGE_PATH_MAX + 8, "%s", full)
               : !strncmp(full, "\\\\", 2)
                   ? snprintf(native, PACKAGE_PATH_MAX + 8, "\\\\?\\UNC\\%s", full + 2)
                   : snprintf(native, PACKAGE_PATH_MAX + 8, "\\\\?\\%s", full);
    if (length < 0 || length >= PACKAGE_PATH_MAX + 8) {
        return false;
    }
    for (char *part = native; *part; part++) {
        if (*part == '/') {
            *part = '\\';
        }
    }
    return true;
}

static HANDLE windows_open(const char *path, bool follow)
{
    char native[PACKAGE_PATH_MAX + 8];
    if (!windows_path(path, native)) {
        return INVALID_HANDLE_VALUE;
    }
    return CreateFileA(
        native,
        GENERIC_READ,
        follow ? FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE : FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | (follow ? 0 : FILE_FLAG_OPEN_REPARSE_POINT),
        NULL
    );
}

static bool windows_info(HANDLE handle, windows_file_info *info)
{
    return GetFileType(handle) == FILE_TYPE_DISK &&
           GetFileInformationByHandle(handle, &info->file) &&
           GetFileInformationByHandleEx(
               handle,
               FileBasicInfo,
               &info->basic,
               sizeof info->basic
           );
}

static bool same_identity(const windows_file_info *a, const windows_file_info *b)
{
    return a->file.dwVolumeSerialNumber == b->file.dwVolumeSerialNumber &&
           a->file.nFileIndexHigh == b->file.nFileIndexHigh &&
           a->file.nFileIndexLow == b->file.nFileIndexLow;
}

static bool same_file(const windows_file_info *a, const windows_file_info *b)
{
    return same_identity(a, b) &&
           a->file.dwFileAttributes == b->file.dwFileAttributes &&
           a->file.nFileSizeHigh == b->file.nFileSizeHigh &&
           a->file.nFileSizeLow == b->file.nFileSizeLow &&
           a->basic.LastWriteTime.QuadPart == b->basic.LastWriteTime.QuadPart &&
           a->basic.ChangeTime.QuadPart == b->basic.ChangeTime.QuadPart;
}

static char *windows_canonical(const char *path, windows_file_info *info)
{
    HANDLE handle = windows_open(path, true);
    if (handle == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    char final[PACKAGE_PATH_MAX + 8];
    DWORD size = GetFinalPathNameByHandleA(handle, final, sizeof final, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    bool ok = size && size < sizeof final && windows_info(handle, info);
    if (!CloseHandle(handle)) {
        ok = false;
    }
    if (!ok) {
        return NULL;
    }
    char *start = final;
    if (!strncmp(final, "\\\\?\\UNC\\", 8)) {
        final[6] = '\\';
        start = final + 6;
    } else if (!strncmp(final, "\\\\?\\", 4)) {
        start = final + 4;
    }
    size_t length = strlen(start);
    if (length >= PACKAGE_PATH_MAX || !disk_absolute(start)) {
        return NULL;
    }
    char *canonical = xstrdup(start);
    for (char *part = canonical; *part; part++) {
        if (*part == '\\') {
            *part = '/';
        }
    }
    if (length > 3 && canonical[length - 1] == '/') {
        canonical[length - 1] = 0;
    }
    return canonical;
}

static bool add_root(package_files *files, const char *path)
{
    if (!disk_absolute(path) || strnlen(path, PACKAGE_PATH_MAX) == PACKAGE_PATH_MAX) {
        return reject(files->error, path, "Package roots must be explicit absolute directories");
    }
    windows_file_info info;
    char *canonical = windows_canonical(path, &info);
    if (!canonical) {
        return reject(files->error, path, "Cannot canonicalize package root");
    }
    if (!(info.file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        free(canonical);
        return reject(files->error, path, "Package root is not a supported directory");
    }
    for (int i = 0; i < acount(files->roots); i++) {
        if (!strcmp(files->roots[i], canonical) ||
            same_identity(&files->root_info[i], &info)) {
            free(canonical);
            return reject(files->error, path, "Duplicate physical package root");
        }
    }
    apush(files->roots, canonical);
    apush(files->root_info, info);
    return true;
}

static void close_handles(HANDLE *handles)
{
    for (int i = acount(handles) - 1; i >= 0; i--) {
        CloseHandle(handles[i]);
    }
    afree(handles);
}

/* Denying write/delete sharing pins every ancestor against replacement and
 * conversion into a reparse point while absolute child paths are opened. */
static HANDLE *open_root(const char *path)
{
    size_t prefix = 3;
    if (path[0] == '/' && path[1] == '/') {
        const char *server = strchr(path + 2, '/');
        const char *share = server ? strchr(server + 1, '/') : NULL;
        if (!server || server == path + 2 || !server[1]) {
            return NULL;
        }
        prefix = share ? (size_t)(share - path) : strlen(path);
    }
    HANDLE *handles = NULL;
    char component[PACKAGE_PATH_MAX];
    memcpy(component, path, strlen(path) + 1);
    for (size_t end = prefix;;) {
        char saved = component[end];
        component[end] = 0;
        HANDLE handle = windows_open(component, false);
        windows_file_info info;
        bool ok = handle != INVALID_HANDLE_VALUE && windows_info(handle, &info) &&
                  (info.file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                  !(info.file.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
        component[end] = saved;
        if (!ok) {
            if (handle != INVALID_HANDLE_VALUE) {
                CloseHandle(handle);
            }
            close_handles(handles);
            return NULL;
        }
        apush(handles, handle);
        if (!saved) {
            return handles;
        }
        size_t next = end + (component[end] == '/');
        while (component[next] && component[next] != '/') {
            next++;
        }
        end = next;
    }
}

static bool read_file(package_files *files, HANDLE handle, const char *path, const windows_file_info *before)
{
    uint64_t size64 = ((uint64_t)before->file.nFileSizeHigh << 32) |
                      before->file.nFileSizeLow;
    if (acount(files->files) >= SOURCE_PACKAGE_MAX_FILES) {
        return reject(files->error, path, "Package source file limit exceeded");
    }
    if (size64 > MAX_FILE_BYTES || size64 > MAX_TOTAL_BYTES - files->bytes) {
        return reject(files->error, path, "Package source byte limit exceeded");
    }
    size_t size = (size_t)size64, used = 0;
    char *text = xmalloc(size + 1);
    bool ok = true;
    while (ok && used < size) {
        DWORD count = 0;
        ok = ReadFile(handle, text + used, (DWORD)(size - used), &count, NULL) && count;
        used += count;
    }
    char extra;
    DWORD count = 0;
    windows_file_info after;
    ok = ok && ReadFile(handle, &extra, 1, &count, NULL) && !count &&
         windows_info(handle, &after) && same_file(before, &after) &&
         !memchr(text, 0, size);
    if (!ok) {
        free(text);
        return reject(files->error, path, "Package source changed during read or contains NUL");
    }
    text[size] = 0;
    apush(files->files, ((source_package_input){ .path = xstrdup(path), .text = text, .size = size }));
    files->bytes += size;
    return true;
}

static bool scan(package_files *files, HANDLE handle, const char *path)
{
    windows_file_info before, after;
    if (!windows_info(handle, &before)) {
        return reject(files->error, path, "Cannot inspect package directory");
    }
    char pattern[PACKAGE_PATH_MAX + 8];
    if (!windows_path(path, pattern) || strlen(pattern) + 2 >= sizeof pattern) {
        return reject(files->error, path, "Package path limit exceeded");
    }
    strcat(pattern, pattern[strlen(pattern) - 1] == '\\' ? "*" : "\\*");
    WIN32_FIND_DATAA entry;
    HANDLE directory = FindFirstFileA(pattern, &entry);
    bool ok = directory != INVALID_HANDLE_VALUE || GetLastError() == ERROR_FILE_NOT_FOUND;
    if (!ok) {
        return reject(files->error, path, "Cannot scan package directory");
    }
    bool pending = directory != INVALID_HANDLE_VALUE;
    while (ok && pending) {
        if (++files->entries > MAX_ENTRIES) {
            ok = reject(files->error, path, "Package directory entry limit exceeded");
            break;
        }
        if (entry.cFileName[0] != '.') {
            char child[PACKAGE_PATH_MAX];
            int length = snprintf(
                child,
                sizeof child,
                "%s%s%s",
                path,
                path[strlen(path) - 1] == '/' ? "" : "/",
                entry.cFileName
            );
            if (length < 0 || length >= (int)sizeof child) {
                ok = reject(files->error, path, "Package path limit exceeded");
                break;
            }
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                ok = reject(files->error, child, "Package descendants must not be symlinks or reparse points");
                break;
            }
            bool folder = entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
            size_t size = strlen(entry.cFileName);
            bool source = size >= 6 && !strcmp(entry.cFileName + size - 6, ".chibi");
            bool skipped = folder &&
                           (explicit_root(files, child) ||
                            (files->skip_tests && !strcmp(entry.cFileName, "tests")));
            if (!skipped && (folder || source)) {
                if (folder && !depth_valid(files, child)) {
                    ok = reject(files->error, child, "Package directory depth limit exceeded");
                    break;
                }
                HANDLE next = windows_open(child, false);
                windows_file_info opened;
                ok = next != INVALID_HANDLE_VALUE && windows_info(next, &opened) &&
                     !(opened.file.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                     opened.file.dwFileAttributes == entry.dwFileAttributes &&
                     !CompareFileTime(&opened.file.ftLastWriteTime, &entry.ftLastWriteTime) &&
                     (folder || (opened.file.nFileSizeHigh == entry.nFileSizeHigh &&
                                 opened.file.nFileSizeLow == entry.nFileSizeLow));
                if (!ok) {
                    reject(files->error, child, "Package descendant changed during open");
                } else {
                    ok = folder ? scan(files, next, child)
                                : read_file(files, next, child, &opened);
                }
                if (next != INVALID_HANDLE_VALUE && !CloseHandle(next) && ok) {
                    ok = reject(files->error, child, "Cannot close package descendant");
                }
            }
        }
        pending = FindNextFileA(directory, &entry);
        if (!pending && GetLastError() != ERROR_NO_MORE_FILES && ok) {
            ok = reject(files->error, path, "Cannot read package directory");
        }
    }
    if (directory != INVALID_HANDLE_VALUE && !FindClose(directory) && ok) {
        ok = reject(files->error, path, "Cannot close package directory");
    }
    if (ok && (!windows_info(handle, &after) || !same_file(&before, &after))) {
        ok = reject(files->error, path, "Package directory changed during scan");
    }
    return ok;
}

static bool collect(package_files *files)
{
    for (int i = 0; i < acount(files->roots); i++) {
        HANDLE *handles = open_root(files->roots[i]);
        windows_file_info opened;
        bool ok = handles && windows_info(handles[acount(handles) - 1], &opened) &&
                  same_file(&files->root_info[i], &opened);
        if (!ok) {
            reject(files->error, files->roots[i], "Cannot open canonical package root");
        } else {
            files->skip_tests = i >= files->first_collection;
            ok = scan(files, handles[acount(handles) - 1], files->roots[i]);
        }
        close_handles(handles);
        if (!ok) {
            return false;
        }
    }
    if (acount(files->files) > 1) {
        qsort(files->files, (size_t)acount(files->files), sizeof *files->files, compare_files);
    }
    return true;
}

static bool windows_explicit(const char *path, source_package_input **out, source_package_load_error *error)
{
    windows_file_info expected;
    char *canonical = path ? windows_canonical(path, &expected) : NULL;
    if (!canonical) {
        return reject(error, path, "Cannot canonicalize program source");
    }
    char *parent = xstrdup(canonical);
    char *slash = strrchr(parent, '/');
    assert(slash);
    slash[slash == parent || (slash == parent + 2 && parent[1] == ':') ? 1 : 0] = 0;
    HANDLE *handles = open_root(parent);
    free(parent);
    HANDLE handle = handles ? windows_open(canonical, false) : INVALID_HANDLE_VALUE;
    windows_file_info opened;
    package_files files = { .error = error };
    bool ok = handle != INVALID_HANDLE_VALUE && windows_info(handle, &opened) &&
              !(opened.file.dwFileAttributes &
                (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
              same_file(&expected, &opened);
    if (!ok) {
        reject(error, canonical, "Program source is not a readable regular file");
    } else {
        ok = read_file(&files, handle, canonical, &opened);
    }
    if (handle != INVALID_HANDLE_VALUE && !CloseHandle(handle) && ok) {
        ok = reject(error, canonical, "Cannot close program source");
    }
    close_handles(handles);
    if (ok) {
        *out = files.files;
        files.files = NULL;
    }
    free_files(&files);
    free(canonical);
    return ok;
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
    /* Its one root is a collection: the build host's and the core library's. */
    package_files found = { .error = error, .first_collection = 0 };
    bool ok = add_root(&found, root) && collect(&found);
    if (ok) {
        *files = found.files;
        found.files = NULL;
    }
    free_files(&found);
    return ok;
}

bool source_package_file_read(const char *path, source_package_input **files, source_package_load_error *error)
{
    assert(files && !*files && error);
    *error = (source_package_load_error){ 0 };
#ifdef _WIN32
    return windows_explicit(path, files, error);
#else
    char *canonical = realpath(path, NULL);
    if (!canonical) {
        return reject(error, path, "Cannot canonicalize program source");
    }
    if (strlen(canonical) >= PACKAGE_PATH_MAX) {
        free(canonical);
        return reject(error, path, "Program source path limit exceeded");
    }
    char *parent = xstrdup(canonical);
    char *slash = strrchr(parent, '/');
    assert(slash);
    slash[slash == parent ? 1 : 0] = 0;
    int directory = open_root(parent);
    free(parent);
    const char *name = strrchr(canonical, '/') + 1;
    struct stat info;
    package_files found = { .error = error };
    bool ok = directory >= 0 &&
              !fstatat(directory, name, &info, AT_SYMLINK_NOFOLLOW) &&
              S_ISREG(info.st_mode);
    if (ok) {
        ok = read_file(&found, directory, name, canonical, &info);
    } else {
        reject(error, canonical, "Program source is not a readable regular file");
    }
    if (directory >= 0) {
        close(directory);
    }
    if (ok) {
        *files = found.files;
        found.files = NULL;
    }
    free_files(&found);
    free(canonical);
    return ok;
#endif
}

bool source_package_files_load(
    const source_package_disk_options *options,
    source_package_snapshot_options *inputs,
    source_package_load_error *error
)
{
    assert(inputs && !inputs->project_root && !inputs->files && !inputs->collections && error);
    *error = (source_package_load_error){ 0 };
    if (!options || options->collection_count < 0 ||
        options->collection_count >= SOURCE_PACKAGE_MAX_FILES ||
        (options->collection_count && !options->collections)) {
        reject(error, "", "Invalid package filesystem options");
        return false;
    }
    package_files files = { .error = error, .first_collection = 1 };
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
    ok = ok && collect(&files);
    if (ok) {
        source_package_collection
            *collections = xcalloc((size_t)options->collection_count, sizeof *collections);
        for (int i = 0; i < options->collection_count; i++) {
            collections[i] = (source_package_collection){
                xstrdup(options->collections[i].name),
                xstrdup(files.roots[i + 1])
            };
        }
        *inputs = (source_package_snapshot_options){
            .project_root = xstrdup(files.roots[0]),
            .files = files.files,
            .file_count = acount(files.files),
            .collections = collections,
            .collection_count = options->collection_count,
        };
        files.files = NULL;
    }
    free_files(&files);
    return ok;
}

void source_package_files_release(source_package_snapshot_options *inputs)
{
    free((char *)inputs->project_root);
    source_package_files_free((source_package_input *)inputs->files);
    for (int i = 0; i < inputs->collection_count; i++) {
        free((char *)inputs->collections[i].name);
        free((char *)inputs->collections[i].root);
    }
    free((source_package_collection *)inputs->collections);
    *inputs = (source_package_snapshot_options){ 0 };
}
