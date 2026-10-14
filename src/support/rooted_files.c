#include "support/rooted_files_internal.h"
#include "support/utf8.h"
#include "support/xalloc.h"

#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PATH_BYTES_MAX 4096
#define FILE_BYTES_MAX (64 * 1024 * 1024)
static bool relative(const char *path)
{
    if (!path || !*path || *path == '/' || strchr(path, '\\') ||
        strlen(path) > PATH_BYTES_MAX || !utf8_text_valid(path, strlen(path))) {
        errno = EINVAL;
        return false;
    }
    const char *at = path;
    for (;;) {
        const char *slash = strchr(at, '/');
        size_t bytes = slash ? (size_t)(slash - at) : strlen(at);
        if (!bytes || (bytes == 1 && at[0] == '.') ||
            (bytes == 2 && at[0] == '.' && at[1] == '.')) {
            errno = EINVAL;
            return false;
        }
        char component[PATH_BYTES_MAX + 1];
        memcpy(component, at, bytes);
        component[bytes] = 0;
        if (!rooted_native_name_valid(component)) {
            return false;
        }
        if (!slash) {
            return true;
        }
        at = slash + 1;
    }
}
bool rooted_files_path_valid(const char *path)
{
    return relative(path);
}
rooted_files *rooted_files_open(const char *path)
{
    assert(path);
    return rooted_native_root(path);
}
rooted_files *rooted_files_open_parent(const char *path, char **leaf)
{
    *leaf = NULL;
    if (!path || !*path || strlen(path) > PATH_BYTES_MAX ||
        !utf8_text_valid(path, strlen(path))) {
        errno = EINVAL;
        return NULL;
    }
    char *copy = xstrdup(path), *slash = strrchr(copy, '/');
#ifdef _WIN32
    char *backslash = strrchr(copy, '\\');
    if (backslash && (!slash || backslash > slash)) {
        slash = backslash;
    }
#endif
    char *name = xstrdup(slash ? slash + 1 : copy);
    rooted_files *files = NULL;
    if (rooted_files_path_valid(name)) {
        if (slash) {
            if (slash == copy || (slash == copy + 2 && copy[1] == ':')) {
                slash[1] = 0;
            } else {
                *slash = 0;
            }
            files = rooted_files_open(copy);
        } else {
            files = rooted_files_open(".");
        }
    }
    int error = errno;
    free(copy);
    if (files) {
        *leaf = name;
    } else {
        free(name);
    }
    errno = error;
    return files;
}
static rooted_files *parent(rooted_files *root, const char *path, char leaf[PATH_BYTES_MAX + 1])
{
    assert(root && root->directory);
    if (!relative(path)) {
        return NULL;
    }
    char copy[PATH_BYTES_MAX + 1];
    strcpy(copy, path);
    rooted_files *current = rooted_native_duplicate(root);
    char *at = copy, *slash;
    while (current && (slash = strchr(at, '/'))) {
        *slash = 0;
        rooted_files *next = rooted_native_child(current, at, true, ROOTED_CREATE_NONE, 0);
        int error = errno;
        rooted_files_close(current);
        errno = error;
        current = next;
        at = slash + 1;
    }
    if (current) {
        strcpy(leaf, at);
    }
    return current;
}
rooted_files *rooted_files_directory(rooted_files *root, const char *path, bool create)
{
    assert(root && root->directory && path);
    if (!*path) {
        return rooted_native_duplicate(root);
    }
    if (!relative(path)) {
        return NULL;
    }
    char copy[PATH_BYTES_MAX + 1];
    strcpy(copy, path);
    rooted_files *current = rooted_native_duplicate(root);
    char *at = copy;
    while (current) {
        char *slash = strchr(at, '/');
        if (slash) {
            *slash = 0;
        }
        rooted_files *next = rooted_native_child(
            current,
            at,
            true,
            create ? ROOTED_CREATE_OPEN : ROOTED_CREATE_NONE,
            0700
        );
        int error = errno;
        rooted_files_close(current);
        errno = error;
        current = next;
        if (!slash) {
            break;
        }
        at = slash + 1;
    }
    return current;
}
rooted_files *rooted_files_directory_new(rooted_files *root, const char *path)
{
    char leaf[PATH_BYTES_MAX + 1];
    rooted_files *directory = parent(root, path, leaf);
    if (!directory) {
        return NULL;
    }
    rooted_files *created = rooted_native_child(directory, leaf, true, ROOTED_CREATE_EXCLUSIVE, 0700);
    int error = errno;
    rooted_files_close(directory);
    errno = error;
    return created;
}
rooted_stream *rooted_files_stream(rooted_files *root, const char *path, rooted_stream_mode mode)
{
    char leaf[PATH_BYTES_MAX + 1];
    rooted_files *directory = parent(root, path, leaf);
    if (!directory) {
        return NULL;
    }
    rooted_stream *file = rooted_native_child(
        directory,
        leaf,
        false,
        mode == ROOTED_STREAM_CREATE ? ROOTED_CREATE_EXCLUSIVE : ROOTED_CREATE_NONE,
        0666
    );
    int error = errno;
    rooted_files_close(directory);
    errno = error;
    return file;
}
bool rooted_files_stat(rooted_files *root, const char *path, rooted_file_info *out)
{
    char leaf[PATH_BYTES_MAX + 1];
    rooted_files *directory = parent(root, path, leaf);
    if (!directory) {
        return false;
    }
    bool ok = rooted_native_stat(directory, leaf, out);
    if (ok && out->link) {
        ok = false;
        errno = ELOOP;
    }
    int error = errno;
    rooted_files_close(directory);
    errno = error;
    return ok;
}
static int compare_entries(const void *a, const void *b)
{
    return strcmp(
        ((const rooted_file_entry *)a)->name,
        ((const rooted_file_entry *)b)->name
    );
}
bool rooted_files_list(rooted_files *root, const char *path, int limit, rooted_file_entry **out, int *count)
{
    *out = NULL;
    *count = 0;
    if (limit < 1 || limit > 65536) {
        errno = EINVAL;
        return false;
    }
    rooted_files *directory = rooted_files_directory(root, path, false);
    if (!directory) {
        return false;
    }
    bool ok = rooted_native_list(directory, limit, out, count);
    int error = errno;
    rooted_files_close(directory);
    errno = error;
    for (int i = 0; ok && i < *count; i++) {
        if (!utf8_text_valid((*out)[i].name, strlen((*out)[i].name))) {
            rooted_file_entries_free(*out, *count);
            *out = NULL;
            *count = 0;
            errno = EILSEQ;
            ok = false;
        }
    }
    if (ok && *count > 1) {
        qsort(*out, (size_t)*count, sizeof **out, compare_entries);
    }
    return ok;
}
void rooted_file_entries_free(rooted_file_entry *entries, int count)
{
    for (int i = 0; i < count; i++) {
        free(entries[i].name);
    }
    free(entries);
}
bool rooted_file_info_equal(const rooted_file_info *a, const rooted_file_info *b)
{
    return a->device == b->device && a->identity[0] == b->identity[0] &&
           a->identity[1] == b->identity[1] && a->bytes == b->bytes &&
           a->modified_seconds == b->modified_seconds &&
           a->modified_nanoseconds == b->modified_nanoseconds &&
           a->changed_seconds == b->changed_seconds &&
           a->changed_nanoseconds == b->changed_nanoseconds &&
           a->regular == b->regular && a->directory == b->directory &&
           a->link == b->link;
}
static bool read_file(rooted_stream *file, size_t limit, unsigned char **out, size_t *size)
{
    rooted_file_info before, after;
    if (!rooted_stream_info(file, &before) || !before.regular) {
        errno = EINVAL;
        return false;
    }
    if (before.bytes > limit) {
        errno = EFBIG;
        return false;
    }
    unsigned char *data = xmalloc((size_t)before.bytes + 1);
    size_t read = 0;
    while (read < before.bytes) {
        int64_t got = rooted_stream_read(file, data + read, (size_t)before.bytes - read);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            free(data);
            if (!got) {
                errno = EIO;
            }
            return false;
        }
        read += (size_t)got;
    }
    unsigned char extra;
    int64_t got;
    do {
        got = rooted_stream_read(file, &extra, 1);
    } while (got < 0 && errno == EINTR);
    if (got < 0 || !rooted_stream_info(file, &after)) {
        free(data);
        return false;
    }
    if (got != 0 || !rooted_file_info_equal(&before, &after)) {
        free(data);
        errno = EBUSY;
        return false;
    }
    data[read] = 0;
    *out = data;
    *size = read;
    return true;
}
bool rooted_files_read(rooted_files *root, const char *path, size_t limit, unsigned char **out, size_t *size)
{
    assert(out && size);
    *out = NULL;
    *size = 0;
    if (limit > FILE_BYTES_MAX) {
        errno = EFBIG;
        return false;
    }
    rooted_stream *file = rooted_files_stream(root, path, ROOTED_STREAM_READ);
    if (!file) {
        return false;
    }
    bool ok = read_file(file, limit, out, size);
    int error = errno;
    bool closed = rooted_stream_finish(file);
    if (!ok) {
        errno = error;
    } else if (!closed) {
        free(*out);
        *out = NULL;
        *size = 0;
        ok = false;
    }
    return ok;
}
static bool baseline(rooted_files *directory, const char *leaf, size_t limit, const void *expected, size_t expected_bytes)
{
    unsigned char *data = NULL;
    size_t bytes = 0;
    if (!rooted_files_read(directory, leaf, limit, &data, &bytes)) {
        return false;
    }
    bool equal = bytes == expected_bytes &&
                 (!bytes || !memcmp(data, expected, bytes));
    free(data);
    if (!equal) {
        errno = EBUSY;
    }
    return equal;
}
bool rooted_files_write(
    rooted_files *root,
    const char *path,
    const void *data,
    size_t bytes,
    size_t limit,
    bool replace,
    const void *expected,
    size_t expected_bytes
)
{
    assert(data || !bytes);
    if (bytes > limit || expected_bytes > limit || limit > FILE_BYTES_MAX ||
        (replace && !expected && expected_bytes)) {
        errno = EFBIG;
        return false;
    }
    char leaf[PATH_BYTES_MAX + 1];
    rooted_files *directory = parent(root, path, leaf);
    if (!directory) {
        return false;
    }
    rooted_file_info original = { 0 };
    bool ok = !replace ||
              (rooted_files_stat(directory, leaf, &original) &&
               baseline(directory, leaf, limit, expected, expected_bytes));
    rooted_stream *stage = NULL;
    char temporary[96] = "";
    /* Exclusive creation, rather than predictability, protects stage names. */
    static _Atomic unsigned sequence;
    for (int attempt = 0; ok && !stage && attempt < 128; attempt++) {
        snprintf(
            temporary,
            sizeof temporary,
            ".chibi-stage-%p-%u",
            (void *)root,
            atomic_fetch_add_explicit(&sequence, 1, memory_order_relaxed) + 1
        );
        stage = rooted_native_child(directory, temporary, false, ROOTED_CREATE_EXCLUSIVE, replace ? 0600 : 0666);
        if (!stage && errno != EEXIST) {
            ok = false;
        }
    }
    if (ok && !stage) {
        errno = EEXIST;
        ok = false;
    }
    bool created = stage != NULL;
    if (ok && replace) {
        ok = rooted_native_permissions(stage, original.permissions);
    }
    size_t written = 0;
    while (ok && written < bytes) {
        int64_t n = rooted_stream_write(stage, (const unsigned char *)data + written, bytes - written);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            if (!n) {
                errno = EIO;
            }
            ok = false;
            break;
        }
        written += (size_t)n;
    }
    if (ok) {
        ok = rooted_stream_flush(stage);
    }
    if (stage) {
        int error = errno;
        bool closed = rooted_stream_finish(stage);
        if (!ok) {
            errno = error;
        } else {
            ok = closed;
        }
    }
    if (ok && replace) {
        ok = baseline(directory, leaf, limit, expected, expected_bytes);
    }
    if (ok) {
        ok = rooted_native_move(directory, temporary, directory, leaf, replace);
    }
    int error = errno;
    if (created) {
        rooted_native_remove(directory, temporary);
    }
    rooted_files_close(directory);
    errno = error;
    return ok;
}
static bool move_entry(rooted_files *root, const char *from, const char *to, bool replace, bool directory)
{
    char old_leaf[PATH_BYTES_MAX + 1], new_leaf[PATH_BYTES_MAX + 1];
    rooted_files *old = parent(root, from, old_leaf);
    if (!old) {
        return false;
    }
    rooted_file_info info;
    bool valid = rooted_native_stat(old, old_leaf, &info) &&
                 (directory ? info.directory : info.regular) && !info.link;
    if (!valid) {
        rooted_files_close(old);
        errno = EINVAL;
        return false;
    }
    rooted_files *next = parent(root, to, new_leaf);
    bool ok = next && rooted_native_move(old, old_leaf, next, new_leaf, replace);
    int error = errno;
    if (next) {
        rooted_files_close(next);
    }
    rooted_files_close(old);
    errno = error;
    return ok;
}
bool rooted_files_move(rooted_files *root, const char *from, const char *to, bool replace)
{
    return move_entry(root, from, to, replace, false);
}
bool rooted_files_move_directory(rooted_files *root, const char *from, const char *to)
{
    return move_entry(root, from, to, false, true);
}
static bool remove_entry(rooted_files *root, const char *path, bool is_directory)
{
    char leaf[PATH_BYTES_MAX + 1];
    rooted_files *directory = parent(root, path, leaf);
    if (!directory) {
        return false;
    }
    rooted_file_info info;
    bool ok = rooted_native_stat(directory, leaf, &info) &&
              (is_directory ? info.directory : info.regular) && !info.link;
    if (ok) {
        ok = is_directory ? rooted_native_remove_directory(directory, leaf)
                          : rooted_native_remove(directory, leaf);
    } else {
        errno = EINVAL;
    }
    int error = errno;
    rooted_files_close(directory);
    errno = error;
    return ok;
}
bool rooted_files_remove(rooted_files *root, const char *path)
{
    return remove_entry(root, path, false);
}
bool rooted_files_remove_directory(rooted_files *root, const char *path)
{
    return remove_entry(root, path, true);
}

