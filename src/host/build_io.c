#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "host/build_io.h"

#include "host/process.h"
#include "support/xalloc.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BUILD_FILE_LIMIT (1024 * 1024)
#define BUILD_IO_CHUNK 4096

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
    char *joined = build_io_path(root, path);
    size_t temporary_size = strlen(joined) + sizeof ".XXXXXX";
    char *temporary = xmalloc(temporary_size);
    snprintf(temporary, temporary_size, "%s.XXXXXX", joined);
    int fd = mkstemp(temporary);
    bool ok = fd >= 0;
    if (ok) {
        ok = !fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
    size_t written = 0;
    while (ok && written < size) {
        if (io_ready(deadline, cancelled)) {
            ok = false;
            break;
        }
        size_t remaining = size - written;
        size_t chunk = remaining < BUILD_IO_CHUNK ? remaining : BUILD_IO_CHUNK;
        ssize_t count = write(fd, text + written, chunk);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            ok = false;
            break;
        }
        written += (size_t)count;
    }
    if (fd >= 0 && close(fd)) {
        ok = false;
    }
    ok = ok && !io_ready(deadline, cancelled) && !rename(temporary, joined);
    if (!ok && fd >= 0) {
        unlink(temporary);
    }
    free(temporary);
    free(joined);
    return ok;
}
