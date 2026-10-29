#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#include "support/xalloc.h"
#include "toolchain/compiler_bootstrap.h"
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
#ifdef _WIN32
#include <windows.h>
#endif

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
    const int clock = CLOCK_UPTIME_RAW;
#else
    const int clock = CLOCK_MONOTONIC;
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
#ifdef _WIN32
    int fd = open(path, O_RDONLY | _O_NOINHERIT);
#else
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
#endif
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
/* Same-directory rename is the sole commit point. This is
 * atomic visibility, not fsync-backed durability. A hard kill may leave staging. */
static bool write_binary(const char *path, const unsigned char *bytes, size_t length, double deadline)
{
    if (!ready(deadline)) {
        return false;
    }
    size_t path_length = strlen(path);
    if (path_length > SIZE_MAX - sizeof ".XXXXXX") {
        return false;
    }
    char *temporary = xmalloc(path_length + sizeof ".XXXXXX");
    memcpy(temporary, path, path_length);
    memcpy(temporary + path_length, ".XXXXXX", sizeof ".XXXXXX");
    int fd = mkstemp(temporary);
    if (fd < 0) {
        free(temporary);
        return false;
    }
#ifdef _WIN32
    bool ok = SetHandleInformation((HANDLE)_get_osfhandle(fd), HANDLE_FLAG_INHERIT, 0) !=
              0;
#else
    bool ok = fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
#endif
    size_t used = 0;
    while (ok && used < length) {
        if (!ready(deadline)) {
            ok = false;
            break;
        }
        size_t chunk = length - used;
        if (chunk > 4096) {
            chunk = 4096;
        }
        ssize_t count = write(fd, bytes + used, chunk);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            ok = false;
            break;
        }
        used += (size_t)count;
    }
    /* Do not retry close after EINTR: the descriptor may already be closed. */
    if (close(fd)) {
        ok = false;
    }
    if (ok && ready(deadline) && rename(temporary, path) == 0) {
        free(temporary);
        return true; /* No postcommit I/O, deadline or cancellation checks. */
    }
    unlink(temporary);
    free(temporary);
    return false;
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
    fprintf(
        stderr,
        "usage: chibi-compiler-host --artifact FILE --entry HEXID --input FILE "
        "--backend bytecode|sljit --budget N --timeout-ms N [--batch | "
        "--output-artifact FILE | --output-bytes FILE]\n"
        "       chibi-compiler-host --target-json [OUTPUT]\n"
        "       chibi-compiler-host --bootstrap DIR --compiler DIR --core DIR "
        "[--seed DIR [--revision TEXT] | --recover FILE]\n"
    );
    return 1;
}
/* --bootstrap DIR checks the bootstrap artifact in DIR against the compiler
 * sources, and with --recover writes the compiler it recovers; with --seed it
 * writes an artifact from a seed instead. */
