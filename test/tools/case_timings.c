#include "third_party/pico_headers/pico_unit.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Only main.c renames the vendor implementation. Other translation units keep
 * pico's normal entry point; no fixture setup/teardown hook is replaced. */
void tool_pico_run_test(const char *name, pu_test_fn test);

static FILE *output;
static bool initialized, stopped;
static unsigned ordinal;
enum
{
    CASE_LIMIT = 4096,
    NAME_LIMIT = 128
};

static void stop(const char *reason)
{
    fprintf(stderr, "case timings: %s; remaining cases unmeasured\n", reason);
    if (output) {
        fclose(output);
        output = NULL;
    }
    stopped = true;
}

static void initialize()
{
    initialized = true;
    const char *directory = getenv("CHIBI_TEST_TIMINGS");
    if (!directory || !*directory) {
        return;
    }
    char path[4096];
    int n = snprintf(path, sizeof path, "%s/cases-XXXXXX", directory);
    if (n < 0 || (size_t)n >= sizeof path) {
        stop("sink path too long");
        return;
    }
    int fd = mkstemp(path);
    if (fd < 0) {
        stop("cannot create private sink");
        return;
    }
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 || !(output = fdopen(fd, "w"))) {
        close(fd);
        unlink(path);
        stop("cannot open private sink");
        return;
    }
    if (fputs("# chibi-case-wall-v1; suite/startup residual unmeasured\nevent\tordinal\tcase\twall_ns\n", output) <
            0 ||
        fflush(output)) {
        stop("cannot write sink");
    }
}

static bool now(int64_t *value)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time)) {
        stop("monotonic clock unavailable");
        return false;
    }
    *value = (int64_t)time.tv_sec * 1000000000 + time.tv_nsec;
    return true;
}

void pu_run_test(const char *const name, pu_test_fn test)
{
    if (!initialized) {
        initialize();
    }
    if (!output || stopped) {
        tool_pico_run_test(name, test);
        return;
    }
    if (ordinal == CASE_LIMIT) {
        fputs("# truncated: case limit reached\n", output);
        stop("case limit reached");
        tool_pico_run_test(name, test);
        return;
    }
    size_t length = strnlen(name, NAME_LIMIT + 1);
    if (!length || length > NAME_LIMIT ||
        strspn(name, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
            length) {
        fputs("# truncated: unsupported case name\n", output);
        stop("unsupported case name");
        tool_pico_run_test(name, test);
        return;
    }
    unsigned index = ++ordinal;
    if (fprintf(output, "begin\t%u\t%s\t0\n", index, name) < 0 || fflush(output)) {
        stop("cannot write sink");
        tool_pico_run_test(name, test);
        return;
    }
    int64_t start, end;
    bool measured = now(&start);
    tool_pico_run_test(name, test);
    if (!measured || !now(&end)) {
        return;
    }
    if (fprintf(output, "end\t%u\t%s\t%lld\n", index, name, (long long)(end - start)) < 0 ||
        fflush(output)) {
        stop("cannot write sink");
    }
}
