#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "host/build_io.h"

#include "host/build_values.h"
#include "host/process.h"
#include "support/array.h"
#include "support/sha256.h"
#include "support/xalloc.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BUILD_FILE_LIMIT (1024 * 1024)
#define BUILD_IO_CHUNK 4096
#define BUILD_IO_COPY_CHUNK 65536
#define BUILD_IO_LOCK_POLL_NS 10000000

static const char *io_ready(double deadline, const volatile sig_atomic_t *cancelled)
{
    if (cancelled && *cancelled) {
        return "build process cancelled";
    }
    double now = chibi_process_now();
    if (now < 0) {
        return "build file clock failed";
    }
    if (!(deadline > now)) {
        return "build process timeout";
    }
    return NULL;
}

char *build_io_path(const char *root, const char *path)
{
    assert(root && root[0] == '/' && path);
    if (path[0] == '/') {
        return xstrdup(path);
    }
    size_t size = strlen(root) + strlen(path) + 2;
    char *joined = xmalloc(size);
    snprintf(joined, size, "%s/%s", root, path);
    return joined;
}

char *build_io_resolve(const char *root, const char *path)
{
    char *joined = build_io_path(root, path);
    char *canonical = realpath(joined, NULL);
    free(joined);
    return canonical;
}

bool build_io_exists(const char *root, const char *path)
{
    char *joined = build_io_path(root, path);
    struct stat info;
    bool exists = !stat(joined, &info);
    free(joined);
    return exists;
}

bool build_io_mkdir(const char *root, const char *path)
{
    char *joined = build_io_path(root, path);
    bool ok = true;
    for (char *part = joined + 1; *part && ok; part++) {
        if (*part != '/') {
            continue;
        }
        *part = 0;
        ok = !mkdir(joined, 0755) || errno == EEXIST;
        *part = '/';
    }
    if (ok) {
        ok = !mkdir(joined, 0755) || errno == EEXIST;
    }
    struct stat info;
    ok = ok && !stat(joined, &info) && S_ISDIR(info.st_mode) &&
         !access(joined, W_OK | X_OK);
    free(joined);
    return ok;
}

bool build_io_read(
    const char *root,
    const char *path,
    double deadline,
    const volatile sig_atomic_t *cancelled,
    char **out,
    const char **error
)
{
    assert(out && error);
    *out = NULL;
    *error = io_ready(deadline, cancelled);
    if (*error) {
        return false;
    }
    char *joined = build_io_path(root, path);
    int fd = open(joined, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
    int open_error = errno;
    free(joined);
    if (fd < 0) {
        if (open_error == ENOENT) {
            *out = xstrdup("");
            return true;
        }
        *error = "build file read failed";
        return false;
    }
    struct stat info;
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size < 0 ||
        info.st_size > BUILD_FILE_LIMIT) {
        close(fd);
        *error = "build file must be regular text of at most 1 MiB";
        return false;
    }
    char *text = xmalloc(BUILD_FILE_LIMIT + 1);
    size_t size = 0;
    for (;;) {
        *error = io_ready(deadline, cancelled);
        if (*error) {
            break;
        }
        size_t remaining = BUILD_FILE_LIMIT + 1 - size;
        size_t chunk = remaining < BUILD_IO_CHUNK ? remaining : BUILD_IO_CHUNK;
        ssize_t count = read(fd, text + size, chunk);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            *error = "build file read failed";
            break;
        }
        if (!count) {
            break;
        }
        if (memchr(text + size, 0, (size_t)count)) {
            *error = "build file contains NUL bytes";
            break;
        }
        size += (size_t)count;
        if (size > BUILD_FILE_LIMIT) {
            *error = "build file exceeds 1 MiB";
            break;
        }
    }
    if (close(fd) && !*error) {
        *error = "build file read failed";
    }
    if (*error) {
        free(text);
        return false;
    }
    text[size] = 0;
    *out = text;
    return true;
}

/* Nothing is at a path whose lookup failed this way. */
static bool missing(int error)
{
    return error == ENOENT || error == ENOTDIR;
}

/* An open temporary beside path, closed on exec, or -1. */
static int temporary_file(const char *path, char **temporary)
{
    size_t size = strlen(path) + sizeof ".XXXXXX";
    *temporary = xmalloc(size);
    snprintf(*temporary, size, "%s.XXXXXX", path);
    int fd = mkstemp(*temporary);
    if (fd >= 0 && fcntl(fd, F_SETFD, FD_CLOEXEC)) {
        close(fd);
        unlink(*temporary);
        fd = -1;
    }
    if (fd < 0) {
        free(*temporary);
    }
    return fd;
}

static bool write_all(int fd, const char *data, size_t size, double deadline, const volatile sig_atomic_t *cancelled)
{
    size_t written = 0;
    while (written < size) {
        if (io_ready(deadline, cancelled)) {
            return false;
        }
        size_t remaining = size - written;
        size_t chunk = remaining < BUILD_IO_CHUNK ? remaining : BUILD_IO_CHUNK;
        ssize_t count = write(fd, data + written, chunk);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return false;
        }
        written += (size_t)count;
    }
    return true;
}

