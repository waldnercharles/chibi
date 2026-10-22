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
    fprintf(stderr, "usage: chibi-build-host --root DIR --source FILE [--core DIR] [--collection NAME=DIR]... --entry NAME --timeout-ms N [-- ARGUMENTS...]\n");
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
/* path, from root unless it is absolute. */
static char *from_root(const char *root, const char *path)
{
    size_t size = strlen(root) + strlen(path) + 2;
    char *out = xmalloc(size);
    if (path[0] == '/') {
        snprintf(out, size, "%s", path);
    } else {
        snprintf(out, size, "%s/%s", root, path);
    }
    return out;
}
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--protocol-version")) {
        printf("%d\n", CHIBI_BUILD_HOST_PROTOCOL);
        return 0;
    }
    const char *root = NULL, *path = NULL, *core = NULL, *entry = NULL;
    long timeout = 0;
    int arguments_start = argc;
    const char *collections[CHIBI_BUILD_MAX_COLLECTIONS];
    int collection_count = 0;
    for (int i = 1; i < argc; i += 2) {
        if (!strcmp(argv[i], "--")) {
            arguments_start = i + 1;
            break;
        }
        if (i + 1 == argc) {
            return usage();
        }
        if (!strcmp(argv[i], "--root") && !root) {
            root = argv[i + 1];
        } else if (!strcmp(argv[i], "--source") && !path) {
            path = argv[i + 1];
        } else if (!strcmp(argv[i], "--core") && !core) {
            core = argv[i + 1];
        } else if (!strcmp(argv[i], "--collection") && collection_count < CHIBI_BUILD_MAX_COLLECTIONS) {
            const char *equals = strchr(argv[i + 1], '=');
            if (!equals || equals == argv[i + 1] || !equals[1]) {
                return usage();
            }
            collections[collection_count++] = argv[i + 1];
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
    if (!root || !path || !entry || !timeout) {
        return usage();
    }
    /* Budget starts before file IO and preparation, and every child shares it. */
    double deadline = chibi_build_now() + timeout / 1000.0;
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
    char *source_path = from_root(root, path);
    char *core_path = NULL;
    if (core) {
        char *joined = from_root(root, core);
        core_path = realpath(joined, NULL);
        free(joined);
        if (!core_path) {
            fprintf(stderr, "build core collection could not be found\n");
            free(source_path);
            return 1;
        }
    }
    chibi_build_collection named[CHIBI_BUILD_MAX_COLLECTIONS];
    char *roots[CHIBI_BUILD_MAX_COLLECTIONS] = { 0 };
    bool found = true;
    for (int i = 0; i < collection_count; i++) {
        const char *equals = strchr(collections[i], '=');
        char *joined = from_root(root, equals + 1);
        roots[i] = realpath(joined, NULL);
        free(joined);
        found &= roots[i] != NULL;
        size_t length = (size_t)(equals - collections[i]);
        char *name = xmalloc(length + 1);
        memcpy(name, collections[i], length);
        name[length] = 0;
        named[i] = (chibi_build_collection){ name, roots[i] };
    }
    char *text = found ? read_source(source_path, deadline) : NULL;
    if (!text) {
        fprintf(stderr, found ? "build source could not be read\n" : "build collection could not be found\n");
        free(source_path);
        free(core_path);
        for (int i = 0; i < collection_count; i++) {
            free((char *)named[i].name);
            free(roots[i]);
        }
        return 1;
    }
    chibi_build_options options = {
        .root = root,
        .source_path = source_path,
        .source_text = text,
        .core = core_path,
        .collections = named,
        .collection_count = collection_count,
        .entry = entry,
        .arguments = (const char *const *)&argv[arguments_start],
        .argument_count = argc - arguments_start,
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
    free(core_path);
    for (int i = 0; i < collection_count; i++) {
        free((char *)named[i].name);
        free(roots[i]);
    }
    /* Authored negative signal results retain their conventional CLI encoding. */
    return success ? (result.value < 0 && result.value >= -127 ? 128 - result.value
                                                               : result.value)
                   : 1;
}
