#ifndef CHIBI_HOST_BUILD_TIMING_H
#define CHIBI_HOST_BUILD_TIMING_H

/* Private, opt-in diagnostics. Each host owns one exclusive file; neither
 * argument values nor environment contents enter the bounded trace. */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define BUILD_TIMING_FILES 64

static inline FILE *build_timing_open(const char *directory)
{
    if (!directory || !directory[0]) {
        return NULL;
    }
    char path[4096];
    for (int i = 0; i < BUILD_TIMING_FILES; i++) {
        int length = snprintf(path, sizeof path, "%s/%02d.tsv", directory, i);
        if (length < 0 || (size_t)length >= sizeof path) {
            errno = ENAMETOOLONG;
            return NULL;
        }
        int fd = open(path, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
        if (fd < 0) {
            if (errno == EEXIST) {
                continue;
            }
            return NULL;
        }
        FILE *file = fdopen(fd, "w");
        if (!file) {
            close(fd);
            return NULL;
        }
        /* Preserve completed observations even if a later child times out. */
        setvbuf(file, NULL, _IOLBF, 0);
        fputs("category\tstep\tstarted\tobserved\tstatus\terror\n", file);
        return file;
    }
    errno = ENOSPC;
    return NULL;
}

static inline void build_timing_write(
    FILE *file,
    const char *category,
    int step,
    double started,
    double observed,
    int status,
    int error
)
{
    if (file) {
        fprintf(file, "%s\t%d\t%.6f\t%.6f\t%d\t%d\n", category, step, started, observed, status, error);
    }
}

static inline const char *build_timing_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static inline const char *build_timing_category(char *const *argv, bool reported)
{
    if (!reported) {
        return "probe";
    }
    const char *program = build_timing_name(argv[0]);
    for (int i = 1; argv[i]; i++) {
        if (!strcmp(argv[i], "-c")) {
            return "compile";
        }
        if (!strcmp(argv[i], "--source") && argv[i + 1]) {
            const char *source = build_timing_name(argv[i + 1]);
            if (!strcmp(source, "AssetCompiler.chibi")) {
                return "generator";
            }
            if (!strcmp(source, "Graph.chibi")) {
                return "graph-child";
            }
            if (!strcmp(source, "Build.chibi")) {
                return "build-child";
            }
        }
    }
    if (!strcmp(program, "ar") || !strcmp(program, "llvm-ar") ||
        !strcmp(program, "ranlib") || !strcmp(program, "llvm-ranlib")) {
        return "archive";
    }
    if (!strcmp(program, "reflect")) {
        return "reflection";
    }
    if (!strcmp(program, "chibi-compiler-host")) {
        return "compiler-host";
    }
    if (!strncmp(program, "clang", 5) || !strncmp(program, "gcc", 3) ||
        !strcmp(program, "cc") || !strcmp(program, "c++") || !strcmp(program, "g++")) {
        return "link";
    }
    return "process";
}
#endif