static int bootstrap(const compiler_bootstrap *b)
{
    char report[512] = { 0 };
    bool ok = b->seed ? compiler_bootstrap_write(b, report, sizeof report)
                      : compiler_bootstrap_check(b, report, sizeof report);
    if (!ok) {
        fprintf(stderr, "bootstrap: %s\n", report);
        return 1;
    }
    if (b->seed) {
        printf("wrote the bootstrap artifact %s\n", b->folder);
    } else {
        printf("%s\n", report);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if ((argc == 2 || argc == 3) && !strcmp(argv[1], "--target-json")) {
        size_t length;
        char error[256] = { 0 };
        char *json = compiler_host_compiler_target_json(&length, error, sizeof error);
        if (!json) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        FILE *output = argc == 3 ? fopen(argv[2], "wb") : stdout;
        bool ok = output && fwrite(json, 1, length, output) == length &&
                  !fflush(output);
        if (output && output != stdout) {
            ok = !fclose(output) && ok;
        }
        free(json);
        return ok ? 0 : 1;
    }

    const char *artifact = NULL, *input = NULL, *output_artifact = NULL;
    /* --output-bytes writes a byte[] result that is not a compiler, such as
     * a game's modules, without loading it as one. */
    bool raw = false;
    uint64_t entry = 0, budget = 0, timeout = 0;
    behavior_backend backend = BEHAVIOR_UNAVAILABLE;
    bool batch = false;
    compiler_bootstrap boot = { 0 };
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--batch") && !batch) {
            batch = true;
            continue;
        }
        if (i + 1 == argc) {
            return usage();
        }
        const char *key = argv[i++], *value = argv[i];
        const char **bootstrap_value = !strcmp(key, "--bootstrap") ? &boot.folder
                                     : !strcmp(key, "--compiler") ? &boot.compiler
                                     : !strcmp(key, "--core") ? &boot.core
                                     : !strcmp(key, "--seed") ? &boot.seed
                                     : !strcmp(key, "--revision") ? &boot.revision
                                     : !strcmp(key, "--recover") ? &boot.output
                                                                 : NULL;
        if (bootstrap_value && !*bootstrap_value && *value) {
            *bootstrap_value = value;
        } else if (bootstrap_value) {
            return usage();
        } else if (!strcmp(key, "--artifact") && !artifact) {
            artifact = value;
        } else if (!strcmp(key, "--input") && !input) {
            input = value;
        } else if ((!strcmp(key, "--output-artifact") || !strcmp(key, "--output-bytes")) && !output_artifact && *value) {
            output_artifact = value;
            raw = !strcmp(key, "--output-bytes");
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
    if (boot.folder || boot.compiler || boot.core || boot.seed ||
        boot.revision || boot.output) {
        bool alone = !artifact && !input && !output_artifact && !entry && !budget &&
                     !timeout && backend == BEHAVIOR_UNAVAILABLE && !batch;
        bool complete = boot.folder && boot.compiler && boot.core &&
                        (boot.seed || !boot.revision) && !(boot.seed && boot.output);
        return alone && complete ? bootstrap(&boot) : usage();
    }
    if (!artifact || !input || !entry || !budget || !timeout ||
        backend == BEHAVIOR_UNAVAILABLE || (batch && output_artifact)) {
        return usage();
    }
    double deadline = now() + (double)timeout / 1000;
    if (!isfinite(deadline)) {
        return usage();
    }
#ifdef _WIN32
    if (signal(SIGINT, cancel) == SIG_ERR || signal(SIGTERM, cancel) == SIG_ERR) {
        return 1;
    }
#else
    struct sigaction action = { .sa_handler = cancel };
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) {
        return 1;
    }
    if (output_artifact) {
        struct sigaction ignore = { .sa_handler = SIG_IGN };
        sigemptyset(&ignore.sa_mask);
        if (sigaction(SIGPIPE, &ignore, NULL)) {
            return 1;
        }
        /* Keep SIGPIPE ignored through normal stdio cleanup at exit. */
    }
#endif
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
    if (output_artifact) {
        compiler_host_bytes result = { 0 };
        bool ok = host && ready(deadline) &&
                  compiler_host_invoke_bytes(
                      host,
                      entry,
                      (const char *)input_bytes,
                      input_size,
                      (uintptr_t)budget,
                      COMPILER_HOST_MAX_ARTIFACT_BYTES,
                      &result
                  );
        free(input_bytes);
        compiler_host_free(host);
        if (!ready(deadline)) {
            ok = false;
            snprintf(error, sizeof error, "compiler invocation cancelled or timed out");
        }
        if (ok && raw && !result.length) {
            ok = false;
            snprintf(error, sizeof error, "the compiler entry returned no bytes");
        }
        if (ok && !raw) {
            /* Runtime-only validation, never source compilation or serialization.
             * Bytecode avoids preparing a second selected-backend JIT. */
            compiler_host *validation = compiler_host_load(
                result.bytes,
                result.length,
                BEHAVIOR_BYTECODE,
                error,
                sizeof error
            );
            ok = validation != NULL;
            compiler_host_free(validation);
        }
        if (ok) {
            /* All fallible reporting precedes the publication commit. A closed
             * stderr pipe is an ordinary precommit failure with SIGPIPE ignored. */
            ok = fprintf(
                     stderr,
                     "compiler-host bytecode=%" PRIu64 " sljit=%" PRIu64
                     " wasm=%" PRIu64 " fallback=%" PRIu64 " overflow=%d\n",
                     result.counts.bytecode_entries,
                     result.counts.sljit_entries,
                     result.counts.wasm_entries,
                     result.counts.inspection_bytecode_fallbacks,
                     result.counts.overflow
                 ) >= 0;
            if (fflush(stderr)) {
                ok = false;
            }
            if (!ok) {
                snprintf(error, sizeof error, "compiler counters could not be written");
            }
        }
        if (ok) {
            ok = write_binary(output_artifact, result.bytes, result.length, deadline);
            if (!ok) {
                snprintf(error, sizeof error, "compiler output could not be published");
            }
        }
        if (!ok) {
            fprintf(
                stderr,
                "%s\n",
                error[0]               ? error
                : result.diagnostic[0] ? result.diagnostic
                                       : "compiler artifact load failed"
            );
        }
        free(result.bytes);
        return ok ? 0 : 1;
    }
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
