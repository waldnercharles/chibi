#include "host/process.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) && defined(__x86_64__)
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* W20's fixed kernels, built with `chibi build --native` and the LLVM tools
 * in CHIBI_LLVM_BIN. Each prints its compile time, the largest process's peak
 * resident memory, the program's size and its median run time at O0 and O2,
 * after checking the exit status the VM gives (`chibi test`, recorded in
 * test/native_aot/workloads/README.md). */
typedef struct
{
    const char *name;
    int status;
} workload;

static const workload WORKLOADS[] = {
    { "integrate", 161 },
    { "collide", 95 },
    { "lex", 140 },
    { "recurse", 138 },
};

enum
{
    RUNS = 3,
    BUILD_SECONDS = 300,
    RUN_SECONDS = 60,
};

static char suite[256], launcher[1024], llvm[1024];

static void path(char out[1024], const char *base, const char *name)
{
    int size = snprintf(out, 1024, "%s/%s", base, name);
    assert(size > 0 && size < 1024);
}

typedef struct
{
    int status;
    double seconds;
    long peak_kb;
} measured;

/* Runs `chibi build --native` in a child of its own, so wait4 reports the
 * peak resident memory of the largest process in the build. */
static measured build(const char *game, const char *mode)
{
    const char *argv[] = { launcher, "build", "--native", mode,
                           "--llvm", llvm,    game,       NULL };
    const char *envp[] = { "PATH=", NULL };
    double start = chibi_process_now();
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        int quiet = open("/dev/null", O_WRONLY);
        if (quiet < 0 || dup2(quiet, STDOUT_FILENO) < 0 ||
            dup2(quiet, STDERR_FILENO) < 0) {
            _exit(127);
        }
        alarm(BUILD_SECONDS);
        execve(launcher, (char *const *)argv, (char *const *)envp);
        _exit(127);
    }
    int status = 0;
    struct rusage usage;
    pid_t waited = wait4(child, &status, 0, &usage);
    assert(waited == child);
    measured result = { .status = WIFEXITED(status) ? WEXITSTATUS(status) : -1,
                        .seconds = chibi_process_now() - start,
                        .peak_kb = usage.ru_maxrss };
    return result;
}

/* The one program published for `mode` (O0 or O2) below game/build/native. */
static bool program(const char *game, const char *mode, char found[1024])
{
    char folder[1024];
    path(folder, game, "build/native");
    DIR *listing = opendir(folder);
    int count = 0;
    for (struct dirent *entry; listing && (entry = readdir(listing));) {
        if (!strncmp(entry->d_name, mode, 2) && entry->d_name[2] == '-' &&
            !strchr(entry->d_name, '.')) {
            snprintf(found, 1024, "%s/%s/program", folder, entry->d_name);
            count++;
        }
    }
    if (listing) {
        closedir(listing);
    }
    return count == 1;
}

static void forget(const char *game, const char *mode)
{
    char found[1024];
    if (!program(game, mode, found)) {
        return;
    }
    *strrchr(found, '/') = 0;
    tool_remove_tree(found);
}

static int compare(const void *a, const void *b)
{
    double left = *(const double *)a, right = *(const double *)b;
    return (left > right) - (left < right);
}

/* The median of RUNS runs, or a negative time when a run's status differs. */
static double run(const char *executable, int expected)
{
    double times[RUNS];
    for (int i = 0; i < RUNS; i++) {
        const char *argv[] = { executable, NULL };
        const char *envp[] = { NULL };
        chibi_process_output out = { 0 }, err = { 0 };
        double start = chibi_process_now();
        chibi_process_capture_result ran = chibi_process_capture(
            executable,
            argv,
            suite,
            envp,
            start + RUN_SECONDS,
            NULL,
            &out,
            &err
        );
        times[i] = chibi_process_now() - start;
        if (ran.error != CHIBI_PROCESS_OK || ran.status != expected) {
            fprintf(stderr, "native workload: %s exited %d, expected %d\n", executable, ran.status, expected);
            return -1;
        }
    }
    qsort(times, RUNS, sizeof *times, compare);
    return times[RUNS / 2];
}

static bool measure(const workload *kernel)
{
    char game[1024], source[1024], copy[1024];
    path(game, suite, kernel->name);
    REQUIRE(!mkdir(game, 0700));
    snprintf(
        source,
        sizeof source,
        CHIBI_SOURCE_ROOT "/test/native_aot/workloads/%s/Main.chibi",
        kernel->name
    );
    path(copy, game, "Main.chibi");
    char *text = tool_read(source);
    REQUIRE(text);
    tool_write(copy, text);
    free(text);
    /* The first build also compiles the emitter closure the others reuse. */
    measured cold = build(game, "-O2");
    REQUIRE(cold.status == 0);
    static const char *modes[] = { "O0", "O2" };
    for (int i = 0; i < 2; i++) {
        char flag[4], executable[1024];
        snprintf(flag, sizeof flag, "-%s", modes[i]);
        forget(game, modes[i]);
        measured warm = build(game, flag);
        REQUIRE(warm.status == 0 && program(game, modes[i], executable));
        struct stat info;
        REQUIRE(!stat(executable, &info));
        double median = run(executable, kernel->status);
        REQUIRE(median >= 0);
        printf(
            "NATIVE_WORKLOAD name=%s mode=%s cold_build_ms=%.0f build_ms=%.0f "
            "peak_rss_kb=%ld "
            "size_bytes=%lld run_ms=%.1f status=%d\n",
            kernel->name,
            modes[i],
            cold.seconds * 1000,
            warm.seconds * 1000,
            warm.peak_kb,
            (long long)info.st_size,
            median * 1000,
            kernel->status
        );
    }
    return true;
}

TEST_CASE(native_workloads_build_and_run)
{
    for (int i = 0; i < (int)(sizeof WORKLOADS / sizeof *WORKLOADS); i++) {
        REQUIRE(measure(&WORKLOADS[i]));
    }
    return true;
}
#endif

void test_native_workload_benchmark(const char *binary)
{
#if defined(__linux__) && defined(__x86_64__)
    const char *bin = getenv("CHIBI_LLVM_BIN");
    if (!bin || !*bin) {
        fprintf(stderr, "native workloads: set CHIBI_LLVM_BIN to the folder of opt, llc and ld.lld\n");
        return;
    }
    char *resolved = realpath(binary, NULL);
    assert(resolved && strlen(resolved) < sizeof launcher);
    strcpy(launcher, resolved);
    free(resolved);
    resolved = realpath(bin, NULL);
    assert(resolved && strlen(resolved) < sizeof llvm);
    strcpy(llvm, resolved);
    free(resolved);
    strcpy(suite, "/tmp/chibi-native-workloads-XXXXXX");
    assert(mkdtemp(suite));
    RUN_TEST_CASE(native_workloads_build_and_run);
    tool_remove_tree(suite);
#else
    (void)binary;
    fprintf(stderr, "native workloads: one Linux x86-64 case unexecuted on this platform\n");
#endif
}
