#include "host/process.h"
#include "support/array.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) && defined(__x86_64__)
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
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

/* Signals every descendant of this process, found through the parents /proc
 * lists, so a process that moved to a group or session of its own is found
 * too. */
static void signal_descendants(int sig)
{
    int *pids = NULL, *parents = NULL, *tree = NULL;
    DIR *proc = opendir("/proc");
    assert(proc);
    for (struct dirent *entry; (entry = readdir(proc));) {
        int pid = atoi(entry->d_name), parent;
        char name[64], line[1024];
        snprintf(name, sizeof name, "/proc/%d/stat", pid);
        FILE *file = pid > 0 ? fopen(name, "r") : NULL;
        if (!file) {
            continue;
        }
        char *end = fgets(line, sizeof line, file) ? strrchr(line, ')') : NULL;
        fclose(file);
        if (end && sscanf(end + 1, " %*c %d", &parent) == 1) {
            apush(pids, pid);
            apush(parents, parent);
        }
    }
    closedir(proc);
    apush(tree, getpid());
    for (int i = 0; i < acount(tree); i++) {
        for (int j = 0; j < acount(pids); j++) {
            if (parents[j] == tree[i]) {
                apush(tree, pids[j]);
                kill(pids[j], sig);
            }
        }
    }
    afree(pids);
    afree(parents);
    afree(tree);
}

/* Kills and reaps every descendant. As a subreaper this process adopts the
 * orphans of each one it kills, so the tree is gone once nothing is left to
 * reap. */
static void reap_descendants()
{
    for (;;) {
        signal_descendants(SIGKILL);
        pid_t reaped = waitpid(-1, NULL, WNOHANG);
        if (reaped < 0) {
            assert(errno == ECHILD);
            return;
        }
        if (!reaped) {
            struct timespec pause = { .tv_nsec = 1000000 };
            nanosleep(&pause, NULL);
        }
    }
}

/* Runs `executable` as a child of a subreaper, so wait4 reports the peak
 * resident memory of the largest process the child waited for, and every
 * process it starts, in any process group, stays a descendant. A timeout,
 * SIGINT or SIGTERM kills them all, and so does the executable's exit; an
 * interrupt is raised again once they are gone. The status is -1 unless the
 * executable exits within `seconds`. */
static measured supervise(const char *executable, const char *const *argv, double seconds)
{
    const char *envp[] = { "PATH=", NULL };
    sigset_t stop, mask;
    sigemptyset(&stop);
    sigaddset(&stop, SIGINT);
    sigaddset(&stop, SIGTERM);
    assert(!sigprocmask(SIG_BLOCK, &stop, &mask));
    assert(!prctl(PR_SET_CHILD_SUBREAPER, 1));
    double start = chibi_process_now();
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        int quiet = open("/dev/null", O_RDWR);
        if (quiet < 0 || dup2(quiet, STDIN_FILENO) < 0 ||
            dup2(quiet, STDOUT_FILENO) < 0 || dup2(quiet, STDERR_FILENO) < 0 ||
            sigprocmask(SIG_SETMASK, &mask, NULL)) {
            _exit(127);
        }
        execve(executable, (char *const *)argv, (char *const *)envp);
        _exit(127);
    }
    int status = 0, interrupt = 0;
    bool stopped = false;
    struct rusage usage;
    pid_t waited;
    while (!(waited = wait4(child, &status, WNOHANG, &usage))) {
        struct timespec pause = { .tv_nsec = 1000000 };
        int caught = sigtimedwait(&stop, NULL, &pause);
        interrupt = caught > 0 ? caught : interrupt;
        if (!stopped && (interrupt || chibi_process_now() - start >= seconds)) {
            stopped = true;
            signal_descendants(SIGKILL);
        }
    }
    assert(waited == child);
    reap_descendants();
    assert(!prctl(PR_SET_CHILD_SUBREAPER, 0));
    assert(!sigprocmask(SIG_SETMASK, &mask, NULL));
    if (interrupt) {
        raise(interrupt);
    }
    measured result = { .status = !stopped && WIFEXITED(status) ? WEXITSTATUS(status)
                                                                : -1,
                        .seconds = chibi_process_now() - start,
                        .peak_kb = usage.ru_maxrss };
    return result;
}

static measured build(const char *game, const char *mode)
{
    const char *argv[] = { launcher, "build", "--native", mode,
                           "--llvm", llvm,    game,       NULL };
    return supervise(launcher, argv, BUILD_SECONDS);
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

static void pause_for(double seconds)
{
    struct timespec pause = { .tv_sec = (time_t)seconds,
                              .tv_nsec = (long)((seconds - (time_t)seconds) * 1e9) };
    while (nanosleep(&pause, &pause) && errno == EINTR) {
    }
}

/* A fake launcher leaves two background children that would each touch a
 * marker a second later: one in the launcher's process group and one that
 * starts a session of its own, as the build host's children start groups of
 * their own. Supervision must kill both when the launcher hangs past its
 * timeout and when the launcher exits first. */
TEST_CASE(native_workloads_supervision_leaves_no_descendants)
{
    static const char SETSID[] = "/usr/bin/setsid";
    REQUIRE(!access(SETSID, X_OK));
    char script[1024], grouped[1024], detached[1024], text[4096];
    path(script, suite, "launcher.sh");
    path(grouped, suite, "grouped");
    path(detached, suite, "detached");
    snprintf(
        text,
        sizeof text,
        "#!/bin/sh\n"
        "(/bin/sleep 1; : > '%s') &\n"
        "%s /bin/sh -c \"/bin/sleep 1; : > '%s'\" &\n"
        "if [ \"$1\" = hang ]; then exec /bin/sleep 30; fi\n",
        grouped,
        SETSID,
        detached
    );
    tool_write(script, text);
    REQUIRE(!chmod(script, 0700));
    const char *hang[] = { script, "hang", NULL };
    measured hung = supervise(script, hang, 0.2);
    REQUIRE(hung.status == -1 && hung.seconds < 5);
    const char *leave[] = { script, "leave", NULL };
    REQUIRE(supervise(script, leave, 10).status == 0);
    pause_for(1.5);
    struct stat info;
    REQUIRE(stat(grouped, &info) && errno == ENOENT);
    REQUIRE(stat(detached, &info) && errno == ENOENT);
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
    strcpy(suite, "/tmp/chibi-native-workloads-XXXXXX");
    assert(mkdtemp(suite));
    RUN_TEST_CASE(native_workloads_supervision_leaves_no_descendants);
    const char *bin = getenv("CHIBI_LLVM_BIN");
    if (!bin || !*bin) {
        fprintf(stderr, "native workloads: set CHIBI_LLVM_BIN to the folder of opt, llc and ld.lld\n");
        tool_remove_tree(suite);
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
    RUN_TEST_CASE(native_workloads_build_and_run);
    tool_remove_tree(suite);
#else
    (void)binary;
    fprintf(stderr, "native workloads: one Linux x86-64 case unexecuted on this platform\n");
#endif
}
