#ifndef _WIN32
#ifdef __APPLE__
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif
#include "support/binary_cache.h"

#ifdef _WIN32
unsigned char *binary_cache_read(const char *, const char *, size_t, size_t *length)
{
    *length = 0;
    return NULL;
}
bool binary_cache_remove(const char *, const char *)
{
    return false;
}
int binary_cache_lock(const char *)
{
    return -1;
}
void binary_cache_unlock(int) {}
bool binary_cache_list(const char *, binary_cache_entry **entries, int *count)
{
    *entries = NULL;
    *count = 0;
    return false;
}
bool binary_cache_write(const char *, const char *, const void *, size_t, size_t)
{
    return false;
}
#else
#include "support/sha256.h"
#include "support/xalloc.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <stdatomic.h>
static atomic_flag publication_lock = ATOMIC_FLAG_INIT;
#endif

#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif

static const unsigned char magic[8] = { 'C', 'H', 'I', 'B', 'I', 'C', '0', '1' };
static void changed()
{
#ifdef __EMSCRIPTEN__
    EM_ASM({
        if (Module['chibiAssetCacheDirty']) {
            Module['chibiAssetCacheDirty']();
        }
    });
#endif
}
static bool key_valid(const char *key)
{
    size_t length = key ? strnlen(key, BINARY_CACHE_KEY_BYTES + 1) : 0;
    if (!length || length > BINARY_CACHE_KEY_BYTES) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        char c = key[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) {
            return false;
        }
    }
    return true;
}
static char *joined(const char *directory, const char *name)
{
    size_t length = directory ? strnlen(directory, 4097) : 0;
    if (!length || length > 4096) {
        return NULL;
    }
    size_t size = length + strlen(name) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", directory, name);
    return path;
}
static char *path_for(const char *directory, const char *key)
{
    if (!key_valid(key)) {
        return NULL;
    }
    char name[BINARY_CACHE_KEY_BYTES + 5];
    snprintf(name, sizeof name, "%s.bin", key);
    return joined(directory, name);
}
static uint64_t read_length(const unsigned char *bytes)
{
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) {
        value |= (uint64_t)bytes[i] << (i * 8);
    }
    return value;
}
unsigned char *binary_cache_read(const char *directory, const char *key, size_t limit, size_t *length)
{
    *length = 0;
    char *path = path_for(directory, key);
    if (!path) {
        return NULL;
    }
    struct stat named;
    if (lstat(path, &named) || !S_ISREG(named.st_mode)) {
        free(path);
        return NULL;
    }
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    free(path);
    if (fd < 0) {
        return NULL;
    }
    struct stat info;
    bool fits = !fstat(fd, &info) && S_ISREG(info.st_mode) &&
                info.st_size >= BINARY_CACHE_HEADER_BYTES &&
                (uint64_t)info.st_size - BINARY_CACHE_HEADER_BYTES <= limit;
    FILE *file = fits ? fdopen(fd, "rb") : NULL;
    if (!file) {
        close(fd);
        return NULL;
    }
    unsigned char header[BINARY_CACHE_HEADER_BYTES];
    unsigned char *bytes = NULL;
    char digest[65];
    if (fread(header, 1, sizeof header, file) != sizeof header ||
        memcmp(header, magic, sizeof magic)) {
        goto done;
    }
    uint64_t size = read_length(header + 8);
    if (size != (uint64_t)info.st_size - sizeof header || size > limit) {
        goto done;
    }
    sha256_hex(key, strlen(key), digest);
    if (memcmp(header + 16, digest, 64)) {
        goto done;
    }
    bytes = xmalloc(size ? (size_t)size : 1);
    if (fread(bytes, 1, (size_t)size, file) != size || fgetc(file) != EOF ||
        ferror(file)) {
        free(bytes);
        bytes = NULL;
        goto done;
    }
    sha256_hex(bytes, (size_t)size, digest);
    if (memcmp(header + 80, digest, 64)) {
        free(bytes);
        bytes = NULL;
        goto done;
    }
    *length = (size_t)size;
done:
    fclose(file);
    return bytes;
}
bool binary_cache_remove(const char *directory, const char *key)
{
    char *path = path_for(directory, key);
    if (!path) {
        return false;
    }
    int result = unlink(path);
    int error = errno;
    free(path);
    if (!result) {
        changed();
    }
    return !result || error == ENOENT;
}
static bool directories(const char *directory)
{
    if (!directory || !*directory || strnlen(directory, 4097) > 4096) {
        return false;
    }
    char *path = xstrdup(directory);
    bool ok = true;
    for (char *p = path + 1;; p++) {
        bool separator = *p == '/';
        if (*p && !separator) {
            continue;
        }
        char end = *p;
        *p = 0;
        struct stat info;
        if (mkdir(path, 0700) &&
            (errno != EEXIST || stat(path, &info) || !S_ISDIR(info.st_mode))) {
            ok = false;
            break;
        }
        *p = end;
        if (!end) {
            break;
        }
    }
    free(path);
    return ok;
}
int binary_cache_lock(const char *directory)
{
#ifdef __EMSCRIPTEN__
    if (atomic_flag_test_and_set(&publication_lock)) {
        return -1;
    }
#endif
    if (!directories(directory)) {
#ifdef __EMSCRIPTEN__
        atomic_flag_clear(&publication_lock);
#endif
        return -1;
    }
    char *path = joined(directory, ".lock");
    int fd = path ? open(path, O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK, 0600) : -1;
    free(path);
    struct stat info;
    if (fd >= 0 && (fstat(fd, &info) || !S_ISREG(info.st_mode) ||
                    flock(fd, LOCK_EX | LOCK_NB))) {
        close(fd);
        fd = -1;
    }
#ifdef __EMSCRIPTEN__
    if (fd < 0) {
        atomic_flag_clear(&publication_lock);
    }
#endif
    return fd;
}
void binary_cache_unlock(int descriptor)
{
    if (descriptor >= 0) {
        flock(descriptor, LOCK_UN);
        close(descriptor);
#ifdef __EMSCRIPTEN__
        atomic_flag_clear(&publication_lock);
#endif
    }
}
static int entry_order(const void *a, const void *b)
{
    return strcmp(
        ((const binary_cache_entry *)a)->key,
        ((const binary_cache_entry *)b)->key
    );
}
bool binary_cache_list(const char *directory, binary_cache_entry **entries, int *count)
{
    *entries = NULL;
    *count = 0;
    DIR *dir = opendir(directory);
    if (!dir) {
        return false;
    }
    binary_cache_entry *items = xcalloc(BINARY_CACHE_SCAN_LIMIT, sizeof *items);
    bool ok = true;
    struct dirent *item;
    int visited = 0;
    for (;;) {
        errno = 0;
        item = readdir(dir);
        if (!item) {
            if (errno) {
                ok = false;
            }
            break;
        }
        if (++visited > BINARY_CACHE_SCAN_LIMIT + 16) {
            ok = false;
            break;
        }
        size_t length = strlen(item->d_name);
        bool temporary = !strncmp(item->d_name, ".tmp-", 5);
        if (!temporary && (length <= 4 || strcmp(item->d_name + length - 4, ".bin"))) {
            continue;
        }
        if (!temporary && length - 4 > BINARY_CACHE_KEY_BYTES) {
            continue;
        }
        char *path = joined(directory, item->d_name);
        struct stat info;
        bool regular = path && !lstat(path, &info) && S_ISREG(info.st_mode);
        if (regular && temporary) {
            if (!unlink(path)) {
                changed();
            }
        }
        free(path);
        if (!regular || temporary) {
            continue;
        }
        if (*count == BINARY_CACHE_SCAN_LIMIT) {
            ok = false;
            break;
        }
        binary_cache_entry entry = { 0 };
        memcpy(entry.key, item->d_name, length - 4);
        if (!key_valid(entry.key)) {
            continue;
        }
        entry.bytes = info.st_size < 0 ? 0 : (uint64_t)info.st_size;
        entry.stamp = info.st_mtime < 0 ? 0 : (uint64_t)info.st_mtime;
        items[(*count)++] = entry;
    }
    closedir(dir);
    if (!ok) {
        free(items);
        *count = 0;
        return false;
    }
    qsort(items, (size_t)*count, sizeof *items, entry_order);
    *entries = items;
    return true;
}
bool binary_cache_write(const char *directory, const char *key, const void *bytes, size_t length, size_t limit)
{
    if (length > limit) {
        return false;
    }
    char *path = path_for(directory, key);
    char *temporary = path ? joined(directory, ".tmp-XXXXXX") : NULL;
    int fd = temporary ? mkstemp(temporary) : -1;
    if (fd < 0) {
        free(path);
        free(temporary);
        return false;
    }
    unsigned char header[BINARY_CACHE_HEADER_BYTES] = { 0 };
    memcpy(header, magic, sizeof magic);
    for (int i = 0; i < 8; i++) {
        header[8 + i] = (unsigned char)((uint64_t)length >> (8 * i));
    }
    char digest[65];
    sha256_hex(key, strlen(key), digest);
    memcpy(header + 16, digest, 64);
    sha256_hex(bytes, length, digest);
    memcpy(header + 80, digest, 64);
    FILE *file = fdopen(fd, "wb");
    bool ok = file && fwrite(header, 1, sizeof header, file) == sizeof header &&
              fwrite(bytes, 1, length, file) == length && !fflush(file) &&
              !fsync(fd);
    if (file) {
        if (fclose(file)) {
            ok = false;
        }
    } else {
        close(fd);
    }
    if (ok) {
        ok = !rename(temporary, path);
    }
    if (!ok) {
        unlink(temporary);
    } else {
        changed();
    }
    free(path);
    free(temporary);
    return ok;
}

#endif
