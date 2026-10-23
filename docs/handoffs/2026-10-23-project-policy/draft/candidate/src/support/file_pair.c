#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "support/file_pair.h"

#include "support/xalloc.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

static bool fail(file_pair *pair, file_pair_operation operation, int entry)
{
    if (!(pair->plan.failures & FILE_PAIR_FAIL(operation, entry))) {
        return false;
    }
    errno = EIO;
    return true;
}
static void failure(file_pair *pair, file_pair_operation operation, int entry)
{
    if (!pair->system_error) {
        pair->system_error = errno;
        pair->failed_operation = operation;
        pair->failed_entry = entry;
    }
}
static int move(file_pair *pair, file_pair_operation operation, int entry, const char *source, const char *destination)
{
    if (fail(pair, operation, entry)) {
        return -1;
    }
    if (operation == FILE_PAIR_BACKUP || pair->plan.mode == FILE_PAIR_NATIVE) {
        return rename(source, destination);
    }
    switch (pair->plan.mode) {
        case FILE_PAIR_EXCLUSIVE:
#if defined(__APPLE__)
            return renamex_np(source, destination, RENAME_EXCL);
#elif defined(__linux__)
            return (int)syscall(SYS_renameat2, AT_FDCWD, source, AT_FDCWD, destination, 1 /* RENAME_NOREPLACE */);
#else
            errno = ENOTSUP;
            return -1;
#endif
        case FILE_PAIR_NATIVE: break;
    }
    assert(false);
    return -1;
}
static bool remove_owned(file_pair *pair, file_pair_operation operation, int entry, const char *path)
{
    if (!fail(pair, operation, entry) && (!unlink(path) || errno == ENOENT)) {
        return true;
    }
    failure(pair, operation, entry);
    return false;
}
static size_t quoted_size(const char *path)
{
    size_t size = 2;
    for (const unsigned char *at = (const unsigned char *)path; *at; at++) {
        size += *at >= 32 && *at <= 126 && *at != '"' && *at != '\\' ? 1 : 4;
    }
    return size;
}
static char *quote(char *out, const char *path)
{
    static const char hex[] = "0123456789abcdef";
    *out++ = '"';
    for (const unsigned char *at = (const unsigned char *)path; *at; at++) {
        if (*at >= 32 && *at <= 126 && *at != '"' && *at != '\\') {
            *out++ = (char)*at;
        } else {
            *out++ = '\\';
            *out++ = 'x';
            *out++ = hex[*at >> 4];
            *out++ = hex[*at & 15];
        }
    }
    *out++ = '"';
    *out++ = '\n';
    *out = 0;
    return out;
}
char *file_pair_source_diagnostic(const char *const sources[2], size_t limit, size_t *size)
{
    static const char
        first[] = "artifact-pair not admitted; raw source arguments\nsource0 ";
    static const char second[] = "source1 ";
    *size = strlen(first) + quoted_size(sources[0]) + 1 + strlen(second) +
            quoted_size(sources[1]) + 1;
    if (*size > limit) {
        return NULL;
    }
    char *text = xmalloc(*size + 1);
    memcpy(text, first, strlen(first));
    char *out = quote(text + strlen(first), sources[0]);
    memcpy(out, second, strlen(second));
    out = quote(out + strlen(second), sources[1]);
    assert((size_t)(out - text) == *size);
    return text;
}
size_t file_pair_diagnostic_capacity(const file_pair_plan *plan)
{
    size_t capacity = 1024;
    for (int i = 0; i < 2; i++) {
        capacity += quoted_size(plan->source[i]) +
                    2 * quoted_size(plan->destination[i]) + strlen(".tmp.XXXXXX");
    }
    return capacity;
}
static void diagnose(file_pair *pair)
{
    pair->diagnostic_size = 0;
    if (!pair->status) {
        pair->diagnostic[0] = 0;
        return;
    }
    int length = snprintf(
        pair->diagnostic,
        pair->diagnostic_capacity,
        "artifact-pair status=%d operation=%d entry=%d errno=%d\n",
        pair->status,
        pair->failed_operation,
        pair->failed_entry,
        pair->system_error
    );
    assert(length > 0);
    pair->diagnostic_size = (size_t)length;
    for (int i = 0; i < 2; i++) {
        length = snprintf(
            pair->diagnostic + pair->diagnostic_size,
            pair->diagnostic_capacity - pair->diagnostic_size,
            "source%d consumed=%d retained=%d ",
            i,
            pair->consumed[i],
            pair->retained_source[i]
        );
        pair->diagnostic_size += (size_t)length;
        pair->diagnostic_size = (size_t)(quote(
                                             pair->diagnostic + pair->diagnostic_size,
                                             pair->plan.source[i]
                                         ) -
                                         pair->diagnostic);
        length = snprintf(
            pair->diagnostic + pair->diagnostic_size,
            pair->diagnostic_capacity - pair->diagnostic_size,
            "destination%d prior=%d published=%d ",
            i,
            pair->existed[i],
            pair->published[i]
        );
        pair->diagnostic_size += (size_t)length;
        pair->diagnostic_size = (size_t)(quote(
                                             pair->diagnostic + pair->diagnostic_size,
                                             pair->plan.destination[i]
                                         ) -
                                         pair->diagnostic);
        length = snprintf(
            pair->diagnostic + pair->diagnostic_size,
            pair->diagnostic_capacity - pair->diagnostic_size,
            "backup%d old-bytes=%d retained=%d ",
            i,
            pair->old_moved[i],
            pair->retained_backup[i]
        );
        pair->diagnostic_size += (size_t)length;
        pair->diagnostic_size = (size_t)(quote(pair->diagnostic + pair->diagnostic_size, pair->backup[i]) -
                                         pair->diagnostic);
    }
    assert(pair->diagnostic_size < pair->diagnostic_capacity);
}
static bool cleanup(file_pair *pair)
{
    bool clean = true;
    for (int i = 0; i < 2; i++) {
        if (pair->admitted && !pair->consumed[i]) {
            pair->retained_source[i] = !remove_owned(
                pair,
                FILE_PAIR_REMOVE_SOURCE,
                i,
                pair->plan.source[i]
            );
            clean &= !pair->retained_source[i];
        }
        if (!pair->reserved[i]) {
            continue;
        }
        if (pair->old_moved[i] && !pair->committed) {
            pair->retained_backup[i] = true;
            clean = false;
            continue;
        }
        pair->retained_backup[i] = !remove_owned(pair, FILE_PAIR_REMOVE_BACKUP, i, pair->backup[i]);
        clean &= !pair->retained_backup[i];
        if (!pair->retained_backup[i]) {
            pair->reserved[i] = false;
            pair->old_moved[i] = false;
        }
    }
    return clean;
}
bool file_pair_prepare(file_pair *pair, const file_pair_plan *plan)
{
    *pair = (file_pair){ .plan = *plan, .status = 1, .failed_entry = -1 };
    size_t capacity = file_pair_diagnostic_capacity(plan);
    for (int i = 0; i < 2; i++) {
        size_t size = strlen(plan->destination[i]) + sizeof ".tmp.XXXXXX";
        pair->backup[i] = xmalloc(size);
        snprintf(pair->backup[i], size, "%s.tmp.XXXXXX", plan->destination[i]);
    }
    pair->diagnostic = xmalloc(capacity);
    pair->diagnostic_capacity = capacity;
    for (int i = 0; i < 2; i++) {
        struct stat info;
        if (!lstat(plan->destination[i], &info)) {
            pair->existed[i] = true;
            if (!S_ISREG(info.st_mode)) {
                errno = EINVAL;
                failure(pair, FILE_PAIR_RESERVE, i);
                file_pair_abort(pair);
                return false;
            }
        } else if (errno != ENOENT) {
            failure(pair, FILE_PAIR_RESERVE, i);
            file_pair_abort(pair);
            return false;
        }
    }
    for (int i = 0; i < 2; i++) {
        int fd = fail(pair, FILE_PAIR_RESERVE, i) ? -1 : mkstemp(pair->backup[i]);
        if (fd < 0) {
            failure(pair, FILE_PAIR_RESERVE, i);
            file_pair_abort(pair);
            return false;
        }
        pair->reserved[i] = true;
        if (close(fd)) {
            failure(pair, FILE_PAIR_RESERVE, i);
            file_pair_abort(pair);
            return false;
        }
    }
    pair->prepared = true;
    return true;
}
int file_pair_abort(file_pair *pair)
{
    assert(!pair->admitted);
    cleanup(pair);
    pair->status = 1;
    diagnose(pair);
    return pair->status;
}
int file_pair_execute(file_pair *pair)
{
    assert(pair->prepared && !pair->admitted);
    pair->admitted = true;
    bool valid = true;
    for (int i = 0; valid && i < 2; i++) {
        if (pair->existed[i]) {
            valid = !move(pair, FILE_PAIR_BACKUP, i, pair->plan.destination[i], pair->backup[i]);
            pair->old_moved[i] = valid;
            if (!valid) {
                failure(pair, FILE_PAIR_BACKUP, i);
            }
        }
    }
    for (int i = 0; valid && i < 2; i++) {
        valid = !move(
            pair,
            FILE_PAIR_PUBLISH,
            i,
            pair->plan.source[i],
            pair->plan.destination[i]
        );
        pair->published[i] = valid;
        pair->consumed[i] = valid;
        if (!valid) {
            failure(pair, FILE_PAIR_PUBLISH, i);
        }
    }
    pair->committed = valid;
    pair->status = valid ? 0 : 1;
    if (!valid) {
        for (int i = 0; i < 2; i++) {
            bool absent = true;
            if (pair->published[i]) {
                absent = remove_owned(pair, FILE_PAIR_REMOVE_NEW, i, pair->plan.destination[i]);
                if (absent) {
                    pair->published[i] = false;
                } else {
                    pair->status = 2;
                }
            }
            if (pair->old_moved[i]) {
                if (absent && !move(
                                  pair,
                                  FILE_PAIR_RESTORE,
                                  i,
                                  pair->backup[i],
                                  pair->plan.destination[i]
                              )) {
                    pair->old_moved[i] = false;
                    pair->reserved[i] = false;
                } else {
                    failure(pair, FILE_PAIR_RESTORE, i);
                    pair->retained_backup[i] = true;
                    pair->status = 2;
                }
            }
        }
    }
    bool clean = cleanup(pair);
    if (pair->committed && !clean) {
        pair->status = 3;
    }
    diagnose(pair);
    return pair->status;
}
void file_pair_dispose(file_pair *pair)
{
    free(pair->backup[0]);
    free(pair->backup[1]);
    free(pair->diagnostic);
    *pair = (file_pair){ 0 };
}
