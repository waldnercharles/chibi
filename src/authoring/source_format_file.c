#ifdef __APPLE__
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "authoring/source_format_file.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum
{
    MAX_BYTES = 8 * 1024 * 1024,
    MAX_TOTAL_BYTES = 256 * 1024 * 1024,
    MAX_CONFIG = 4096,
    MAX_FILES = 65536,
    MAX_DEPTH = 64
};

/* realpath uses forward slashes on every host through Chibi's Windows shim. */
static size_t root_length(const char *path)
{
#ifdef _WIN32
    if (isalpha((unsigned char)path[0]) && path[1] == ':' && path[2] == '/') {
        return 3;
    }
    if (path[0] == '/' && path[1] == '/') {
        const char *server = strchr(path + 2, '/');
        if (server) {
            const char *share = strchr(server + 1, '/');
            return share ? (size_t)(share - path) : strlen(path);
        }
    }
#endif
    return path[0] == '/' ? 1 : 0;
}

static char *join(const char *a, const char *b)
{
    size_t size = strlen(a) + strlen(b) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", a, b);
    return path;
}

static bool fail(char *error, size_t size, const char *path, const char *message)
{
    if (size) {
        snprintf(error, size, "%s: %s", path, message);
    }
    return false;
}

/* Reject links, non-regular files, NULs and changing/oversized input. */
static char *read_text(const char *path, int limit, char *error, size_t size)
{
    struct stat before, after;
    if (lstat(path, &before) || !S_ISREG(before.st_mode)) {
        fail(error, size, path, "expected a regular file, not a symlink or special file");
        return NULL;
    }
    int flags = O_RDONLY | O_NOFOLLOW;
#ifndef _WIN32
    flags |= O_NONBLOCK;
#endif
    int fd = open(path, flags);
    if (fd < 0 || fstat(fd, &before) || !S_ISREG(before.st_mode) ||
        before.st_size < 0 || before.st_size > limit) {
        if (fd >= 0) {
            close(fd);
        }
        fail(error, size, path, "cannot read regular file within formatter limit");
        return NULL;
    }
    size_t length = (size_t)before.st_size, used = 0;
    char *text = xmalloc(length + 1);
    while (used < length) {
        ssize_t n = read(fd, text + used, length - used);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        used += (size_t)n;
    }
    char extra;
    bool ok = used == length && read(fd, &extra, 1) == 0 &&
              !fstat(fd, &after) && before.st_size == after.st_size &&
              before.st_mtime == after.st_mtime && !memchr(text, 0, length);
    ok = !close(fd) && ok;
    if (!ok) {
        free(text);
        fail(error, size, path, "source changed during read or contains NUL");
        return NULL;
    }
    text[length] = 0;
    return text;
}

static char *trim(char *text)
{
    while (isspace((unsigned char)*text)) {
        text++;
    }
    size_t n = strlen(text);
    while (n && isspace((unsigned char)text[n - 1])) {
        text[--n] = 0;
    }
    return text;
}

static bool config_read(const char *path, authoring_format_options *options, char *error, size_t size)
{
    char *text = read_text(path, MAX_CONFIG, error, size);
    if (!text) {
        return false;
    }
    unsigned seen = 0;
    bool ok = true;
    int line = 0;
    for (char *part = text; part && ok;) {
        char *next = strchr(part, '\n');
        if (next) {
            *next++ = 0;
        }
        line++;
        char *comment = strchr(part, '#');
        if (comment) {
            *comment = 0;
        }
        char *key = trim(part), *equal = strchr(key, '=');
        part = next;
        if (!*key) {
            continue;
        }
        if (!equal) {
            ok = false;
            break;
        }
        *equal = 0;
        key = trim(key);
        char *value = trim(equal + 1), *end = NULL;
        unsigned bit = !strcmp(key, "indent_width")               ? 1u
                     : !strcmp(key, "preferred_width")            ? 2u
                     : !strcmp(key, "use_tabs")                   ? 4u
                     : !strcmp(key, "remove_optional_semicolons") ? 8u
                                                                  : 0u;
        if (!bit || (seen & bit)) {
            ok = false;
            break;
        }
        seen |= bit;
        if (bit <= 2) {
            errno = 0;
            long number = strtol(value, &end, 10);
            if (!*value || *end || errno || number < (bit == 1 ? 1 : 0) ||
                number > (bit == 1 ? 16 : 4096)) {
                ok = false;
                break;
            }
            if (bit == 1) {
                options->indent_width = (int)number;
            }
            if (bit == 2) {
                options->preferred_width = (int)number;
            }
        } else {
            if (strcmp(value, "true") && strcmp(value, "false")) {
                ok = false;
                break;
            }
            if (bit == 4) {
                options->use_tabs = !strcmp(value, "true");
            }
            if (bit == 8) {
                options->remove_optional_semicolons = !strcmp(value, "true");
            }
        }
    }
    if (!ok && size) {
        snprintf(error, size, "%s:%d: unknown, duplicate or invalid formatter setting", path, line);
    }
    free(text);
    return ok;
}

