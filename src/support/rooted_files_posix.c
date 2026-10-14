#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "support/rooted_files_internal.h"
#include "support/xalloc.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <sys/syscall.h>
#endif
#include <unistd.h>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

static rooted_files *wrap(int fd, bool directory)
{
    if (fd < 0) {
        return NULL;
    }
    rooted_files *file = xmalloc(sizeof *file);
    *file = (rooted_files){ .native = fd, .directory = directory };
    return file;
}
bool rooted_native_name_valid(const char *name)
{
    if (!*name || strlen(name) > NAME_MAX) {
        errno = ENAMETOOLONG;
        return false;
    }
    return true;
}
rooted_files *rooted_native_root(const char *path)
{
    return wrap(open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC), true);
}
char *rooted_files_host_path(rooted_files *file)
{
#if defined(__APPLE__)
    char path[4096];
    if (fcntl((int)file->native, F_GETPATH, path)) {
        return NULL;
    }
    return xstrdup(path);
#else
    char source[64], path[4097];
    snprintf(source, sizeof source, "/proc/self/fd/%d", (int)file->native);
    ssize_t length = readlink(source, path, sizeof path - 1);
    if (length < 0) {
        return NULL;
    }
    if ((size_t)length >= sizeof path - 1) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    path[length] = 0;
    return xstrdup(path);
#endif
}
rooted_files *rooted_native_duplicate(rooted_files *file)
{
    return wrap(dup((int)file->native), file->directory);
}
static void info(const struct stat *in, rooted_file_info *out)
{
    *out = (rooted_file_info){ .device = (uint64_t)in->st_dev,
                               .identity = { (uint64_t)in->st_ino, 0 },
                               .bytes = in->st_size < 0 ? 0 : (uint64_t)in->st_size,
                               .modified_seconds = in->st_mtime,
                               .changed_seconds = in->st_ctime,
                               .permissions = in->st_mode & 0777,
                               .regular = S_ISREG(in->st_mode),
                               .directory = S_ISDIR(in->st_mode),
                               .link = S_ISLNK(in->st_mode) };
#if defined(__APPLE__)
    out->modified_nanoseconds = (int)in->st_mtimespec.tv_nsec;
    out->changed_nanoseconds = (int)in->st_ctimespec.tv_nsec;
#else
    out->modified_nanoseconds = (int)in->st_mtim.tv_nsec;
    out->changed_nanoseconds = (int)in->st_ctim.tv_nsec;
#endif
}
rooted_files *rooted_native_child(
    rooted_files *parent,
    const char *name,
    bool directory,
    rooted_create create,
    unsigned permissions
)
{
    assert(parent && parent->directory);
    if (directory && create && mkdirat((int)parent->native, name, (mode_t)permissions) &&
        (errno != EEXIST || create == ROOTED_CREATE_EXCLUSIVE)) {
        return NULL;
    }
    int flags = O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK;
    if (directory) {
        flags |= O_DIRECTORY | O_RDONLY;
    } else {
        flags |= create ? O_WRONLY | O_CREAT | O_EXCL : O_RDONLY;
    }
    int fd = openat((int)parent->native, name, flags, (mode_t)permissions);
    if (fd < 0) {
        return NULL;
    }
    struct stat status;
    if (fstat(fd, &status)) {
        int error = errno;
        close(fd);
        errno = error;
        return NULL;
    }
    if (directory ? !S_ISDIR(status.st_mode) : !S_ISREG(status.st_mode)) {
        close(fd);
        errno = directory ? ENOTDIR : EINVAL;
        return NULL;
    }
    return wrap(fd, directory);
}
bool rooted_native_stat(rooted_files *parent, const char *name, rooted_file_info *out)
{
    struct stat status;
    if (fstatat((int)parent->native, name, &status, AT_SYMLINK_NOFOLLOW)) {
        return false;
    }
    info(&status, out);
    return true;
}
rooted_stream *rooted_stream_external(const char *path)
{
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        return NULL;
    }
    struct stat status;
    if (fstat(fd, &status) || !S_ISREG(status.st_mode)) {
        close(fd);
        errno = EINVAL;
        return NULL;
    }
    return wrap(fd, false);
}
bool rooted_stream_finish(rooted_stream *file)
{
    assert(file);
    bool ok = close((int)file->native) == 0;
    free(file);
    return ok;
}
void rooted_stream_close(rooted_stream *file)
{
    rooted_stream_finish(file);
}
void rooted_files_close(rooted_files *file)
{
    rooted_stream_close(file);
}
int64_t rooted_stream_read(rooted_stream *file, void *out, size_t size)
{
    assert(file && !file->directory);
    return read((int)file->native, out, size);
}
int64_t rooted_stream_write(rooted_stream *file, const void *data, size_t size)
{
    assert(file && !file->directory);
    return write((int)file->native, data, size);
}
bool rooted_stream_flush(rooted_stream *file)
{
    return fsync((int)file->native) == 0;
}
bool rooted_stream_info(rooted_stream *file, rooted_file_info *out)
{
    struct stat status;
    if (fstat((int)file->native, &status)) {
        return false;
    }
    info(&status, out);
    return true;
}
bool rooted_native_permissions(rooted_stream *file, unsigned permissions)
{
    return fchmod((int)file->native, (mode_t)permissions) == 0;
}
bool rooted_native_list(rooted_files *parent, int limit, rooted_file_entry **out, int *count)
{
    *out = NULL;
    *count = 0;
    int fd = openat((int)parent->native, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    DIR *directory = fd < 0 ? NULL : fdopendir(fd);
    if (!directory) {
        if (fd >= 0) {
            close(fd);
        }
        return false;
    }
    bool ok = true;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (!entry) {
            if (errno) {
                ok = false;
            }
            break;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        if (*count >= limit) {
            errno = EFBIG;
            ok = false;
            break;
        }
        rooted_file_info status;
        if (!rooted_native_stat(parent, entry->d_name, &status)) {
            ok = false;
            break;
        }
        *out = xrealloc(*out, (size_t)(*count + 1) * sizeof **out);
        (*out)[*count] = (rooted_file_entry){ .name = xstrdup(entry->d_name),
                                              .info = status };
        (*count)++;
    }
    int error = errno;
    closedir(directory);
    if (!ok) {
        rooted_file_entries_free(*out, *count);
        *out = NULL;
        *count = 0;
    }
    errno = error;
    return ok;
}
bool rooted_native_move(rooted_files *from, const char *old, rooted_files *to, const char *name, bool replace)
{
    if (replace) {
        return renameat((int)from->native, old, (int)to->native, name) == 0;
    }
#if defined(__APPLE__)
    return renameatx_np((int)from->native, old, (int)to->native, name, RENAME_EXCL) == 0;
#elif defined(__linux__) && !defined(__EMSCRIPTEN__) && defined(SYS_renameat2)
    return syscall(SYS_renameat2, (int)from->native, old, (int)to->native, name, 1u) == 0;
#elif defined(__EMSCRIPTEN__)
    /* MEMFS is synchronous and has no external writers during this call. */
    struct stat target;
    if (!fstatat((int)to->native, name, &target, AT_SYMLINK_NOFOLLOW)) {
        errno = EEXIST;
        return false;
    }
    if (errno != ENOENT) {
        return false;
    }
    return renameat((int)from->native, old, (int)to->native, name) == 0;
#else
    if (linkat((int)from->native, old, (int)to->native, name, 0)) {
        return false;
    }
    if (!unlinkat((int)from->native, old, 0)) {
        return true;
    }
    int error = errno;
    unlinkat((int)to->native, name, 0);
    errno = error;
    return false;
#endif
}
bool rooted_native_remove(rooted_files *parent, const char *name)
{
    return unlinkat((int)parent->native, name, 0) == 0;
}
bool rooted_native_remove_directory(rooted_files *parent, const char *name)
{
    return unlinkat((int)parent->native, name, AT_REMOVEDIR) == 0;
}
#endif

bool rooted_native_name_equal(rooted_files *parent, const char *left, const char *right)
{
#ifdef __APPLE__
    {
        CFOptionFlags options = kCFCompareNonliteral;
        if (fpathconf((int)parent->native, _PC_CASE_SENSITIVE) == 0) {
            options |= kCFCompareCaseInsensitive;
        }
        CFStringRef a = CFStringCreateWithCString(NULL, left, kCFStringEncodingUTF8);
        CFStringRef b = CFStringCreateWithCString(NULL, right, kCFStringEncodingUTF8);
        bool equal = a && b && CFStringCompare(a, b, options) == kCFCompareEqualTo;
        if (a) {
            CFRelease(a);
        }
        if (b) {
            CFRelease(b);
        }
        return equal;
    }
#else
    (void)parent;
#endif
    return !strcmp(left, right);
}
