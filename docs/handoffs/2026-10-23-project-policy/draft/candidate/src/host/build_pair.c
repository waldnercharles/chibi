#include "host/build_host_internal.h"
#include "support/file_pair.h"
#include "support/xalloc.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool same_file(const struct stat *left, const struct stat *right)
{
    return left->st_dev == right->st_dev && left->st_ino == right->st_ino;
}
static bool entry(const char *path, bool source, struct stat *info, bool *present)
{
    if (!lstat(path, info)) {
        *present = true;
        return S_ISREG(info->st_mode);
    }
    *present = false;
    return !source && errno == ENOENT;
}
/* All entries share one physical existing directory. Normalize only the
 * parents; retain each requested leaf spelling for exclusive-move admission. */
static bool raw_paths(const char *const input[4], char *paths[4], struct stat before[4], bool present[4])
{
    char *parent = NULL;
    struct stat first = { 0 };
    bool valid = true;
    for (int i = 0; valid && i < 4; i++) {
        const char *slash = strrchr(input[i], '/');
        if (input[i][0] != '/' || !slash || !slash[1] ||
            !strcmp(slash + 1, ".") || !strcmp(slash + 1, "..")) {
            valid = false;
            break;
        }
        size_t length = (size_t)(slash - input[i]);
        char *prefix = xmalloc(length + 2);
        memcpy(prefix, input[i], length);
        prefix[length] = 0;
        if (!length) {
            strcpy(prefix, "/");
        }
        char *canonical = realpath(prefix, NULL);
        free(prefix);
        struct stat directory;
        if (!canonical || stat(canonical, &directory) ||
            !S_ISDIR(directory.st_mode) || (i && !same_file(&first, &directory))) {
            free(canonical);
            valid = false;
            break;
        }
        if (!i) {
            parent = canonical;
            first = directory;
        } else {
            free(canonical);
        }
        size_t size = strlen(parent) + strlen(slash + 1) + 2;
        paths[i] = xmalloc(size);
        snprintf(paths[i], size, "%s%s%s", parent, !strcmp(parent, "/") ? "" : "/", slash + 1);
        valid = entry(paths[i], !(i & 1), &before[i], &present[i]);
        for (int j = 0; valid && j < i; j++) {
            valid = strcmp(paths[i], paths[j]) &&
                    !(present[i] && present[j] && same_file(&before[i], &before[j]));
        }
    }
    free(parent);
    return valid;
}
/* Preparation may create only its two empty backup reservations. Recheck
 * every caller entry so a generated backup cannot occupy a requested name,
 * including a previously absent case alias. No old bytes have moved yet. */
static bool prepared_paths(
    const file_pair *pair,
    char *const paths[4],
    const struct stat before[4],
    const bool present[4]
)
{
    struct stat backups[2];
    for (int i = 0; i < 2; i++) {
        if (lstat(pair->backup[i], &backups[i]) || !S_ISREG(backups[i].st_mode)) {
            return false;
        }
    }
    if (same_file(&backups[0], &backups[1])) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        struct stat after;
        bool exists;
        if (!entry(paths[i], !(i & 1), &after, &exists) ||
            exists != present[i] || (exists && !same_file(&before[i], &after))) {
            return false;
        }
        for (int j = 0; exists && j < 2; j++) {
            if (same_file(&after, &backups[j])) {
                return false;
            }
        }
    }
    return pair->existed[0] == present[1] && pair->existed[1] == present[3];
}
static void append(build_host *host, const char *text, size_t size)
{
    chibi_build_result *result = host->result;
    assert(size < sizeof result->stderr_text - result->stderr_size);
    memcpy(result->stderr_text + result->stderr_size, text, size);
    result->stderr_size += size;
    result->stderr_text[result->stderr_size] = 0;
}
const char *build_host_publish_pair(void *service, const pin_value *arguments, pin_value *results)
{
    build_host *host = service;
    if (!host) {
        return "build host service is unavailable";
    }
    const char *error = build_host_ready(host);
    if (error) {
        return error;
    }
    const char *input[4];
    for (int i = 0; i < 4; i++) {
        error = build_values_cstring(&host->values, arguments[i], &input[i]);
        if (error) {
            return error;
        }
    }
    results[0].i = 1;
    size_t remaining = sizeof host->result->stderr_text - host->result->stderr_size - 1;
    const char *sources[] = { input[0], input[2] };
    size_t evidence_size = 0;
    char *evidence = host->result->stderr_truncated
                       ? NULL
                       : file_pair_source_diagnostic(sources, remaining, &evidence_size);
    char *paths[4] = { 0 };
    struct stat before[4];
    bool present[4];
    if (!raw_paths(input, paths, before, present)) {
        if (evidence) {
            append(host, evidence, evidence_size);
        }
        for (int i = 0; i < 4; i++) {
            free(paths[i]);
        }
        bool reported = evidence != NULL;
        free(evidence);
        return reported ? "invalid build artifact-pair paths; staging retained" : "invalid build artifact-pair paths; staging retained; recovery paths exceed capture capacity";
    }
    file_pair_plan plan = { .source = { paths[0], paths[2] },
                            .destination = { paths[1], paths[3] },
                            .mode = FILE_PAIR_EXCLUSIVE };
    /* Reserve the full result/diagnostic bound before backup preparation.
     * Appending after admission cannot allocate, truncate, or check READY. */
    if (host->result->stderr_truncated ||
        file_pair_diagnostic_capacity(&plan) > remaining) {
        if (evidence) {
            append(host, evidence, evidence_size);
        }
        for (int i = 0; i < 4; i++) {
            free(paths[i]);
        }
        bool reported = evidence != NULL;
        free(evidence);
        return reported
                 ? "build artifact-pair diagnostic capacity exceeded; staging "
                   "retained"
                 : "build artifact-pair diagnostic capacity exceeded; staging "
                   "retained; recovery paths exceed capture capacity";
    }
    file_pair pair;
    if (file_pair_prepare(&pair, &plan)) {
        if (!prepared_paths(&pair, paths, before, present)) {
            pair.system_error = EINVAL;
            file_pair_abort(&pair);
        } else {
            error = build_host_ready(host);
            if (error) {
                file_pair_abort(&pair);
            } else {
                results[0].i = file_pair_execute(&pair);
            }
        }
    }
    append(host, pair.diagnostic, pair.diagnostic_size);
    file_pair_dispose(&pair);
    free(evidence);
    for (int i = 0; i < 4; i++) {
        free(paths[i]);
    }
    return error;
}