bool authoring_format_options_for_file(
    const char *source,
    const char *config,
    authoring_format_options *options,
    char *error,
    size_t size
)
{
    *options = authoring_format_defaults();
    if (size) {
        error[0] = 0;
    }
    if (config) {
        return config_read(config, options, error, size);
    }
    char *parent = xstrdup(source);
#ifdef _WIN32
    for (char *c = parent; *c; c++) {
        if (*c == '\\') {
            *c = '/';
        }
    }
    if (isalpha((unsigned char)parent[0]) && parent[1] == ':' && parent[2] != '/') {
        free(parent);
        return fail(error, size, source, "use an absolute drive path, not a drive-relative filename");
    }
#endif
    char *slash = strrchr(parent, '/');
    if (slash) {
        size_t root = root_length(parent);
        if ((size_t)(slash - parent) < root) {
            parent[root] = 0;
        } else {
            *slash = 0;
        }
    } else {
        free(parent);
        parent = xstrdup(".");
    }
    char *directory = realpath(parent, NULL);
    free(parent);
    if (!directory) {
        return fail(error, size, source, "cannot resolve source directory");
    }
    bool ok = true;
    for (;;) {
        char *candidate = join(directory, ".chibi-format");
        struct stat info;
        int status = lstat(candidate, &info), saved = errno;
        if (!status) {
            ok = config_read(candidate, options, error, size);
            free(candidate);
            break;
        }
        free(candidate);
        if (saved != ENOENT) {
            ok = fail(error, size, directory, "cannot inspect formatter config");
            break;
        }
        char *boundary = join(directory, ".git");
        status = lstat(boundary, &info);
        saved = errno;
        free(boundary);
        if (!status || strlen(directory) <= root_length(directory)) {
            break;
        }
        if (saved != ENOENT) {
            ok = fail(error, size, directory, "cannot inspect repository boundary");
            break;
        }
        slash = strrchr(directory, '/');
        size_t root = root_length(directory);
        if ((size_t)(slash - directory) < root) {
            directory[root] = 0;
        } else {
            *slash = 0;
        }
    }
    free(directory);
    return ok;
}

char *authoring_format_file_text(
    const char *source,
    const char *text,
    const char *config,
    char *error,
    size_t size
)
{
    authoring_format_options options;
    if (!authoring_format_options_for_file(source, config, &options, error, size)) {
        return NULL;
    }
    if (strlen(text) > MAX_BYTES) {
        fail(error, size, source, "formatter byte limit exceeded");
        return NULL;
    }
    script_syntax *syntax = script_parse(text);
    char *formatted = authoring_source_format_with_options(syntax, &options, NULL, error, size);
    script_syntax_free(syntax);
    return formatted;
}

static bool source_path(const char *path)
{
    size_t n = strlen(path);
    return n >= 6 && !strcmp(path + n - 6, ".chibi");
}

static bool collect(const char *path, int depth, char ***paths, char *error, size_t size)
{
    struct stat info;
    if (lstat(path, &info)) {
        return fail(error, size, path, "cannot inspect path");
    }
    if (S_ISREG(info.st_mode)) {
        if (!source_path(path)) {
            return depth ? true : fail(error, size, path, "expected a .chibi file");
        }
        if (acount(*paths) == MAX_FILES) {
            return fail(error, size, path, "formatter file limit exceeded");
        }
        apush(*paths, xstrdup(path));
        return true;
    }
    if (!S_ISDIR(info.st_mode)) {
        return fail(error, size, path, "formatter paths must not be symlinks or special files");
    }
    if (depth >= MAX_DEPTH) {
        return fail(error, size, path, "formatter directory depth limit exceeded");
    }
    DIR *dir = opendir(path);
    if (!dir) {
        return fail(error, size, path, "cannot read directory");
    }
    bool ok = true;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno) {
                ok = fail(error, size, path, "cannot read directory");
            }
            break;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") ||
            !strcmp(entry->d_name, ".git") || !strcmp(entry->d_name, ".chibi")) {
            continue;
        }
        char *child = join(path, entry->d_name);
        ok = collect(child, depth + 1, paths, error, size);
        free(child);
        if (!ok) {
            break;
        }
    }
    if (closedir(dir)) {
        ok = fail(error, size, path, "cannot close directory");
    }
    return ok;
}

