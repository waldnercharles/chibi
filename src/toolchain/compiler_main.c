#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#include "support/xalloc.h"
#include "toolchain/compiler_host.h"
#include "types/catalog.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* This private host admits no game/native reflected type packs. */
int type_catalog_pack_count()
{
    return 0;
}
type_catalog_pack type_catalog_pack_at(int index)
{
    (void)index;
    assert(!"compiler host has no native type packs");
    return (type_catalog_pack){ 0 };
}
static volatile sig_atomic_t cancelled;
static void cancel(int signal)
{
    cancelled = signal;
}
static double now()
{
    struct timespec value;
#ifdef __APPLE__
    /* Match Python time.monotonic and the process supervisor: excludes sleep. */
    const clockid_t clock = CLOCK_UPTIME_RAW;
#else
    const clockid_t clock = CLOCK_MONOTONIC;
#endif
    return clock_gettime(clock, &value) ? INFINITY
                                        : (double)value.tv_sec + value.tv_nsec / 1e9;
}
static bool ready(double deadline)
{
    return !cancelled && now() < deadline;
}
static unsigned char *read_binary(const char *path, size_t *length, size_t limit, double deadline)
{
    *length = 0;
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return NULL;
    }
    struct stat info;
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size <= 0 ||
        (uintmax_t)info.st_size > limit || !ready(deadline)) {
        close(fd);
        return NULL;
    }
    size_t expected = (size_t)info.st_size;
    unsigned char *bytes = xmalloc(expected + 1);
    size_t used = 0;
    while (used <= expected && ready(deadline)) {
        ssize_t count = read(fd, bytes + used, expected + 1 - used);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            break;
        }
        if (!count) {
            close(fd);
            if (used != expected) {
                free(bytes);
                return NULL;
            }
            bytes[used] = 0;
            *length = used;
            return bytes;
        }
        used += (size_t)count;
    }
    close(fd);
    free(bytes);
    return NULL;
}
static bool number(const char *text, int base, uint64_t *out)
{
    if (!text || !*text ||
        !((*text >= '0' && *text <= '9') ||
          (base == 16 &&
           ((*text >= 'a' && *text <= 'f') || (*text >= 'A' && *text <= 'F'))))) {
        return false;
    }
    char *end;
    errno = 0;
    unsigned long long value = strtoull(text, &end, base);
    if (errno || *end || !value || value > UINT64_MAX) {
        return false;
    }
    *out = (uint64_t)value;
    return true;
}
static int usage()
{
    fprintf(stderr, "usage: chibi-compiler-host --artifact FILE --entry HEXID --input FILE --backend bytecode|sljit --budget N [--timeout-ms N] [--batch]\n");
    return 1;
}
int main(int argc, char **argv)
{
    const char *artifact = NULL, *input = NULL;
    uint64_t entry = 0, budget = 0, timeout = 0;
    behavior_backend backend = BEHAVIOR_UNAVAILABLE;
    bool batch = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--batch") && !batch) {
            batch = true;
            continue;
        }
        if (i + 1 == argc) {
            return usage();
        }
        const char *key = argv[i++], *value = argv[i];
        if (!strcmp(key, "--artifact") && !artifact) {
            artifact = value;
        } else if (!strcmp(key, "--input") && !input) {
            input = value;
        } else if (!strcmp(key, "--entry") && !entry) {
            if (!number(value, 16, &entry)) {
                return usage();
            }
        } else if (!strcmp(key, "--budget") && !budget) {
            if (!number(value, 10, &budget) || budget > UINTPTR_MAX) {
                return usage();
            }
        } else if (!strcmp(key, "--timeout-ms") && !timeout) {
            if (!number(value, 10, &timeout) || timeout > INT_MAX) {
                return usage();
            }
        } else if (!strcmp(key, "--backend") && backend == BEHAVIOR_UNAVAILABLE) {
            if (!strcmp(value, "bytecode")) {
                backend = BEHAVIOR_BYTECODE;
            } else if (!strcmp(value, "sljit")) {
                backend = BEHAVIOR_SLJIT;
            } else {
                return usage();
            }
        } else {
            return usage();
        }
    }
    if (!artifact || !input || !entry || !budget || backend == BEHAVIOR_UNAVAILABLE) {
        return usage();
    }
    double deadline = timeout ? now() + (double)timeout / 1000 : INFINITY;
    const char *inherited = getenv("CHIBI_TEST_DEADLINE");
    if (inherited) {
        char *end;
        errno = 0;
        double value = strtod(inherited, &end);
        if (errno || end == inherited || *end || !isfinite(value) || value <= 0) {
            fprintf(stderr, "invalid inherited compiler deadline\n");
            return 1;
        }
        if (value < deadline) {
            deadline = value;
        }
    }
    if (!isfinite(deadline)) {
        return usage();
    }
    struct sigaction action = { .sa_handler = cancel };
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) {
        return 1;
    }
    size_t artifact_size, input_size;
    unsigned char *artifact_bytes = read_binary(artifact, &artifact_size, COMPILER_HOST_MAX_BYTES, deadline);
    if (!artifact_bytes) {
        fprintf(stderr, "compiler artifact could not be read\n");
        return 1;
    }
    unsigned char *input_bytes = read_binary(input, &input_size, COMPILER_HOST_MAX_BYTES, deadline);
    if (!input_bytes) {
        free(artifact_bytes);
        fprintf(stderr, "compiler input could not be read\n");
        return 1;
    }
    char error[256] = { 0 };
    compiler_host *host = ready(deadline)
                            ? compiler_host_load(artifact_bytes, artifact_size, backend, error, sizeof error)
                            : NULL;
    free(artifact_bytes);
    compiler_host_result result = { 0 };
    bool ok = host && ready(deadline) &&
              (batch ? compiler_host_invoke_batch(
                           host,
                           entry,
                           (const char *)input_bytes,
                           input_size,
                           (uintptr_t)budget,
                           &result
                       )
                     : compiler_host_invoke(
                           host,
                           entry,
                           (const char *)input_bytes,
                           input_size,
                           (uintptr_t)budget,
                           &result
                       ));
    free(input_bytes);
    if (!ready(deadline)) {
        ok = false;
        snprintf(error, sizeof error, "compiler invocation cancelled or timed out");
    }
    if (ok) {
        ok = fwrite(result.json, 1, result.length, stdout) == result.length &&
             fputc('\n', stdout) != EOF;
        fprintf(
            stderr,
            "compiler-host bytecode=%" PRIu64 " sljit=%" PRIu64 " wasm=%" PRIu64
            " fallback=%" PRIu64 " overflow=%d\n",
            result.counts.bytecode_entries,
            result.counts.sljit_entries,
            result.counts.wasm_entries,
            result.counts.inspection_bytecode_fallbacks,
            result.counts.overflow
        );
    } else {
        fprintf(
            stderr,
            "%s\n",
            error[0]               ? error
            : result.diagnostic[0] ? result.diagnostic
                                   : "compiler artifact load failed"
        );
    }
    free(result.json);
    compiler_host_free(host);
    return ok ? 0 : 1;
}
