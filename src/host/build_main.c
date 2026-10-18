#include "host/build_host.h"
#include "support/xalloc.h"
#include "types/catalog.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* No game/native type packs are admitted by this private tool host. */
int type_catalog_pack_count()
{
    return 0;
}
type_catalog_pack type_catalog_pack_at(int)
{
    assert(!"build host has no native type packs");
    return (type_catalog_pack){ 0 };
}

static volatile sig_atomic_t cancelled;
static void cancel(int signal)
{
    cancelled = signal;
}
static int usage()
{
    fprintf(stderr, "usage: chibi-build-host --root DIR --source FILE --entry NAME --timeout-ms N\n");
    return 1;
}
#define BUILD_SOURCE_LIMIT (1024 * 1024)
static char *read_source(const char *path, double deadline)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return NULL;
    }
    struct stat info;
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size < 0 ||
        info.st_size > BUILD_SOURCE_LIMIT) {
        close(fd);
        return NULL;
    }
    char *text = xmalloc(BUILD_SOURCE_LIMIT + 1);
    size_t size = 0;
    for (;;) {
        if (cancelled || chibi_build_now() >= deadline || size > BUILD_SOURCE_LIMIT) {
            free(text);
            close(fd);
            return NULL;
        }
        ssize_t count = read(fd, text + size, BUILD_SOURCE_LIMIT + 1 - size);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 || (count > 0 && memchr(text + size, 0, count))) {
            free(text);
            close(fd);
            return NULL;
        }
        if (!count) {
            text[size] = 0;
            close(fd);
            return text;
        }
        size += (size_t)count;
    }
}
int main(int argc, char **argv)
{
    const char *root = NULL, *path = NULL, *entry = NULL;
    long timeout = 0;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 == argc) {
            return usage();
        }
        if (!strcmp(argv[i], "--root") && !root) {
            root = argv[i + 1];
        } else if (!strcmp(argv[i], "--source") && !path) {
            path = argv[i + 1];
        } else if (!strcmp(argv[i], "--entry") && !entry) {
            entry = argv[i + 1];
        } else if (!strcmp(argv[i], "--timeout-ms") && !timeout) {
            char *end;
            errno = 0;
            timeout = strtol(argv[i + 1], &end, 10);
            if (errno || *end || timeout <= 0 || timeout > INT_MAX) {
                return usage();
            }
        } else {
            return usage();
        }
    }
    if (!root || !path || !entry) {
        return usage();
    }
    /* Budget starts before file IO and preparation, and every child shares it. */
    double deadline = timeout ? chibi_build_now() + timeout / 1000.0 : INFINITY;
    const char *inherited = getenv("CHIBI_TEST_DEADLINE");
    if (inherited) {
        char *end;
        double limit = strtod(inherited, &end);
        if (end == inherited || *end || !isfinite(limit) || limit <= 0) {
            fprintf(stderr, "invalid inherited build deadline\n");
            return 1;
        }
        if (limit < deadline) {
            deadline = limit;
        }
    }
    if (!timeout && !inherited) {
        return usage();
    }
    if (!isfinite(deadline)) {
        fprintf(stderr, "build process host failure\n");
        return 1;
    }
    struct sigaction action = { .sa_handler = cancel };
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) {
        fprintf(stderr, "build process host failure\n");
        return 1;
    }
    size_t size = strlen(root) + strlen(path) + 2;
    char *source_path = xmalloc(size);
    if (path[0] == '/') {
        snprintf(source_path, size, "%s", path);
    } else {
        snprintf(source_path, size, "%s/%s", root, path);
    }
    char *text = read_source(source_path, deadline);
    if (!text) {
        fprintf(stderr, "build source could not be read\n");
        free(source_path);
        return 1;
    }
    chibi_build_options options = {
        .root = root,
        .source_path = source_path,
        .source_text = text,
        .entry = entry,
        .backend = BEHAVIOR_BYTECODE,
        .deadline = deadline,
        .cancelled = &cancelled,
    };
    chibi_build_result result;
    bool success = chibi_build_invoke(&options, &result);
    fwrite(result.stdout_text, 1, result.stdout_size, stdout);
    fwrite(result.stderr_text, 1, result.stderr_size, stderr);
    if (result.stdout_truncated) {
        fprintf(stderr, "build stdout truncated\n");
    }
    if (result.stderr_truncated) {
        fprintf(stderr, "build stderr truncated\n");
    }
    if (!success) {
        fprintf(stderr, "%s\n", result.diagnostic);
    }
    free(text);
    free(source_path);
    /* Authored negative signal results retain their conventional CLI encoding. */
    return success ? (result.value < 0 && result.value >= -127 ? 128 - result.value
                                                               : result.value)
                   : 1;
}