static int compare(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static bool write_text(const char *path, const char *before, const char *after)
{
    struct stat info;
    if (lstat(path, &info) || !S_ISREG(info.st_mode)) {
        return false;
    }
    size_t n = strlen(path) + sizeof ".format-XXXXXX";
    char *temporary = xmalloc(n);
    snprintf(temporary, n, "%s.format-XXXXXX", path);
    int fd = mkstemp(temporary);
    if (fd < 0) {
        free(temporary);
        return false;
    }
    bool ok = !fchmod(fd, info.st_mode & 0777);
    size_t length = strlen(after), used = 0;
    while (ok && used < length) {
        ssize_t count = write(fd, after + used, length - used);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        ok = count > 0;
        if (ok) {
            used += (size_t)count;
        }
    }
    ok = !close(fd) && ok;
    /* Do not replace concurrent edits; failed writes leave the original intact. */
    char *current = ok ? read_text(path, MAX_BYTES, NULL, 0) : NULL;
    ok = current && !strcmp(current, before);
    free(current);
    if (ok) {
        ok = !rename(temporary, path);
    }
    if (!ok) {
        unlink(temporary);
    }
    free(temporary);
    return ok;
}

int authoring_format_path(const char *path, const char *config, bool check)
{
    char error[512] = { 0 };
    char **paths = NULL, **texts = NULL, **formatted = NULL;
    int count = -1;
    if (config) {
        authoring_format_options options = authoring_format_defaults();
        if (!config_read(config, &options, error, sizeof error)) {
            goto done;
        }
    }
    size_t bytes = 0;
    struct stat info;
    size_t prefix = !lstat(path, &info) && S_ISDIR(info.st_mode) ? strlen(path) : 0;
    while (prefix && path[prefix - 1] == '/') {
        prefix--;
    }
    if (!collect(path, 0, &paths, error, sizeof error)) {
        goto done;
    }
    if (acount(paths) > 1) {
        qsort(paths, (size_t)acount(paths), sizeof *paths, compare);
    }
    for (int i = 0; i < acount(paths); i++) {
        char *text = read_text(paths[i], MAX_BYTES, error, sizeof error);
        apush(texts, text);
        if (!text) {
            goto done;
        }
        size_t length = strlen(text);
        if (length > MAX_TOTAL_BYTES - bytes) {
            fail(error, sizeof error, path, "formatter aggregate byte limit exceeded");
            goto done;
        }
        bytes += length;
        char *result = authoring_format_file_text(paths[i], text, config, error, sizeof error);
        apush(formatted, result);
        if (!result) {
            fprintf(stderr, "format: %s: %s\n", paths[i], error);
            error[0] = 0;
            goto done;
        }
    }
    count = 0;
    for (int i = 0; i < acount(paths); i++) {
        if (!strcmp(texts[i], formatted[i])) {
            continue;
        }
        if (!check && !write_text(paths[i], texts[i], formatted[i])) {
            fail(error, sizeof error, paths[i], "source changed or write failed");
            count = -1;
            break;
        }
        fprintf(
            stderr,
            "%s %s\n",
            check ? "not canonical:" : "formatted",
            prefix ? paths[i] + prefix + 1 : paths[i]
        );
        count++;
    }
done:
    if (error[0]) {
        fprintf(stderr, "format: %s\n", error);
    }
    for (int i = 0; i < acount(paths); i++) {
        free(paths[i]);
    }
    for (int i = 0; i < acount(texts); i++) {
        free(texts[i]);
    }
    for (int i = 0; i < acount(formatted); i++) {
        free(formatted[i]);
    }
    afree(paths);
    afree(texts);
    afree(formatted);
    return count;
}