/* Closes a complete temporary and renames it over path, or removes it.
 * Frees temporary. */
static bool publish(
    int fd,
    bool complete,
    char *temporary,
    const char *path,
    double deadline,
    const volatile sig_atomic_t *cancelled
)
{
    bool ok = !close(fd) && complete && !io_ready(deadline, cancelled) &&
              !rename(temporary, path);
    if (!ok) {
        unlink(temporary);
    }
    free(temporary);
    return ok;
}

bool build_io_write(
    const char *root,
    const char *path,
    const char *text,
    double deadline,
    const volatile sig_atomic_t *cancelled
)
{
    assert(text);
    size_t size = strnlen(text, BUILD_FILE_LIMIT + 1);
    if (size > BUILD_FILE_LIMIT || io_ready(deadline, cancelled)) {
        return false;
    }
    char *joined = build_io_path(root, path), *temporary;
    int fd = temporary_file(joined, &temporary);
    bool ok = fd >= 0 &&
              publish(fd, write_all(fd, text, size, deadline, cancelled), temporary, joined, deadline, cancelled);
    free(joined);
    return ok;
}

const char *build_io_kind(const char *root, const char *path, const char **kind)
{
    char *joined = build_io_path(root, path);
    struct stat info;
    int failed = lstat(joined, &info), error = errno;
    free(joined);
    *kind = "";
    if (failed) {
        return missing(error) ? NULL : "build file metadata failed";
    }
    *kind = S_ISREG(info.st_mode) ? "file"
          : S_ISDIR(info.st_mode) ? "directory"
          : S_ISLNK(info.st_mode) ? "link"
                                  : "other";
    return NULL;
}

const char *build_io_stamp(const char *root, const char *path, char stamp[BUILD_IO_STAMP_SIZE])
{
    char *joined = build_io_path(root, path);
    struct stat info;
    int failed = stat(joined, &info), error = errno;
    free(joined);
    stamp[0] = 0;
    if (failed) {
        return missing(error) ? NULL : "build file metadata failed";
    }
#ifdef __APPLE__
    struct timespec modified = info.st_mtimespec, changed = info.st_ctimespec;
#else
    struct timespec modified = info.st_mtim, changed = info.st_ctim;
#endif
    snprintf(
        stamp,
        BUILD_IO_STAMP_SIZE,
        "%ju:%ju:%jd:%o:%jd.%09ld:%jd.%09ld",
        (uintmax_t)info.st_dev,
        (uintmax_t)info.st_ino,
        (intmax_t)info.st_size,
        (unsigned)info.st_mode,
        (intmax_t)modified.tv_sec,
        (long)modified.tv_nsec,
        (intmax_t)changed.tv_sec,
        (long)changed.tv_nsec
    );
    return NULL;
}