char *rooted_files_canonical_path(rooted_files *root, const char *path)
{
    rooted_stream *file = rooted_files_stream(root, path, ROOTED_STREAM_READ);
    if (!file) {
        return NULL;
    }
    char *absolute = rooted_files_host_path(file);
    char *base = rooted_files_host_path(root);
    rooted_stream_close(file);
    if (!absolute || !base) {
        free(absolute);
        free(base);
        return NULL;
    }
    size_t bytes = strlen(base);
    while (bytes && base[bytes - 1] == '/') {
        bytes--;
    }
    char *result = NULL;
    if (!strncmp(absolute, base, bytes) && absolute[bytes] == '/' &&
        absolute[bytes + 1]) {
        result = xstrdup(absolute + bytes + 1);
    } else {
        errno = EXDEV;
    }
    free(absolute);
    free(base);
    return result;
}
bool rooted_files_path_equal(rooted_files *root, const char *left, const char *right)
{
    if (!strcmp(left, right)) {
        return true;
    }
    if (!root) {
        return false;
    }
    char a[PATH_BYTES_MAX + 1], b[PATH_BYTES_MAX + 1];
    rooted_files *first = parent(root, left, a), *second = parent(root, right, b);
    rooted_file_info x, y;
    bool equal = false;
    if (first && second) {
        bool a_exists = rooted_native_stat(first, a, &x) && !x.link;
        bool b_exists = rooted_native_stat(second, b, &y) && !y.link;
        if (a_exists && b_exists) {
            equal = x.device == y.device && x.identity[0] == y.identity[0] &&
                    x.identity[1] == y.identity[1];
        } else if (!a_exists && !b_exists) {
            equal = rooted_stream_info(first, &x) && rooted_stream_info(second, &y) &&
                    x.device == y.device && x.identity[0] == y.identity[0] &&
                    x.identity[1] == y.identity[1] &&
                    rooted_native_name_equal(first, a, b);
        }
    }
    if (first) {
        rooted_files_close(first);
    }
    if (second) {
        rooted_files_close(second);
    }
    return equal;
}
