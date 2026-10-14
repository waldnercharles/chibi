#include "state_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include "support/rooted_files.h"
#include "support/xalloc.h"

#include <errno.h>
#endif

#ifndef _WIN32
char *chibi_read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return NULL;
    }
    char *text = malloc((size_t)length + 1);
    if (!text) {
        abort();
    }
    bool ok = fread(text, 1, (size_t)length, file) == (size_t)length;
    ok = fclose(file) == 0 && ok;
    if (!ok) {
        free(text);
        return NULL;
    }
    text[length] = 0;
    return text;
}

bool chibi_write_file(const char *path, const char *text)
{
    size_t size = strlen(path) + sizeof ".XXXXXX";
    char *temporary = malloc(size);
    if (!temporary) {
        abort();
    }
    snprintf(temporary, size, "%s.XXXXXX", path);
    int fd = mkstemp(temporary);
    FILE *file = fd < 0 ? NULL : fdopen(fd, "wb");
    bool ok = false;
    if (file) {
        size_t length = strlen(text);
        ok = fwrite(text, 1, length, file) == length;
        ok = fflush(file) == 0 && ok;
        ok = fsync(fd) == 0 && ok;
        ok = fclose(file) == 0 && ok;
        if (ok) {
            ok = rename(temporary, path) == 0;
        }
    } else if (fd >= 0) {
        close(fd);
    }
    if (!ok) {
        unlink(temporary);
    }
    free(temporary);
    return ok;
}

#else
char *chibi_read_file(const char *path)
{
    char *leaf = NULL;
    rooted_files *files = rooted_files_open_parent(path, &leaf);
    if (!files) {
        return NULL;
    }
    unsigned char *text = NULL;
    size_t bytes = 0;
    bool ok = rooted_files_read(files, leaf, 64 * 1024 * 1024, &text, &bytes);
    rooted_files_close(files);
    free(leaf);
    if (!ok) {
        free(text);
        return NULL;
    }
    return (char *)text;
}
bool chibi_write_file(const char *path, const char *text)
{
    char *leaf = NULL;
    rooted_files *files = rooted_files_open_parent(path, &leaf);
    if (!files) {
        return false;
    }
    unsigned char *before = NULL;
    size_t bytes = 0;
    bool existed = rooted_files_read(files, leaf, 64 * 1024 * 1024, &before, &bytes);
    bool ok = (existed || errno == ENOENT) &&
              rooted_files_write(files, leaf, text, strlen(text), 64 * 1024 * 1024, existed, before, bytes);
    free(before);
    free(leaf);
    rooted_files_close(files);
    return ok;
}
#endif