static int compare_names(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

const char *build_io_list(
    const char *root,
    const char *path,
    double deadline,
    const volatile sig_atomic_t *cancelled,
    char ***names
)
{
    *names = NULL;
    const char *error = io_ready(deadline, cancelled);
    if (error) {
        return error;
    }
    char *joined = build_io_path(root, path);
    DIR *directory = opendir(joined);
    int open_error = errno;
    free(joined);
    if (!directory) {
        return open_error == ENOENT ? NULL : "build directory list failed";
    }
    size_t bytes = 0;
    while (!(error = io_ready(deadline, cancelled))) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (!entry) {
            error = errno ? "build directory list failed" : NULL;
            break;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        size_t size = strlen(entry->d_name) + 1;
        if (acount(*names) == BUILD_ARRAY_LIMIT) {
            error = "build array limit exceeded";
            break;
        }
        if (size > BUILD_TEXT_LIMIT - bytes) {
            error = "build text limit exceeded";
            break;
        }
        bytes += size;
        apush(*names, xstrdup(entry->d_name));
    }
    closedir(directory);
    if (error) {
        for (int i = 0; i < acount(*names); i++) {
            free((*names)[i]);
        }
        afree(*names);
        *names = NULL;
        return error;
    }
    if (acount(*names)) {
        qsort(*names, (size_t)acount(*names), sizeof **names, compare_names);
    }
    return NULL;
}

bool build_io_copy(
    const char *root,
    const char *source,
    const char *destination,
    double deadline,
    const volatile sig_atomic_t *cancelled
)
{
    if (io_ready(deadline, cancelled)) {
        return false;
    }
    char *from = build_io_path(root, source);
    int in = open(from, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
    free(from);
    if (in < 0) {
        return false;
    }
    struct stat info;
    char *to = build_io_path(root, destination), *temporary = NULL;
    int out = fstat(in, &info) || !S_ISREG(info.st_mode)
                ? -1
                : temporary_file(to, &temporary);
    bool complete = out >= 0 && !fchmod(out, info.st_mode & 0777);
    char *buffer = xmalloc(BUILD_IO_COPY_CHUNK);
    while (complete) {
        if (io_ready(deadline, cancelled)) {
            complete = false;
            break;
        }
        ssize_t count = read(in, buffer, BUILD_IO_COPY_CHUNK);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            complete = !count;
            break;
        }
        complete = write_all(out, buffer, (size_t)count, deadline, cancelled);
    }
    free(buffer);
    complete = !close(in) && complete;
    bool ok = out >= 0 && publish(out, complete, temporary, to, deadline, cancelled);
    free(to);
    return ok;
}

bool build_io_rename(const char *root, const char *source, const char *destination)
{
    char *from = build_io_path(root, source), *to = build_io_path(root, destination);
    bool ok = !rename(from, to);
    free(from);
    free(to);
    return ok;
}

bool build_io_remove(const char *root, const char *path)
{
    char *joined = build_io_path(root, path);
    struct stat info;
    bool ok = lstat(joined, &info)  ? missing(errno)
            : S_ISDIR(info.st_mode) ? !rmdir(joined)
                                    : !unlink(joined);
    free(joined);
    return ok;
}

const char *build_io_hash(
    const char *root,
    const char *path,
    double deadline,
    const volatile sig_atomic_t *cancelled,
    char digest[65]
)
{
    digest[0] = 0;
    const char *error = io_ready(deadline, cancelled);
    if (error) {
        return error;
    }
    char *joined = build_io_path(root, path);
    int fd = open(joined, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
    int open_error = errno;
    free(joined);
    if (fd < 0) {
        return missing(open_error) ? NULL : "build file hash failed";
    }
    struct stat info;
    if (fstat(fd, &info) || !S_ISREG(info.st_mode)) {
        close(fd);
        return "build file hash needs a regular file";
    }
    sha256 hash;
    sha256_begin(&hash);
    unsigned char *buffer = xmalloc(BUILD_IO_COPY_CHUNK);
    while (!(error = io_ready(deadline, cancelled))) {
        ssize_t count = read(fd, buffer, BUILD_IO_COPY_CHUNK);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            error = "build file hash failed";
        }
        if (count <= 0) {
            break;
        }
        sha256_add(&hash, buffer, (size_t)count);
    }
    free(buffer);
    if (close(fd) && !error) {
        error = "build file hash failed";
    }
    if (!error) {
        sha256_end(&hash, digest);
    }
    return error;
}

static int held_lock(const build_io_locks *locks, const struct stat *info)
{
    for (int i = 0; i < locks->count; i++) {
        struct stat held;
        if (!fstat(locks->fds[i], &held) && held.st_dev == info->st_dev &&
            held.st_ino == info->st_ino) {
            return i;
        }
    }
    return -1;
}

const char *build_io_lock(
    build_io_locks *locks,
    const char *root,
    const char *path,
    double deadline,
    const volatile sig_atomic_t *cancelled,
    bool *locked
)
{
    *locked = false;
    const char *error = io_ready(deadline, cancelled);
    if (error) {
        return error;
    }
    if (locks->count == BUILD_IO_LOCKS) {
        return "build lock limit exceeded";
    }
    char *joined = build_io_path(root, path);
    int fd = open(joined, O_RDONLY | O_CREAT | O_NONBLOCK | O_CLOEXEC | O_NOCTTY, 0644);
    free(joined);
    struct stat info;
    if (fd < 0) {
        return NULL;
    }
    if (fstat(fd, &info) || !S_ISREG(info.st_mode)) {
        close(fd);
        return NULL;
    }
    if (held_lock(locks, &info) >= 0) {
        close(fd);
        return "build lock is already held";
    }
    while (flock(fd, LOCK_EX | LOCK_NB)) {
        if (errno != EWOULDBLOCK && errno != EINTR) {
            close(fd);
            return NULL;
        }
        error = io_ready(deadline, cancelled);
        if (error) {
            close(fd);
            return error;
        }
        struct timespec pause = { .tv_nsec = BUILD_IO_LOCK_POLL_NS };
        nanosleep(&pause, NULL);
    }
    locks->fds[locks->count++] = fd;
    *locked = true;
    return NULL;
}

bool build_io_unlock(build_io_locks *locks, const char *root, const char *path)
{
    char *joined = build_io_path(root, path);
    struct stat info;
    int index = stat(joined, &info) ? -1 : held_lock(locks, &info);
    free(joined);
    if (index < 0) {
        return false;
    }
    close(locks->fds[index]);
    locks->fds[index] = locks->fds[--locks->count];
    return true;
}

void build_io_unlock_all(build_io_locks *locks)
{
    for (int i = 0; i < locks->count; i++) {
        close(locks->fds[i]);
    }
    locks->count = 0;
}
