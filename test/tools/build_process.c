#include "host/build_host.h"
#include "host/build_timing.h"
#include "host/build_values.h"
#include "support/binary_cache.h"
#include "support/sha256.h"
#include "types/text.h"
#ifdef CHIBI_TEST_NATIVE_RUNTIME
#include "host/native_default.h"
#endif
#include "host/process.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static char root[128];
static const char *worker_path;
static const char *host_path;
static chibi_build_compiler *build_compiler;
static char *saved_secret, *saved_allowed, *saved_worker;
static char *fixture_source;
static char output[1024], errors[1024];
static chibi_process_output out, err;
static volatile sig_atomic_t cancelled;

/* Trace order is wait order, not child exit order. Check each interval rather
 * than treating parallel/nested elapsed intervals as additive wall time. */
static int timing_rows(FILE *file, const char *wanted, int wanted_status, int wanted_error)
{
    rewind(file);
    char line[256], category[32];
    int count = 0;
    while (fgets(line, sizeof line, file)) {
        if (!strncmp(line, "category\t", 9)) {
            continue;
        }
        int step, status, error;
        double started, observed;
        if (sscanf(line, "%31s%d%lf%lf%d%d", category, &step, &started, &observed, &status, &error) !=
                6 ||
            !isfinite(started) || !isfinite(observed) || started <= 0 ||
            observed < started || step < 0) {
            return -1;
        }
        count += !strcmp(category, wanted) && status == wanted_status &&
                 error == wanted_error;
    }
    return ferror(file) ? -1 : count;
}

static double timing_observed(FILE *file, int wanted)
{
    rewind(file);
    char line[256], category[32];
    while (fgets(line, sizeof line, file)) {
        int step, status, error;
        double started, observed;
        if (sscanf(line, "%31s%d%lf%lf%d%d", category, &step, &started, &observed, &status, &error) ==
                6 &&
            step == wanted && !strcmp(category, "process")) {
            return observed;
        }
    }
    return 0;
}

/* A compile rejection reads "path:line:column: message"; other failures are
 * the message alone. */
static bool diagnostic_is(const char *actual, const char *expected)
{
    size_t have = strlen(actual), want = strlen(expected);
    if (have == want) {
        return !strcmp(actual, expected);
    }
    return have > want + 2 && !strcmp(actual + have - want, expected) &&
           actual[have - want - 1] == ' ' && actual[have - want - 2] == ':';
}
TEST_CASE(build_timings_are_opt_in_bounded_and_exclusive)
{
    REQUIRE(!build_timing_open(NULL) && !build_timing_open(""));
    char directory[256];
    snprintf(directory, sizeof directory, "%s/timings", root);
    REQUIRE(!mkdir(directory, 0700));
    for (int i = 0; i < BUILD_TIMING_FILES; i++) {
        FILE *file = build_timing_open(directory);
        REQUIRE(file);
        build_timing_write(file, "compile", i, 1, 2, 0, 0);
        REQUIRE(!fclose(file));
    }
    REQUIRE(!build_timing_open(directory) && errno == ENOSPC);
    char path[512];
    snprintf(path, sizeof path, "%s/00.tsv", directory);
    FILE *file = fopen(path, "r");
    REQUIRE(file);
    REQUIRE(timing_rows(file, "compile", 0, 0) == 1);
    REQUIRE(!fclose(file));
    return true;
}

TEST_CASE(build_timings_classify_without_recording_arguments)
{
    char *compile[] = { "/usr/bin/ccache",  "/usr/bin/clang", "-c",
                        "private-source.c", "-DSECRET=value", NULL };
    char *link[] = { "/usr/bin/clang++", "-o", "private-output", NULL };
    char *archive[] = { "/usr/bin/llvm-ar", "rcs", "private-output", NULL };
    char *reflect[] = { "/build/reflect", NULL };
    char *generator[] = { "/build/chibi-build-host",
                          "--source",
                          "tools/toolchain/AssetCompiler.chibi",
                          NULL };
    REQUIRE(!strcmp(build_timing_category(compile, true), "compile"));
    REQUIRE(!strcmp(build_timing_category(link, true), "link"));
    REQUIRE(!strcmp(build_timing_category(archive, true), "archive"));
    REQUIRE(!strcmp(build_timing_category(reflect, true), "reflection"));
    REQUIRE(!strcmp(build_timing_category(generator, true), "generator"));
    REQUIRE(!strcmp(build_timing_category(compile, false), "probe"));
    FILE *file = tmpfile();
    REQUIRE(file);
    build_timing_write(file, build_timing_category(compile, true), 1, 1, 2, 7, 0);
    REQUIRE(timing_rows(file, "compile", 7, 0) == 1);
    rewind(file);
    char text[256] = { 0 };
    REQUIRE(fread(text, 1, sizeof text - 1, file) > 0);
    REQUIRE(!strstr(text, "private") && !strstr(text, "SECRET") && !strstr(text, "clang"));
    REQUIRE(!fclose(file));
    return true;
}

static double now()
{
    return chibi_build_now();
}

static double deadline_after(double seconds)
{
    return now() + seconds;
}

static chibi_process_capture_result capture(const char *const *argv, const char *const *envp, double seconds)
{
    out = (chibi_process_output){ .data = output, .capacity = sizeof output };
    err = (chibi_process_output){ .data = errors, .capacity = sizeof errors };
    return chibi_process_capture(argv[0], argv, root, envp, deadline_after(seconds), &cancelled, &out, &err);
}

TEST_CASE(argv_cwd_and_explicit_environment)
{
    char parent_cwd[4096], after_cwd[4096];
    REQUIRE(getcwd(parent_cwd, sizeof parent_cwd));
    REQUIRE(!setenv("CHIBI_BUILD_SECRET", "parent-only", 1));
    REQUIRE(!unsetenv("CHIBI_BUILD_ALLOWED"));
    const char *argv[] = { worker_path, "build-argv", "", "space \"quote\";$literal*", NULL };
    const char *envp[] = { "PATH=/usr/bin:/bin", "CHIBI_BUILD_ALLOWED=child-only", NULL };
    chibi_process_capture_result result = capture(argv, envp, 2);
    REQUIRE(result.error == CHIBI_PROCESS_OK);
    REQUIRE(result.status == 23);
    char expected[1024];
    snprintf(expected, sizeof expected, "cwd=%s\nallowed=child-only\nsecret=\narg=0:\narg=23:space \"quote\";$literal*\n", root);
    REQUIRE(!strcmp(output, expected));
    REQUIRE(!strcmp(errors, "worker-stderr\n"));
    REQUIRE(!out.truncated && !err.truncated);
    REQUIRE(!strcmp(getenv("CHIBI_BUILD_SECRET"), "parent-only"));
    REQUIRE(!getenv("CHIBI_BUILD_ALLOWED"));
    REQUIRE(getcwd(after_cwd, sizeof after_cwd));
    REQUIRE(!strcmp(parent_cwd, after_cwd));
    return true;
}

TEST_CASE(separate_output_bounds)
{
    const char *argv[] = { worker_path, "build-output", "16384", NULL };
    const char *envp[] = { "PATH=/usr/bin:/bin", NULL };
    chibi_process_capture_result result = capture(argv, envp, 2);
    REQUIRE(result.error == CHIBI_PROCESS_OK && result.status == 0);
    REQUIRE(out.size == sizeof output - 1 && err.size == sizeof errors - 1);
    REQUIRE(out.truncated && err.truncated);
    REQUIRE(!output[out.size] && !errors[err.size]);
    for (size_t i = 0; i < out.size; i++) {
        REQUIRE(output[i] == 'o');
        REQUIRE(errors[i] == 'e');
    }
    char single = 'x';
    out = (chibi_process_output){ .data = &single, .capacity = 1 };
    err = (chibi_process_output){ 0 };
    result = chibi_process_capture(argv[0], argv, root, envp, deadline_after(2), NULL, &out, &err);
    REQUIRE(result.error == CHIBI_PROCESS_OK && result.status == 0);
    REQUIRE(!single && !out.size && !err.size);
    REQUIRE(out.truncated && err.truncated);
    return true;
}

TEST_CASE(exits_and_spawn_errors)
{
    const char *envp[] = { "PATH=/usr/bin:/bin", NULL };
    const char *missing[] = { "/chibi/nonexistent/build-worker", NULL };
    chibi_process_capture_result result = capture(missing, envp, 2);
    REQUIRE(result.error == CHIBI_PROCESS_SPAWN_FAILED);
    REQUIRE(result.system_error == ENOENT);
    const char *exit[] = { worker_path, "exit", "71", NULL };
    result = capture(exit, envp, 2);
    REQUIRE(result.error == CHIBI_PROCESS_OK && result.status == 71);
    const char *signal[] = { worker_path, "build-signal", "unused", NULL };
    result = capture(signal, envp, 2);
    REQUIRE(result.error == CHIBI_PROCESS_OK && result.status == -SIGTERM);
    const char *relative[] = { "policy_worker", "exit", "0", NULL };
    result = capture(relative, envp, 2);
    REQUIRE(result.error == CHIBI_PROCESS_INVALID_INPUT);
    return true;
}

static void cancel_alarm(int signal)
{
    (void)signal;
    cancelled = 1;
}

TEST_CASE(timeout_and_active_cancellation)
{
    const char *argv[] = { worker_path, "sleep", "3", NULL };
    const char *envp[] = { "PATH=/usr/bin:/bin", NULL };
    double start = now();
    chibi_process_capture_result result = capture(argv, envp, .05);
    REQUIRE(result.error == CHIBI_PROCESS_TIMEOUT);
    REQUIRE(now() - start < 1);
    struct sigaction action = { .sa_handler = cancel_alarm }, previous;
    sigemptyset(&action.sa_mask);
    REQUIRE(!sigaction(SIGALRM, &action, &previous));
    struct itimerval timer = { .it_value = { .tv_usec = 50000 } }, previous_timer;
    assert(!setitimer(ITIMER_REAL, &timer, &previous_timer));
    start = now();
    result = capture(argv, envp, 2);
    timer = (struct itimerval){ 0 };
    assert(!setitimer(ITIMER_REAL, &timer, NULL));
    assert(!sigaction(SIGALRM, &previous, NULL));
    assert(!setitimer(ITIMER_REAL, &previous_timer, NULL));
    cancelled = 0;
    REQUIRE(result.error == CHIBI_PROCESS_CANCELLED);
    REQUIRE(now() - start < 1);
    return true;
}

static bool descendant_gone()
{
    char path[256];
    snprintf(path, sizeof path, "%s/started", root);
    FILE *file = fopen(path, "r");
    if (!file) {
        return false;
    }
    int pid = 0;
    bool valid = fscanf(file, "%d", &pid) == 1 && pid > 0;
    fclose(file);
    if (!valid) {
        return false;
    }
    const char *argv[] = { worker_path, "cleanup-probe", root, NULL };
    const char *envp[] = { "PATH=/usr/bin:/bin", NULL };
    chibi_process_capture_result result = capture(argv, envp, .5);
    return result.error == CHIBI_PROCESS_OK && !result.status;
}

TEST_CASE(descendants_stop_on_timeout_and_leader_exit)
{
    const char *envp[] = { "PATH=/usr/bin:/bin", NULL };
    const char *running[] = { worker_path, "descendant", root, "0", "0", NULL };
    chibi_process_capture_result result = capture(running, envp, .2);
    REQUIRE(result.error == CHIBI_PROCESS_TIMEOUT);
    REQUIRE(descendant_gone());
    char path[256];
    snprintf(path, sizeof path, "%s/started", root);
    REQUIRE(!unlink(path));
    const char *exited[] = { worker_path, "descendant", root, "0", "1", NULL };
    result = capture(exited, envp, 2);
    REQUIRE(result.error == CHIBI_PROCESS_OK && !result.status);
    REQUIRE(descendant_gone());
    REQUIRE(!unlink(path));
    struct sigaction action = { .sa_handler = cancel_alarm }, previous;
    sigemptyset(&action.sa_mask);
    REQUIRE(!sigaction(SIGALRM, &action, &previous));
    struct itimerval timer = { .it_value = { .tv_usec = 200000 } }, previous_timer;
    assert(!setitimer(ITIMER_REAL, &timer, &previous_timer));
    result = capture(running, envp, 2);
    timer = (struct itimerval){ 0 };
    assert(!setitimer(ITIMER_REAL, &timer, NULL));
    assert(!sigaction(SIGALRM, &previous, NULL));
    assert(!setitimer(ITIMER_REAL, &previous_timer, NULL));
    cancelled = 0;
    REQUIRE(result.error == CHIBI_PROCESS_CANCELLED);
    REQUIRE(descendant_gone());
    return true;
}

TEST_CASE(authored_fixture_runs_real_children_on_each_backend)
{
    REQUIRE(!setenv("CHIBI_BUILD_WORKER", worker_path, 1));
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        chibi_build_options options = {
            .compiler = build_compiler,
            .root = root,
            .source_path = "BuildProcessFixture.chibi",
            .source_text = fixture_source,
            .entry = "BuildFixture",
            .backend = backends[b],
            .deadline = now() + 2,
        };
        chibi_build_result result;
        bool success = chibi_build_invoke(&options, &result);
        if (!success) {
            fprintf(stderr, "build fixture: %s\n", result.diagnostic);
        }
        REQUIRE(success);
        REQUIRE(result.value == 23 && result.steps == 1);
        REQUIRE(!result.counts.overflow && !result.counts.inspection_bytecode_fallbacks);
        REQUIRE(result.counts.bytecode_entries == (b == 0 ? 1u : 0u));
        REQUIRE(result.counts.sljit_entries == (b == 1 ? 1u : 0u));
        REQUIRE(!result.counts.wasm_entries);
        char expected[1024];
        snprintf(
            expected,
            sizeof expected,
            "cwd=%s\nallowed=\nsecret=\narg=0:\narg=23:space "
            "\"quote\";$literal*\n",
            root
        );
        REQUIRE(!strcmp(result.stdout_text, expected));
        REQUIRE(!strcmp(result.stderr_text, "worker-stderr\n"));
        REQUIRE(result.stdout_size == strlen(expected));
        REQUIRE(!result.stdout_truncated && !result.stderr_truncated);
    }
    return true;
}

TEST_CASE(authored_capability_errors_do_not_become_exit_codes)
{
    static const char *sources[] = {
        "package failure\nBuild :: () -> int { return "
        "build.run([\"/chibi/nonexistent/worker\"]); }",
        "package failure\nBuild :: () -> int { return "
        "build.run([\"\"]); "
        "}",
        "package failure\nBuild :: () -> int { return "
        "build.run([build.environment(\"CHIBI_BUILD_WORKER\"), \"sleep\", "
        "\"3\"]); }",
    };
    static const char *diagnostics[] = {
        "build process spawn failed",
        "invalid build process arguments",
        "build process timeout",
    };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 3; i++) {
            chibi_build_options options = {
                .compiler = build_compiler,
                .root = root,
                .source_path = "Failure.chibi",
                .source_text = sources[i],
                .entry = "Build",
                .backend = backends[b],
                .deadline = now() + (i == 2 ? .5 : 2),
            };
            chibi_build_result result;
            REQUIRE(!chibi_build_invoke(&options, &result));
            REQUIRE(!strcmp(result.diagnostic, diagnostics[i]));
            REQUIRE(!result.counts.overflow && !result.counts.inspection_bytecode_fallbacks);
            REQUIRE(result.counts.bytecode_entries == (b == 0 ? 1u : 0u));
            REQUIRE(result.counts.sljit_entries == (b == 1 ? 1u : 0u));
            REQUIRE(result.steps == (i == 1 ? 0 : 1));
        }
        volatile sig_atomic_t stop = 1;
        chibi_build_options options = {
            .compiler = build_compiler,
            .root = root,
            .source_path = "BuildProcessFixture.chibi",
            .source_text = fixture_source,
            .entry = "BuildFixture",
            .backend = backends[b],
            .deadline = now() + 2,
            .cancelled = &stop,
        };
        chibi_build_result result;
        REQUIRE(!chibi_build_invoke(&options, &result));
        REQUIRE(!strcmp(result.diagnostic, "build process cancelled"));
        REQUIRE(
            !result.steps && !result.counts.bytecode_entries && !result.counts.sljit_entries
        );
    }
    return true;
}

TEST_CASE(original_workflow_preserves_order_and_failure_on_real_host)
{
    char *workflow = tool_read(CHIBI_SOURCE_ROOT "/test/language/BuildWorkflow.chibi");
    REQUIRE(workflow);
    char *saved_path = getenv("PATH") ? strdup(getenv("PATH")) : NULL;
    char worker_path_env[256];
    snprintf(worker_path_env, sizeof worker_path_env, "%s:/usr/bin:/bin", root);
    assert(!setenv("PATH", worker_path_env, 1));
    char shim[256], marker[256];
    snprintf(shim, sizeof shim, "%s/cmake", root);
    assert(!symlink(worker_path, shim));
    bool valid = true;
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        for (int mode = 0; mode < 3; mode++) {
            if (mode) {
                snprintf(marker, sizeof marker, "%s/%s", root, mode == 1 ? "fail-configure" : "fail-build");
                tool_write(marker, "");
            }
            chibi_build_options options = {
                .compiler = build_compiler,
                .root = root,
                .source_path = "BuildWorkflow.chibi",
                .source_text = workflow,
                .entry = "Build",
                .backend = backends[b],
                .deadline = now() + 2,
            };
            chibi_build_result result;
            bool success = chibi_build_invoke(&options, &result);
            valid &= success && result.value == (mode == 1   ? 37
                                                 : mode == 2 ? 38
                                                             : 0);
            valid &= result.steps == (mode == 1 ? 1 : 2);
            valid &= !strcmp(result.stdout_text, mode == 1 ? "configure\n" : "configure\nbuild\n");
            valid &= !result.stderr_size && !result.stdout_truncated &&
                     !result.stderr_truncated;
            valid &= !result.counts.overflow &&
                     !result.counts.inspection_bytecode_fallbacks;
            valid &= result.counts.bytecode_entries == (b == 0 ? 1u : 0u);
            valid &= result.counts.sljit_entries == (b == 1 ? 1u : 0u);
            if (mode) {
                assert(!unlink(marker));
            }
        }
    }
    assert(!unlink(shim));
    if (saved_path) {
        assert(!setenv("PATH", saved_path, 1));
    } else {
        assert(!unsetenv("PATH"));
    }
    free(saved_path);
    free(workflow);
    REQUIRE(valid);
    return true;
}

TEST_CASE(private_host_protocol_probe_precedes_source_and_budget_requirements)
{
    char log[256];
    snprintf(log, sizeof log, "%s/protocol.log", root);
    const char *probe[] = { host_path, "--protocol-version", NULL };
    REQUIRE(!tool_run(probe, root, log));
    char *text = tool_read(log);
    bool current = text && !strcmp(text, "14\n");
    free(text);
    REQUIRE(current);
    /* A nonzero probe result, as produced by the M1 CLI, must be distinguishable
     * from an invocation error after a successful version-14 selection. */
    const char *invalid[] = { host_path, "--protocol-version", "extra", NULL };
    REQUIRE(tool_run(invalid, root, log) != 0);
    text = tool_read(log);
    bool rejected = text && strstr(text, "usage:") && strcmp(text, "14\n");
    free(text);
    REQUIRE(rejected);
    return true;
}

TEST_CASE(private_host_cli_executes_source_and_reports_errors)
{
    char source[256], log[256];
    snprintf(source, sizeof source, "%s/BuildProcessFixture.chibi", root);
    snprintf(log, sizeof log, "%s/cli.log", root);
    tool_write(source, fixture_source);
    const char *argv[] = {
        host_path,
        "--compiler",
        CHIBI_SOURCE_ROOT "/tools/compiler",
        "--compiler-core",
        CHIBI_SOURCE_ROOT "/packages/core",
        "--root",
        root,
        "--source",
        source,
        "--entry",
        "BuildFixture",
        "--timeout-ms",
        "10000",
        NULL,
    };
    REQUIRE(tool_run(argv, root, log) == 23);
    char *text = tool_read(log);
    bool emitted = text && strstr(text, "arg=0:\n") && strstr(text, "worker-stderr");
    free(text);
    REQUIRE(emitted);
    const char *missing[] = {
        host_path,
        "--compiler",
        CHIBI_SOURCE_ROOT "/tools/compiler",
        "--compiler-core",
        CHIBI_SOURCE_ROOT "/packages/core",
        "--root",
        root,
        "--source",
        source,
        "--entry",
        "Absent",
        "--timeout-ms",
        "10000",
        NULL,
    };
    REQUIRE(tool_run(missing, root, log) != 0);
    text = tool_read(log);
    bool diagnostic = text && strstr(text, "entry");
    free(text);
    REQUIRE(diagnostic);
    /* --core names the collection a program's core: imports read. */
    char directory[256], core_source[256];
    snprintf(directory, sizeof directory, "%s/core-program", root);
    assert(!mkdir(directory, 0700));
    snprintf(core_source, sizeof core_source, "%s/CoreProgram.chibi", directory);
    tool_write(
        core_source,
        "package core_program\nimport strings \"core:strings\"\n"
        "Build :: () -> int { return strings.ByteLength(\"seven..\"); }\n"
    );
    const char *core[] = {
        host_path,
        "--compiler",
        CHIBI_SOURCE_ROOT "/tools/compiler",
        "--compiler-core",
        CHIBI_SOURCE_ROOT "/packages/core",
        "--root",
        root,
        "--source",
        core_source,
        "--core",
        CHIBI_SOURCE_ROOT "/packages/core",
        "--entry",
        "Build",
        "--timeout-ms",
        "10000",
        NULL,
    };
    REQUIRE(tool_run(core, root, log) == 7);
    const char *absent[] = {
        host_path,
        "--compiler",
        CHIBI_SOURCE_ROOT "/tools/compiler",
        "--compiler-core",
        CHIBI_SOURCE_ROOT "/packages/core",
        "--root",
        root,
        "--source",
        core_source,
        "--core",
        "no-core",
        "--entry",
        "Build",
        "--timeout-ms",
        "10000",
        NULL,
    };
    REQUIRE(tool_run(absent, root, log) != 0);
    text = tool_read(log);
    bool unfound = text && strstr(text, "build core collection could not be found");
    free(text);
    REQUIRE(unfound);
    return true;
}

static void sdk_path(char out[PATH_MAX], const char *base, const char *name)
{
    int length = snprintf(out, PATH_MAX, "%s/%s", base, name);
    assert(length > 0 && length < PATH_MAX);
}

/* Include names, permissions, timestamps and bytes, but not access times changed
 * by reading. The SDK is read-only during each invocation, including its host. */
static bool sdk_digest(const char *path, sha256 *hash)
{
    struct stat info;
    REQUIRE(!lstat(path, &info));
    REQUIRE(!(info.st_mode & 0222));
    REQUIRE(S_ISDIR(info.st_mode) || S_ISREG(info.st_mode));
    sha256_add(hash, &info.st_mode, sizeof info.st_mode);
#ifdef __APPLE__
    sha256_add(hash, &info.st_mtimespec, sizeof info.st_mtimespec);
#else
    sha256_add(hash, &info.st_mtim, sizeof info.st_mtim);
#endif
    if (S_ISREG(info.st_mode)) {
        sha256_add(hash, &info.st_size, sizeof info.st_size);
        FILE *file = fopen(path, "rb");
        REQUIRE(file);
        char bytes[8192];
        size_t count;
        while ((count = fread(bytes, 1, sizeof bytes, file))) {
            sha256_add(hash, bytes, count);
        }
        bool read = !ferror(file);
        bool closed = !fclose(file);
        REQUIRE(read && closed);
        return true;
    }
    struct dirent **entries = NULL;
    int count = scandir(path, &entries, NULL, alphasort);
    REQUIRE(count >= 0);
    bool valid = true;
    for (int i = 0; i < count; i++) {
        const char *name = entries[i]->d_name;
        if (strcmp(name, ".") && strcmp(name, "..")) {
            char child[PATH_MAX];
            sdk_path(child, path, name);
            sha256_add(hash, name, strlen(name) + 1);
            valid &= sdk_digest(child, hash);
        }
        free(entries[i]);
    }
    free(entries);
    sha256_add(hash, "", 1);
    return valid;
}

static bool sdk_hash(const char *path, char digest[65])
{
    sha256 hash;
    sha256_begin(&hash);
    bool valid = sdk_digest(path, &hash);
    sha256_end(&hash, digest);
    return valid;
}

static bool default_compiler(const char *host, bool corrupt)
{
    const char *argv[] = {
        host,
        "--root",
        root,
        "--source",
        "DefaultCompiler.chibi",
        "--entry",
        "Build",
        "--timeout-ms",
        "10000",
        NULL,
    };
    /* The child has no toolchain on PATH and no environment-supplied SDK. */
    const char *environment[] = {
        "PATH=/chibi/no-native-toolchain",
#ifdef __APPLE__
        "ASAN_OPTIONS=external_symbolizer_path=/usr/bin/atos",
#endif
        NULL,
    };
    chibi_process_capture_result result = capture(argv, environment, 15);
    REQUIRE(result.error == CHIBI_PROCESS_OK);
    REQUIRE(result.status == (corrupt ? 1 : 47));
    REQUIRE(!out.truncated && !err.truncated && !out.size);
    if (corrupt) {
        REQUIRE(strstr(errors, "compiler.chibip,"));
        REQUIRE(strstr(errors, "is not the closure bootstrap.json records"));
    } else {
        REQUIRE(!err.size);
    }
    return true;
}

TEST_CASE(default_compiler_follows_relocated_read_only_sdk_and_refuses_corrupt_seed)
{
    char original[PATH_MAX], relocated[PATH_MAX], path[PATH_MAX];
    char compiler[PATH_MAX], core[PATH_MAX], shared[PATH_MAX], log[PATH_MAX];
    char *host = realpath(host_path, NULL);
    REQUIRE(host);
    char *slash = strrchr(host, '/');
    REQUIRE(slash);
    *slash = 0;
    sdk_path(compiler, host, "../share/chibi/compiler");
    sdk_path(core, host, "../share/chibi/core");
    free(host);
    sdk_path(log, root, "sdk-copy.log");
    sdk_path(original, root, "sdk-original");
    sdk_path(relocated, root, "relocated sdk");
    REQUIRE(!mkdir(original, 0700));
    static const char *directories[] = { "bin", "libexec", "libexec/chibi", "share", "share/chibi" };
    for (size_t i = 0; i < sizeof directories / sizeof *directories; i++) {
        sdk_path(path, original, directories[i]);
        REQUIRE(!mkdir(path, 0700));
    }
    sdk_path(shared, original, "share/chibi");
    const char *copy_inputs[] = { "cp", "-R", compiler, core, shared, NULL };
    REQUIRE(!tool_run(copy_inputs, root, log));
    static const char *locations[] = { "bin/chibi-build-host",
                                       "libexec/chibi/chibi-build-host" };
    for (size_t i = 0; i < sizeof locations / sizeof *locations; i++) {
        sdk_path(path, original, locations[i]);
        const char *copy_host[] = { "cp", host_path, path, NULL };
        REQUIRE(!tool_run(copy_host, root, log));
    }
    REQUIRE(!rename(original, relocated));
    REQUIRE(access(original, F_OK) == -1 && errno == ENOENT);
    sdk_path(path, root, "DefaultCompiler.chibi");
    tool_write(path, "package sdk_compiler\nBuild :: () -> int { return 47; }\n");

    const char *permissions[] = { "chmod", "-R", "a-w", relocated, NULL };
    bool valid = !tool_run(permissions, root, log);
    char before[65], after[65];
    valid &= sdk_hash(relocated, before);
    for (size_t i = 0; i < sizeof locations / sizeof *locations; i++) {
        sdk_path(path, relocated, locations[i]);
        valid &= default_compiler(path, false);
    }
    valid &= sdk_hash(relocated, after) && !strcmp(before, after);
    permissions[2] = "u+w";
    REQUIRE(!tool_run(permissions, root, log));
    REQUIRE(valid);

    /* Keep the manifest intact. A changed closure must be refused before any
     * compiler execution or fallback, even though valid sources remain beside it. */
    sdk_path(path, relocated, "share/chibi/compiler/bootstrap/compiler.chibip");
    FILE *seed = fopen(path, "r+b");
    REQUIRE(seed);
    int byte = fgetc(seed);
    REQUIRE(byte != EOF && !fseek(seed, 0, SEEK_SET));
    REQUIRE(fputc(byte ^ 1, seed) != EOF);
    REQUIRE(!fclose(seed));
    permissions[2] = "a-w";
    valid = !tool_run(permissions, root, log);
    valid &= sdk_hash(relocated, before);
    for (size_t i = 0; i < sizeof locations / sizeof *locations; i++) {
        sdk_path(path, relocated, locations[i]);
        valid &= default_compiler(path, true);
    }
    valid &= sdk_hash(relocated, after) && !strcmp(before, after);
    permissions[2] = "u+w";
    REQUIRE(!tool_run(permissions, root, log));
    REQUIRE(valid);
    return true;
}

typedef struct
{
    char inputs[PATH_MAX], compiler[PATH_MAX], core[PATH_MAX];
    char source[PATH_MAX], marker[PATH_MAX], digest[65];
} startup_fixture;

static bool startup_inputs(startup_fixture *fixture, const char *name)
{
    sdk_path(fixture->inputs, root, name);
    REQUIRE(!mkdir(fixture->inputs, 0700));
    char log[PATH_MAX], changed[PATH_MAX];
    sdk_path(log, root, "startup-inputs.log");
    const char *copy[] = {
        "cp",
#ifdef __APPLE__
        "-cR",
#else
        "--reflink=auto",
        "-R",
#endif
        CHIBI_SOURCE_ROOT "/tools/compiler",
        CHIBI_SOURCE_ROOT "/packages/core",
        fixture->inputs,
        NULL,
    };
    REQUIRE(!tool_run(copy, root, log));
    sdk_path(fixture->compiler, fixture->inputs, "compiler");
    sdk_path(fixture->core, fixture->inputs, "core");
    sdk_path(changed, fixture->compiler, "package_compiler/Compile.chibi");
    FILE *file = fopen(changed, "ab");
    REQUIRE(file);
    bool written = fputs("\n// Exercise changed-source recovery before Build.\n", file) >=
                   0;
    written = !fclose(file) && written;
    REQUIRE(written);
    /* This changes a recorded source's bytes, keeping the manifest and seed
     * intact so startup must recover rather than reject a damaged artifact. */
    sdk_path(fixture->source, fixture->inputs, "Build.chibi");
    sdk_path(fixture->marker, root, "startup-authored-ran");
    tool_write(
        fixture->source,
        "package startup\nBuild :: () -> int { "
        "build.write_file(\"startup-authored-ran\", \"unexpected\"); "
        "return 0; }\n"
    );
    const char *permissions[] = { "chmod", "-R", "a-w", fixture->inputs, NULL };
    bool frozen = !tool_run(permissions, root, log) &&
                  sdk_hash(fixture->inputs, fixture->digest);
    if (!frozen) {
        permissions[2] = "u+w";
        assert(!tool_run(permissions, root, log));
    }
    REQUIRE(frozen);
    return true;
}

static bool startup_interrupted(startup_fixture *fixture, int signal, bool blocked)
{
    const char *argv[] = {
        worker_path,
        "build-blocked-signals",
        host_path,
        "--compiler",
        fixture->compiler,
        "--compiler-core",
        fixture->core,
        "--root",
        root,
        "--source",
        fixture->source,
        "--entry",
        "Build",
        "--timeout-ms",
        signal ? "10000" : "50",
        NULL,
    };
    const char *const *launch = blocked ? argv : argv + 2;
    const char *environment[] = {
        "PATH=/chibi/no-native-toolchain",
#ifdef __APPLE__
        "ASAN_OPTIONS=external_symbolizer_path=/usr/bin/atos",
#endif
        NULL,
    };
    chibi_process_capture_result result;
    bool sent = !signal;
    if (!signal) {
        result = capture(launch, environment, 2);
    } else {
        out = (chibi_process_output){ .data = output, .capacity = sizeof output };
        err = (chibi_process_output){ .data = errors, .capacity = sizeof errors };
        chibi_process_job job = { 0 };
        double started = now(), deadline = started + 2;
        result = chibi_process_begin(&job, launch[0], launch, root, environment, deadline, NULL, &out, &err);
        if (result.error == CHIBI_PROCESS_OK) {
            /* Real compiler recovery outlasts this startup window. Signal only
             * the CLI, leaving its cancellation and cleanup responsibilities
             * intact; the outer deadline contains a broken implementation. */
            while (chibi_process_running(&job) && now() < started + .1) {
                chibi_process_service(&job, 1, deadline, NULL);
            }
            sent = chibi_process_running(&job) && !kill(job.pid, signal);
            while (chibi_process_running(&job)) {
                chibi_process_service(&job, 1, deadline, NULL);
            }
            result = chibi_process_end(&job);
        }
    }
    char after[65], log[PATH_MAX];
    bool unchanged = sdk_hash(fixture->inputs, after) &&
                     !strcmp(fixture->digest, after);
    sdk_path(log, root, "startup-inputs.log");
    const char *permissions[] = { "chmod", "-R", "u+w", fixture->inputs, NULL };
    REQUIRE(!tool_run(permissions, root, log));
    REQUIRE(!unlink(fixture->source));
    REQUIRE(sent);
    REQUIRE(result.error == CHIBI_PROCESS_OK && result.status == 1);
    REQUIRE(!out.size && !out.truncated && !err.truncated);
    REQUIRE(strstr(errors, signal ? "build process cancelled" : "build process timeout"));
    REQUIRE(access(fixture->marker, F_OK) == -1 && errno == ENOENT);
    REQUIRE(unchanged);
    return true;
}

TEST_CASE(private_host_cli_deadline_covers_changed_compiler_recovery)
{
    startup_fixture fixture;
    REQUIRE(startup_inputs(&fixture, "startup-timeout"));
    return startup_interrupted(&fixture, 0, false);
}

TEST_CASE(private_host_cli_sigint_covers_changed_compiler_recovery)
{
    startup_fixture fixture;
    REQUIRE(startup_inputs(&fixture, "startup-sigint"));
    return startup_interrupted(&fixture, SIGINT, false);
}

TEST_CASE(private_host_cli_sigterm_covers_changed_compiler_recovery)
{
    startup_fixture fixture;
    REQUIRE(startup_inputs(&fixture, "startup-sigterm"));
    return startup_interrupted(&fixture, SIGTERM, false);
}

TEST_CASE(private_host_cli_deadline_unblocks_inherited_signal_masks)
{
    startup_fixture fixture;
    REQUIRE(startup_inputs(&fixture, "startup-blocked"));
    return startup_interrupted(&fixture, 0, true);
}

TEST_CASE(private_host_cli_runtime_signals_still_clean_up_descendants)
{
    char directory[PATH_MAX], source[PATH_MAX], marker[PATH_MAX];
    char worker_environment[PATH_MAX + 32];
    snprintf(worker_environment, sizeof worker_environment, "CHIBI_BUILD_WORKER=%s", worker_path);
    const char *environment[] = {
        "PATH=/chibi/no-native-toolchain",
        worker_environment,
#ifdef __APPLE__
        "ASAN_OPTIONS=external_symbolizer_path=/usr/bin/atos",
#endif
        NULL,
    };
    const int signals[] = { SIGINT, SIGTERM };
    for (int i = 0; i < 2; i++) {
        sdk_path(directory, root, i ? "runtime-sigterm" : "runtime-sigint");
        REQUIRE(!mkdir(directory, 0700));
        sdk_path(source, directory, "Build.chibi");
        sdk_path(marker, directory, "started");
        tool_write(
            source,
            "package runtime\nBuild :: () -> int { "
            "build.run([build.environment(\"CHIBI_BUILD_WORKER\"), "
            "\"descendant\", build.root(), \"0\", \"0\"]); return 0; }\n"
        );
        const char *argv[] = {
            host_path,
            "--compiler",
            CHIBI_SOURCE_ROOT "/tools/compiler",
            "--compiler-core",
            CHIBI_SOURCE_ROOT "/packages/core",
            "--root",
            directory,
            "--source",
            source,
            "--entry",
            "Build",
            "--timeout-ms",
            "10000",
            NULL,
        };
        out = (chibi_process_output){ .data = output, .capacity = sizeof output };
        err = (chibi_process_output){ .data = errors, .capacity = sizeof errors };
        chibi_process_job job = { 0 };
        double deadline = deadline_after(10);
        chibi_process_capture_result result = chibi_process_begin(
            &job,
            argv[0],
            argv,
            root,
            environment,
            deadline,
            NULL,
            &out,
            &err
        );
        bool sent = false;
        if (result.error == CHIBI_PROCESS_OK) {
            /* The worker's marker proves authored execution has started and
             * owns a real descendant before the signal targets only the CLI. */
            while (chibi_process_running(&job) && access(marker, F_OK)) {
                chibi_process_service(&job, 1, deadline, NULL);
            }
            sent = chibi_process_running(&job) && !access(marker, F_OK) &&
                   !kill(job.pid, signals[i]);
            while (chibi_process_running(&job)) {
                chibi_process_service(&job, 1, deadline, NULL);
            }
            result = chibi_process_end(&job);
        }
        bool diagnostic = !out.size && !out.truncated && !err.truncated &&
                          strstr(errors, "build process cancelled");
        const char *probe[] = { worker_path, "cleanup-probe", directory, NULL };
        chibi_process_capture_result cleanup = capture(probe, environment, .5);
        bool gone = cleanup.error == CHIBI_PROCESS_OK && !cleanup.status;
        if (!gone) {
            /* Contain a failed cleanup assertion before removing its fixture. */
            FILE *file = fopen(marker, "r");
            int pid = 0;
            if (file) {
                bool recorded = fscanf(file, "%d", &pid) == 1 && pid > 0;
                fclose(file);
                pid_t group = recorded ? getpgid(pid) : -1;
                if (group > 0 && group != getpgrp()) {
                    kill(-group, SIGKILL);
                }
            }
        }
        REQUIRE(sent);
        REQUIRE(result.error == CHIBI_PROCESS_OK && result.status == 1);
        REQUIRE(diagnostic);
        REQUIRE(gone);
    }
    return true;
}

static chibi_build_options primitive_options(const char *text, behavior_backend backend)
{
    /* Functional invocations compile through the self-hosted compiler before
     * execution. Allow for sanitizer preparation and execution; deadline and
     * cancellation witnesses set their own limits. */
    return (chibi_build_options){
        .compiler = build_compiler,
        .root = root,
        .source_path = "BuildPrimitives.chibi",
        .source_text = text,
        .entry = "Build",
        .backend = backend,
        .deadline = deadline_after(120),
    };
}

/* Measure authored operations independently of compiler preparation. */
static double primitive_started;
static void primitive_prepared()
{
    primitive_started = now();
}

static bool primitive_backend(const chibi_build_result *result, behavior_backend backend)
{
    return !result->counts.overflow && !result->counts.inspection_bytecode_fallbacks &&
           !result->counts.wasm_entries &&
           result->counts.bytecode_entries == (backend == BEHAVIOR_BYTECODE ? 1u : 0u) &&
           result->counts.sljit_entries == (backend == BEHAVIOR_SLJIT ? 1u : 0u);
}

/* A step takes up to 1,023 arguments, as many as the process layer passes and
 * twice a link of every object and library the tooling tests use, and refuses
 * more. */
TEST_CASE(steps_take_up_to_1023_arguments)
{
    static const char *const sources[] = {
        "package primitives\nBuild :: () -> int { "
        "command:=[build.environment(\"CHIBI_BUILD_WORKER\"),\"exit\",\"7\"]; "
        "while (build.length(command) < 1023) { command.append(\"-\"); } "
        "return build.run(command); }",
        "package primitives\nBuild :: () -> int { "
        "command:=[build.environment(\"CHIBI_BUILD_WORKER\"),\"exit\",\"7\"]; "
        "while (build.length(command) < 1024) { command.append(\"-\"); } "
        "return build.run(command); }",
    };
    for (int b = 0; b < 2; b++) {
        behavior_backend backend = b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        chibi_build_options options = primitive_options(sources[0], backend);
        options.timings = tmpfile();
        REQUIRE(options.timings);
        chibi_build_result result;
        bool success = chibi_build_invoke(&options, &result);
        if (!success) {
            fprintf(stderr, "step arguments: %s\n", result.diagnostic);
        }
        REQUIRE(
            success && result.value == 7 && result.steps == 1 &&
            primitive_backend(&result, backend)
        );
        REQUIRE(timing_rows(options.timings, "process", 7, CHIBI_PROCESS_OK) == 1);
        REQUIRE(timing_rows(options.timings, "execute", 7, 0) == 1);
        REQUIRE(!fclose(options.timings));
        options = primitive_options(sources[1], backend);
        REQUIRE(!chibi_build_invoke(&options, &result));
        REQUIRE(
            !strcmp(result.diagnostic, "invalid build process arguments") &&
            result.steps == 0
        );
    }
    return true;
}

TEST_CASE(private_values_and_process_snapshots_survive_later_calls)
{
    static const char *source =
        "package primitives\nBuild :: () -> int { "
        "args:=build.arguments(); retained:=args[1]; "
        "worker:=build.environment(\"CHIBI_BUILD_WORKER\"); "
        "status:=build.run([worker,\"build-argv\",args[0],retained]); "
        "first:=build.last_stdout(); firstError:=build.last_stderr(); "
        "if(status!=23) { return 1; } "
        "if(build.run([worker,\"exit\",\"0\"])!=0) { return 2; } "
        "later:=build.last_stdout(); "
        "joined:=build.join(build.arguments(),\"|\"); "
        "if(later!=\"\" || build.last_stderr()!=\"\") { return 3; } "
        "if(firstError!=\"worker-stderr\\n\") { return 4; } "
        "expected:=build.join([\"arg=23:\",retained,\"\\n\"],\"\"); "
        "if(build.after(first,\"arg=0:\\n\")!=expected) { return 5; } "
        "if(joined!=build.join([\"\",retained],\"|\") || args[1]!=retained) { "
        "return 6; } "
        "if(build.root()!=build.resolve_path(\".\") || "
        "build.program_directory()!=build.root()) { return 7; } "
        "if(build.find_executable(worker)==\"\" || "
        "build.find_executable(\"/chibi/missing/tool\")!=\"\") { return 8; } "
        "platform:=build.platform(); if(platform!=\"Linux\" && "
        "platform!=\"Darwin\") { return 9; } "
        "return 0; }";
    const char *arguments[] = { "", "space \"quote\";$literal*" };
    for (int b = 0; b < 2; b++) {
        behavior_backend backend = b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        chibi_build_options options = primitive_options(source, backend);
        options.arguments = arguments;
        options.argument_count = 2;
        chibi_build_result result;
        bool success = chibi_build_invoke(&options, &result);
        if (!success) {
            fprintf(stderr, "private value lifetime: %s\n", result.diagnostic);
        }
        REQUIRE(success && result.value == 0 && result.steps == 2);
        REQUIRE(primitive_backend(&result, backend));
    }
    return true;
}

TEST_CASE(private_string_utilities_preserve_delimiters_and_empty_values)
{
    static const char
        *source = "package text\nBuild :: () -> int { "
                  "lines:=build.lines(\" a\\r\\n\\nb\\n\"); "
                  "if(build.length(lines)!=3 || build.trim(lines[0])!=\"a\" || "
                  "lines[1]!=\"\" || lines[2]!=\"b\") { return 1; } "
                  "if(build.before(\"key=a=b\",\"=\")!=\"key\" || "
                  "build.after(\"key=a=b\",\"=\")!=\"a=b\") { return 2; } "
                  "if(build.before(\"text\",\":\")!=\"text\" || "
                  "build.after(\"text\",\":\")!=\"\") { return 3; } "
                  "empty:string[]=[]; "
                  "merged:=build.concat(empty,build.concat([\"\",\"one\"],["
                  "\"two\"])); "
                  "if(build.length(merged)!=3 || "
                  "build.join(merged,\"|\")!=\"|one|two\") { return 4; } "
                  "if(build.length(build.lines(\"\"))!=0 || "
                  "build.join(empty,\";\")!=\"\") { return 5; } "
                  "if(!build.starts_with(\"abc\",\"ab\") || "
                  "build.starts_with(\"a\",\"ab\")) { return 6; } "
                  "if(build.report(\"authored diagnostic\")!=0) { return 7; } "
                  "if(build.replace(\"cp_a "
                  "cp_cp_\",\"cp_\",\"x_cp_\")!=\"x_cp_a x_cp_x_cp_\" || "
                  "build.replace(\"aaa\",\"aa\",\"\")!=\"a\" || "
                  "build.replace(\"none\",\"cp_\",\"x\")!=\"none\") { return "
                  "8; } "
                  "return 0; }";
    for (int b = 0; b < 2; b++) {
        behavior_backend backend = b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        chibi_build_options options = primitive_options(source, backend);
        chibi_build_result result;
        bool invoked = chibi_build_invoke(&options, &result);
        if (!invoked) {
            fprintf(stderr, "build string utilities: backend=%d %s\n", b, result.diagnostic);
        }
        REQUIRE(invoked);
        REQUIRE(result.value == 0 && !result.steps && primitive_backend(&result, backend));
        REQUIRE(!strcmp(result.stderr_text, "authored diagnostic\n"));
    }
    return true;
}

/* The command-line host passes stdout and stderr, so a hanging step still
 * shows the reports and step output before it. */
TEST_CASE(private_sinks_receive_reports_and_step_output_as_they_happen)
{
    REQUIRE(!setenv("CHIBI_BUILD_WORKER", worker_path, 1));
    static const char *source = "package sinks\nBuild :: () -> int { "
                                "build.report(\"streamed\"); "
                                "return "
                                "build.run([build.environment(\"CHIBI_BUILD_"
                                "WORKER\"),\"cat\",\"sink-stderr\"]); }";
    char out_path[4096], err_path[4096];
    snprintf(out_path, sizeof out_path, "%s/sink-stdout", root);
    snprintf(err_path, sizeof err_path, "%s/sink-stderr", root);
    FILE *out = fopen(out_path, "w+"), *err = fopen(err_path, "w+");
    REQUIRE(out && err);
    chibi_build_options options = primitive_options(source, BEHAVIOR_BYTECODE);
    options.stdout_sink = out;
    options.stderr_sink = err;
    chibi_build_result result;
    bool invoked = chibi_build_invoke(&options, &result);
    if (!invoked) {
        fprintf(stderr, "build sinks: %s\n", result.diagnostic);
    }
    REQUIRE(invoked && result.value == 0 && result.steps == 1);
    REQUIRE(!result.stdout_size && !result.stderr_size);
    char text[64];
    rewind(out);
    size_t size = fread(text, 1, sizeof text - 1, out);
    text[size] = 0;
    REQUIRE(!strcmp(text, "streamed\n"));
    rewind(err);
    size = fread(text, 1, sizeof text - 1, err);
    text[size] = 0;
    REQUIRE(!strcmp(text, "streamed\n"));
    REQUIRE(!fclose(out) && !fclose(err) && !unlink(out_path) && !unlink(err_path));
    options.stderr_sink = NULL;
    REQUIRE(!chibi_build_invoke(&options, &result));
    REQUIRE(strstr(result.diagnostic, "invalid build host options"));
    return true;
}

TEST_CASE(private_natives_resolve_only_under_the_build_namespace)
{
    static const char *sources[] = {
        "package names\nBuild :: () -> int { "
        "if(build.join([\"a\",\"b\"],\"\")!=\"ab\") { return 1; } "
        "return 0; }",
        "package names\nBuild :: () -> int { "
        "BuildJoin([\"a\"],\"\"); return 0; }",
        "package names\nBuild :: () -> int { "
        "join([\"a\"],\"\"); return 0; }",
    };
    static const char *diagnostics[] = {
        NULL,
        "Unknown function 'BuildJoin'",
        "Unknown function 'join'",
    };
    for (int i = 0; i < 3; i++) {
        chibi_build_options options = primitive_options(sources[i], BEHAVIOR_BYTECODE);
        chibi_build_result result;
        bool ran = chibi_build_invoke(&options, &result);
        if (ran != !diagnostics[i] ||
            (diagnostics[i] && !diagnostic_is(result.diagnostic, diagnostics[i]))) {
            fprintf(
                stderr,
                "namespace case %d: ran=%d steps=%d bytecode=%llu sljit=%llu "
                "expected=[%s] actual=[%s]\n",
                i,
                ran,
                result.steps,
                (unsigned long long)result.counts.bytecode_entries,
                (unsigned long long)result.counts.sljit_entries,
                diagnostics[i] ? diagnostics[i] : "successful invocation",
                result.diagnostic
            );
        }
        REQUIRE(ran == !diagnostics[i]);
        REQUIRE(
            diagnostics[i] ? diagnostic_is(result.diagnostic, diagnostics[i])
                           : result.value == 0
        );
        if (diagnostics[i]) {
            REQUIRE(
                !result.steps && !result.counts.bytecode_entries &&
                !result.counts.sljit_entries
            );
        }
    }
    return true;
}

TEST_CASE(private_programs_import_core_packages_and_format_text)
{
    static const char
        *source = "package core_text\n"
                  "import strings \"core:strings\"\n"
                  "Build :: () -> int { "
                  "parts := strings.Split(\"a,b,c\", \",\"); "
                  "build.report(format_text(\"{0} parts, last {1}\", "
                  "length(parts), parts[2])); "
                  "return strings.Index(\"build\", \"il\"); }";
    for (int b = 0; b < 2; b++) {
        behavior_backend backend = b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        chibi_build_options options = primitive_options(source, backend);
        options.core = CHIBI_SOURCE_ROOT "/packages/core";
        chibi_build_result result;
        double started = chibi_build_now();
        bool invoked = chibi_build_invoke(&options, &result);
        if (!invoked) {
            fprintf(
                stderr,
                "core package build: backend=%d elapsed=%.3f %s\n",
                b,
                chibi_build_now() - started,
                result.diagnostic
            );
        }
        REQUIRE(invoked);
        /* Each call into core:strings enters the backend again. */
        uint64_t entries = b ? result.counts.sljit_entries
                             : result.counts.bytecode_entries;
        uint64_t other = b ? result.counts.bytecode_entries : result.counts.sljit_entries;
        REQUIRE(result.value == 2 && entries > 1 && !other && !result.counts.wasm_entries);
        REQUIRE(!strcmp(result.stderr_text, "3 parts, last c\n"));
    }
    /* Without a core collection, the import fails before the program runs. */
    chibi_build_options options = primitive_options(source, BEHAVIOR_BYTECODE);
    chibi_build_result result;
    REQUIRE(!chibi_build_invoke(&options, &result));
    REQUIRE(
        diagnostic_is(result.diagnostic, "Unknown import collection") &&
        !result.counts.bytecode_entries
    );
    return true;
}

static int kept_programs(const char *cache)
{
    binary_cache_entry *entries = NULL;
    int count = 0, kept = 0;
    if (!binary_cache_list(cache, &entries, &count)) {
        return -1;
    }
    for (int i = 0; i < count; i++) {
        kept += !strncmp(entries[i].key, "program-", 8);
    }
    free(entries);
    return kept;
}

/* A build host keeps the program it compiles beside the recovered compilers and
 * reuses it: the second start of the same program adds no entry. The program
 * is new to this process, so an entry a restored cache holds cannot count. */
TEST_CASE(private_compiled_programs_are_kept_and_reused)
{
    const char *cache = getenv("CHIBI_COMPILER_CACHE");
    if (!cache || !*cache || !strcmp(cache, "off")) {
        return true;
    }
    char source[160];
    snprintf(
        source,
        sizeof source,
        "package kept_program\nBuild :: () -> int { return %ld - %ld + 42; }",
        (long)getpid(),
        (long)getpid()
    );
    int before = kept_programs(cache);
    REQUIRE(before >= 0);
    for (int i = 0; i < 2; i++) {
        chibi_build_options options = primitive_options(source, BEHAVIOR_BYTECODE);
        chibi_build_result result;
        REQUIRE(chibi_build_invoke(&options, &result));
        REQUIRE(result.value == 42);
        REQUIRE(kept_programs(cache) == before + 1);
    }
    return true;
}

/* A build entry takes the address of a local and writes through it on each
 * backend: the host gives the entry a managed heap for the cell. */
TEST_CASE(private_programs_allocate_managed_pointers)
{
    static const char
        *source = "package pointers\n"
                  "Build :: () -> int { x := 2; p := &x; p^ += 1; "
                  "return x; }";
    for (int b = 0; b < 2; b++) {
        chibi_build_options options = primitive_options(source, b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE);
        chibi_build_result result;
        bool invoked = chibi_build_invoke(&options, &result);
        if (!invoked) {
            fprintf(stderr, "managed pointer build: backend=%d %s\n", b, result.diagnostic);
        }
        REQUIRE(invoked && result.value == 3);
    }
    return true;
}

/* A program imports a named collection's packages as NAME:path. */
TEST_CASE(private_programs_import_named_collections)
{
    char collection[256], directory[300], file[320];
    snprintf(collection, sizeof collection, "%s/collection", root);
    snprintf(directory, sizeof directory, "%s/greet", collection);
    assert(!mkdir(collection, 0700) && !mkdir(directory, 0700));
    snprintf(file, sizeof file, "%s/Greet.chibi", directory);
    tool_write(file, "package greet\npub Answer :: () -> int { return 42; }\n");
    static const char *source = "package uses\nimport greet \"lib:greet\"\n"
                                "Build :: () -> int { return greet.Answer(); }";
    const chibi_build_collection named[] = { { "lib", collection } };
    for (int b = 0; b < 2; b++) {
        behavior_backend backend = b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        chibi_build_options options = primitive_options(source, backend);
        options.collections = named;
        options.collection_count = 1;
        chibi_build_result result;
        REQUIRE(chibi_build_invoke(&options, &result));
        REQUIRE(result.value == 42);
    }
    char program[320], log[256];
    snprintf(program, sizeof program, "%s/UsesCollection.chibi", root);
    snprintf(log, sizeof log, "%s/collection.log", root);
    tool_write(program, source);
    char option[300];
    snprintf(option, sizeof option, "lib=%s", collection);
    const char *argv[] = { host_path,
                           "--compiler",
                           CHIBI_SOURCE_ROOT "/tools/compiler",
                           "--compiler-core",
                           CHIBI_SOURCE_ROOT "/packages/core",
                           "--root",
                           root,
                           "--source",
                           program,
                           "--collection",
                           option,
                           "--entry",
                           "Build",
                           "--timeout-ms",
                           "10000",
                           NULL };
    REQUIRE(tool_run(argv, root, log) == 42);
    const char *malformed[] = { host_path,
                                "--compiler",
                                CHIBI_SOURCE_ROOT "/tools/compiler",
                                "--compiler-core",
                                CHIBI_SOURCE_ROOT "/packages/core",
                                "--root",
                                root,
                                "--source",
                                program,
                                "--collection",
                                "lib",
                                "--entry",
                                "Build",
                                "--timeout-ms",
                                "1000",
                                NULL };
    REQUIRE(tool_run(malformed, root, log) == 1);
    char *text = tool_read(log);
    bool usage = text && strstr(text, "usage:");
    free(text);
    REQUIRE(usage);
    return true;
}

/* build.embed_file writes the C that cmake/embed_binary.cmake writes, and
 * leaves an unchanged output alone. */
TEST_CASE(private_embed_file_writes_cmake_byte_arrays)
{
    char input[256], empty[256];
    snprintf(input, sizeof input, "%s/bytes.bin", root);
    snprintf(empty, sizeof empty, "%s/empty.bin", root);
    unsigned char bytes[18];
    for (int i = 0; i < 18; i++) {
        bytes[i] = (unsigned char)(i * 15);
    }
    FILE *file = fopen(input, "wb");
    REQUIRE(file && fwrite(bytes, 1, sizeof bytes, file) == sizeof bytes && !fclose(file));
    tool_write(empty, "");
    static const char
        *source = "package embed\nBuild :: () -> int { "
                  "if (!build.embed_file(\"bytes.bin\", \"out/bytes.c\", "
                  "\"bytes_blob\")) { return 1; } "
                  "stamp := build.file_stamp(\"out/bytes.c\"); "
                  "if (build.embed_file(\"bytes.bin\", \"out/bytes.c\", "
                  "\"bytes_blob\")) { return 2; } "
                  "if (build.file_stamp(\"out/bytes.c\") != stamp) { return 3; "
                  "} "
                  "if (!build.embed_file(\"empty.bin\", \"out/empty.c\", "
                  "\"_empty\")) { return 4; } "
                  "return 0; }";
    char directory[256];
    snprintf(directory, sizeof directory, "%s/out", root);
    REQUIRE(!mkdir(directory, 0700) || errno == EEXIST);
    chibi_build_options options = primitive_options(source, BEHAVIOR_BYTECODE);
    chibi_build_result result;
    REQUIRE(chibi_build_invoke(&options, &result));
    REQUIRE(result.value == 0);
    char path[320];
    snprintf(path, sizeof path, "%s/out/bytes.c", root);
    char *text = tool_read(path);
    REQUIRE(
        text &&
        !strcmp(
            text,
            "#include <stddef.h>\n\nconst unsigned char bytes_blob[] = {\n"
            "    0x00, 0x0f, 0x1e, 0x2d, 0x3c, 0x4b, 0x5a, 0x69, 0x78, 0x87, "
            "0x96, 0xa5, "
            "0xb4, 0xc3, 0xd2, 0xe1,\n    0xf0, 0xff,\n};\n"
            "const size_t bytes_blob_size = sizeof bytes_blob;\n"
        )
    );
    free(text);
    snprintf(path, sizeof path, "%s/out/empty.c", root);
    text = tool_read(path);
    REQUIRE(
        text && !strcmp(
                    text,
                    "#include <stddef.h>\n\nconst unsigned char _empty[] = {\n "
                    "   \n};\n"
                    "const size_t _empty_size = sizeof _empty;\n"
                )
    );
    free(text);
    static const char *refused[] = {
        "package embed\nBuild :: () -> int { build.embed_file(\"bytes.bin\", "
        "\"out/x.c\", \"1st\"); return 0; }",
        "package embed\nBuild :: () -> int { build.embed_file(\"out\", "
        "\"out/x.c\", \"x\"); return 0; }",
    };
    static const char *diagnostics[] = {
        "build embed symbol must be a C identifier",
        "build embed input must be a regular file of at most 64 MiB",
    };
    for (int i = 0; i < 2; i++) {
        options = primitive_options(refused[i], BEHAVIOR_BYTECODE);
        REQUIRE(!chibi_build_invoke(&options, &result));
        REQUIRE(!strcmp(result.diagnostic, diagnostics[i]));
    }
    return true;
}

/* A source that does not parse is reported where it fails, as
 * "path:line:column: message", whichever host compiled it. */
TEST_CASE(private_programs_report_syntax_errors_with_a_location)
{
    static const struct
    {
        const char *source, *position, *message;
    } cases[] = {
        { "package broken\nBuild :: () -> int { return 1 + ; }", ":2:", "Expected an expression, found `;`" },
        { "package broken\nBuild :: () -> int { return 1;", ":2:", "to close the `{` at 2:" },
        { "package broken\nimport project:greet\n", ":2:", "write `import greet \"project:greet\"`" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        chibi_build_options options = primitive_options(cases[i].source, BEHAVIOR_BYTECODE);
        chibi_build_result result;
        REQUIRE(!chibi_build_invoke(&options, &result));
        REQUIRE(
            strstr(result.diagnostic, ".chibi") &&
            strstr(result.diagnostic, cases[i].position)
        );
        REQUIRE(strstr(result.diagnostic, cases[i].message));
        REQUIRE(!result.counts.bytecode_entries && !result.counts.sljit_entries);
    }
    return true;
}

/* Yield and task syntax compile; private execution guards reject suspension
 * and task creation before any process step starts. */
TEST_CASE(private_programs_reject_yield_and_unawaited_tasks)
{
    static const char *sources[] = {
        "package waits\nBuild :: () -> int { yield; return 0; }",
        "package waits\nWait :: () { yield; }\nBuild :: () -> int { Wait(); "
        "return 0; }",
    };
    static const char *diagnostics[] = {
        "a build program cannot wait",
        "a build program cannot start a task",
    };
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 2; i++) {
            behavior_backend backend = b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
            chibi_build_options options = primitive_options(sources[i], backend);
            chibi_build_result result;
            bool ran = chibi_build_invoke(&options, &result);
            if (ran || strcmp(result.diagnostic, diagnostics[i])) {
                fprintf(
                    stderr,
                    "yield case %d backend %d: ran=%d steps=%d bytecode=%llu "
                    "sljit=%llu expected=[%s] actual=[%s]\n",
                    i,
                    b,
                    ran,
                    result.steps,
                    (unsigned long long)result.counts.bytecode_entries,
                    (unsigned long long)result.counts.sljit_entries,
                    diagnostics[i],
                    result.diagnostic
                );
            }
            REQUIRE(!ran);
            REQUIRE(!strcmp(result.diagnostic, diagnostics[i]));
            REQUIRE(!result.steps && primitive_backend(&result, options.backend));
        }
    }
    return true;
}

/* A program that fails without a message of its own says why it stopped. */
TEST_CASE(private_runtime_failures_name_their_cause)
{
    static const char *sources[] = {
        "package fails\nBuild :: () -> int { n := 0; while (true) { n += 1; } "
        "return n; }",
        "package fails\nBuild :: () -> int { xs: int[] = []; return xs[1]; }",
        /* A called function's failure reaches the program's diagnostic. */
        "package fails\nInner :: () -> int { xs: int[] = []; return xs[1]; }\n"
        "Build :: () -> int { return Inner(); }",
        "package fails\nInner :: () -> int { return build.wait(99); }\n"
        "Build :: () -> int { return Inner(); }",
    };
    static const char *diagnostics[] = {
        "instruction budget exhausted",
        "array index out of range",
        "array index out of range",
        "unknown build job",
    };
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 4; i++) {
            behavior_backend backend = b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
            chibi_build_options options = primitive_options(sources[i], backend);
            /* The default budget takes seconds to run out. */
            options.instruction_budget = 100000;
            chibi_build_result result;
            REQUIRE(!chibi_build_invoke(&options, &result));
            if (strcmp(result.diagnostic, diagnostics[i])) {
            }
            REQUIRE(!strcmp(result.diagnostic, diagnostics[i]));
        }
    }
    return true;
}

/* Started steps run at the same time; each wait gives its own step's status. */
TEST_CASE(private_started_steps_run_together)
{
    static const char
        *source = "package jobs\n"
                  "Build :: () -> int { worker := "
                  "build.environment(\"CHIBI_BUILD_WORKER\"); "
                  "handles: int[] = []; "
                  "for (i in 0..<4) { handles = append(handles, "
                  "build.start([worker, \"sleep\", \"0.4\"])); } "
                  "fast := build.start([worker, \"sleep\", \"0.05\"]); "
                  "failed := 0; "
                  "for (handle in handles) { if (build.wait(handle) != 0) { "
                  "failed += 1; } } "
                  "return failed + build.wait(fast); }";
    chibi_build_options options = primitive_options(source, BEHAVIOR_BYTECODE);
    primitive_started = 0;
    options.prepared = primitive_prepared;
    options.timings = tmpfile();
    REQUIRE(options.timings);
    double started = now();
    chibi_build_result result;
    bool invoked = chibi_build_invoke(&options, &result);
    if (!invoked) {
        fprintf(
            stderr,
            "concurrent build steps: elapsed=%.3f %s\n",
            now() - started,
            result.diagnostic
        );
    }
    REQUIRE(invoked);
    double elapsed = now() - primitive_started;
    /* One after another the four would take 1.6 s. */
    REQUIRE(result.value == 0 && result.steps == 5 && elapsed < 1.2);
    REQUIRE(timing_rows(options.timings, "prepare", 0, 0) == 1);
    REQUIRE(timing_rows(options.timings, "process", 0, CHIBI_PROCESS_OK) == 5);
    REQUIRE(timing_rows(options.timings, "execute", 0, 0) == 1);
    REQUIRE(timing_observed(options.timings, 5) < timing_observed(options.timings, 1));
    REQUIRE(!fclose(options.timings));
    return true;
}

/* Waiting for one step drains the others, so a step that writes more than a
 * pipe holds still finishes; output becomes the last output in wait order. */
TEST_CASE(private_waits_take_output_in_wait_order)
{
    static const char *source =
        "package jobs\n"
        "Build :: () -> int { worker := "
        "build.environment(\"CHIBI_BUILD_WORKER\"); "
        "large := build.start([worker, \"build-output\", \"300000\"]); "
        "argv := build.start([worker, \"build-argv\", \"first\"]); "
        "code := build.start([worker, \"exit\", \"7\"]); "
        "if (build.wait(code) != 7) { return 1; } "
        "if (build.wait(large) != 0 || text_byte_length(build.last_stdout()) "
        "!= 300000) { return 2; } "
        "held:=build.last_stdout(); build.platform(); "
        "if (build.wait(argv) != 23 || build.after(build.last_stdout(), "
        "\"arg=5:\") != \"first\\n\") { return 3; } "
        "if (build.last_stderr() != \"worker-stderr\\n\") { return 4; } "
        "if (text_byte_length(held)!=300000 || text_byte_at(held,0)!=111 || "
        "text_byte_at(held,299999)!=111) { return 5; } "
        "return 0; }";
    for (int b = 0; b < 2; b++) {
        behavior_backend backend = b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        chibi_build_options options = primitive_options(source, backend);
        chibi_build_result result;
        REQUIRE(chibi_build_invoke(&options, &result));
        REQUIRE(result.value == 0 && result.steps == 3);
        REQUIRE(strstr(result.stdout_text, "oooo") && result.stdout_truncated);
    }
    return true;
}

TEST_CASE(private_waits_refuse_unknown_and_repeated_handles)
{
    static const char *sources[] = {
        "package jobs\nBuild :: () -> int { return build.wait(1); }",
        "package jobs\nBuild :: () -> int { "
        "step := build.start([build.environment(\"CHIBI_BUILD_WORKER\"), "
        "\"exit\", \"0\"]); "
        "build.wait(step); return build.wait(step); }",
    };
    for (int i = 0; i < 2; i++) {
        chibi_build_options options = primitive_options(sources[i], BEHAVIOR_BYTECODE);
        chibi_build_result result;
        REQUIRE(!chibi_build_invoke(&options, &result));
        REQUIRE(!strcmp(result.diagnostic, "unknown build job"));
    }
    return true;
}

/* wait_for gives a running step's marker once its limit passes and leaves the
 * step running, finishes one that ends first, and kill stops a step and returns
 * its status as the negative signal number; both refuse a finished handle. */
TEST_CASE(private_waits_have_limits_and_steps_can_be_killed)
{
    static const char
        *source = "package jobs\n"
                  "Build :: () -> int { worker := "
                  "build.environment(\"CHIBI_BUILD_WORKER\"); "
                  "slow := build.start([worker, \"sleep\", \"30\"]); "
                  "quick := build.start([worker, \"exit\", \"7\"]); "
                  "if (build.wait_for(slow, 100) != 65536) { return 1; } "
                  "if (build.wait_for(quick, -1) != 7) { return 2; } "
                  "if (build.kill(slow) != -9) { return 3; } "
                  "kept := build.last_stdout(); "
                  "spawned := build.spawn([worker, \"exit\", \"5\"]); "
                  "if (build.wait_for(spawned, -1) != 5) { return 4; } "
                  "if (build.last_stdout() != kept) { return 5; } "
                  "return 0; }";
    chibi_build_options options = primitive_options(source, BEHAVIOR_BYTECODE);
    double started = now();
    chibi_build_result result;
    REQUIRE(chibi_build_invoke(&options, &result));
    REQUIRE(result.value == 0 && now() - started < 10);
    static const char
        *finished = "package jobs\nBuild :: () -> int { "
                    "step := "
                    "build.start([build.environment(\"CHIBI_BUILD_WORKER\"), "
                    "\"exit\", \"0\"]); "
                    "build.wait_for(step, -1); return build.kill(step); }";
    options = primitive_options(finished, BEHAVIOR_BYTECODE);
    REQUIRE(!chibi_build_invoke(&options, &result));
    REQUIRE(!strcmp(result.diagnostic, "unknown build job"));
    return true;
}

/* A step the program never waits for stops when the program returns, and a
 * wait past the deadline fails the program. */
TEST_CASE(private_unwaited_and_late_steps_stop)
{
    static const char
        *unwaited = "package jobs\nBuild :: () -> int { "
                    "build.start([build.environment(\"CHIBI_BUILD_WORKER\"), "
                    "\"sleep\", \"30\"]); return 0; }";
    chibi_build_options options = primitive_options(unwaited, BEHAVIOR_BYTECODE);
    primitive_started = 0;
    options.prepared = primitive_prepared;
    options.timings = tmpfile();
    REQUIRE(options.timings);
    chibi_build_result result;
    REQUIRE(chibi_build_invoke(&options, &result));
    REQUIRE(result.value == 0 && now() - primitive_started < 2);
    REQUIRE(timing_rows(options.timings, "abandoned", -SIGKILL, CHIBI_PROCESS_OK) == 1);
    REQUIRE(!fclose(options.timings));
    static const char
        *late = "package jobs\nBuild :: () -> int { return build.wait("
                "build.start([build.environment(\"CHIBI_BUILD_WORKER\"), "
                "\"sleep\", \"30\"])); }";
    options = primitive_options(late, BEHAVIOR_BYTECODE);
    options.deadline = now() + 0.5;
    options.timings = tmpfile();
    REQUIRE(options.timings);
    double started = now();
    REQUIRE(!chibi_build_invoke(&options, &result));
    REQUIRE(!strcmp(result.diagnostic, "build process timeout") && now() - started < 2);
    REQUIRE(
        timing_rows(options.timings, "execute", 0, 1) +
            timing_rows(options.timings, "prepare", 0, 1) ==
        1
    );
    REQUIRE(!fclose(options.timings));
    return true;
}

/* Start cancellation after preparation so this witnesses an interrupted child,
 * not merely an expired compilation budget. */
static void timing_cancel_prepared()
{
    struct itimerval timer = { .it_value = { .tv_usec = 200000 } };
    assert(!setitimer(ITIMER_REAL, &timer, NULL));
}

TEST_CASE(build_timings_record_interrupted_children)
{
    const char
        *source = "package jobs\nBuild :: () -> int { return build.wait("
                  "build.start([build.environment(\"CHIBI_BUILD_WORKER\"), "
                  "\"sleep\", \"30\"])); }";
    chibi_build_options options = primitive_options(source, BEHAVIOR_BYTECODE);
    options.prepared = timing_cancel_prepared;
    options.cancelled = &cancelled;
    options.timings = tmpfile();
    REQUIRE(options.timings);
    struct sigaction action = { .sa_handler = cancel_alarm }, previous;
    sigemptyset(&action.sa_mask);
    REQUIRE(!sigaction(SIGALRM, &action, &previous));
    struct itimerval previous_timer, timer = { 0 };
    assert(!getitimer(ITIMER_REAL, &previous_timer));
    cancelled = 0;
    chibi_build_result result;
    bool success = chibi_build_invoke(&options, &result);
    assert(!setitimer(ITIMER_REAL, &timer, NULL));
    assert(!sigaction(SIGALRM, &previous, NULL));
    assert(!setitimer(ITIMER_REAL, &previous_timer, NULL));
    cancelled = 0;
    REQUIRE(!success && result.steps == 1);
    REQUIRE(!strcmp(result.diagnostic, "build process cancelled"));
    REQUIRE(timing_rows(options.timings, "process", 0, CHIBI_PROCESS_CANCELLED) == 1);
    REQUIRE(timing_rows(options.timings, "execute", 0, 1) == 1);
    REQUIRE(!fclose(options.timings));
    return true;
}

TEST_CASE(private_value_helpers_preserve_nul_and_validate_c_boundaries)
{
    build_values values = { 0 };
    pin_value item = { .type = PIN_STR, .text = build_values_text(&values, "a\0b", 3) };
    pin_value delimiter = { .type = PIN_STR,
                            .text = build_values_text(&values, "\0", 1) };
    REQUIRE(item.text && delimiter.text);
    pin_array *items = build_values_array(&values, 2);
    REQUIRE(items);
    items->items[0] = item;
    items->items[1] = item;
    pin_value arguments[] = {
        { .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = items },
        delimiter,
    };
    pin_value result = { .type = PIN_STR };
    REQUIRE(!build_values_join(&values, arguments, &result));
    pin_string joined = pin_text(result);
    REQUIRE(joined.length == 7 && !memcmp(joined.bytes, "a\0b\0a\0b", 7));
    arguments[0] = item;
    REQUIRE(!build_values_after(&values, arguments, &result));
    REQUIRE(pin_text(result).length == 1 && pin_text(result).bytes[0] == 'b');
    REQUIRE(!build_values_before(&values, arguments, &result));
    REQUIRE(pin_text(result).length == 1 && pin_text(result).bytes[0] == 'a');
    pin_value replace[] = {
        item,
        delimiter,
        { .type = PIN_STR, .text = build_values_text(&values, "x\0y", 3) },
    };
    REQUIRE(replace[2].text && !build_values_replace(&values, replace, &result));
    REQUIRE(pin_text(result).length == 5 && !memcmp(pin_text(result).bytes, "ax\0yb", 5));
    replace[1].text = build_values_text(&values, "b", 1);
    replace[2].text = build_values_text(&values, "x", 1);
    REQUIRE(
        replace[1].text && replace[2].text &&
        !build_values_replace(&values, replace, &result)
    );
    REQUIRE(pin_text(result).length == 3 && !memcmp(pin_text(result).bytes, "a\0x", 3));
    const char *unchanged = "unchanged";
    const char *native = unchanged;
    REQUIRE(build_values_cstring(&values, item, &native));
    REQUIRE(native == unchanged);
    /* A slice's copy lasts only for the host call, so a graph's millions of
     * path arguments never fill the values that outlive calls. */
    pin_string slice = { .bytes = "path-tail", .length = 4 };
    pin_value path = { .type = PIN_STR, .text = &slice };
    size_t kept, capacity;
    arena_usage(&values.memory, &kept, &capacity);
    REQUIRE(!build_values_cstring(&values, path, &native));
    REQUIRE(!strcmp(native, "path"));
    size_t after;
    arena_usage(&values.memory, &after, &capacity);
    REQUIRE(after == kept);
    build_values_begin_call(&values);
    REQUIRE(!build_values_cstring(&values, path, &native));
    REQUIRE(!strcmp(native, "path"));
    build_values_free(&values);
    return true;
}

TEST_CASE(private_last_output_preserves_bytes_and_refuses_truncation)
{
    static const char *sources[] = {
        "package output\nBuild :: () -> int { "
        "build.run([build.environment(\"CHIBI_BUILD_WORKER\"),\"build-"
        "output\",\"1048577\"]); build.last_stdout(); return 0; }",
        "package output\nBuild :: () -> int { "
        "build.run([build.environment(\"CHIBI_BUILD_WORKER\"),\"build-"
        "output\",\"1048577\"]); build.last_stderr(); return 0; }",
        "package output\nBuild :: () -> int { "
        "build.run([build.environment(\"CHIBI_BUILD_WORKER\"),\"build-"
        "binary\",\"stdout\"]); value:=build.last_stdout(); "
        "if(text_byte_length(value)!=3 || text_byte_at(value,1)!=0 || "
        "text_byte_at(value,2)!=121) { return 1; } return 0; }",
        "package output\nBuild :: () -> int { "
        "build.run([build.environment(\"CHIBI_BUILD_WORKER\"),\"build-"
        "binary\",\"stderr\"]); value:=build.last_stderr(); "
        "if(text_byte_length(value)!=3 || text_byte_at(value,1)!=0 || "
        "text_byte_at(value,2)!=121) { return 1; } return 0; }",
    };
    static const char *diagnostics[] = {
        "build stdout truncated",
        "build stderr truncated",
        NULL,
        NULL,
    };
    for (int b = 0; b < 2; b++) {
        behavior_backend backend = b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        for (int i = 0; i < 4; i++) {
            chibi_build_options options = primitive_options(sources[i], backend);
            chibi_build_result result;
            REQUIRE(chibi_build_invoke(&options, &result) == (i >= 2));
            if (i < 2) {
                REQUIRE(!strcmp(result.diagnostic, diagnostics[i]));
            } else {
                REQUIRE(!result.value && !result.diagnostic[0]);
            }
            REQUIRE(result.steps == 1 && primitive_backend(&result, backend));
            REQUIRE(result.stdout_truncated == (i < 2));
            REQUIRE(result.stderr_truncated == (i < 2));
        }
    }
    return true;
}

static void primitive_file(const char *name, const char *text, size_t size)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s", root, name);
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(text, 1, size, file) == size);
    assert(!fclose(file));
}

/* Build callbacks recycle their borrowed result bank. Public byte conversions
 * must retain exact binary values across later callbacks and a native write. */
TEST_CASE(private_core_bytes_survive_native_result_bank_reuse)
{
    static const unsigned char expected[] = { 65, 0, 255, 66 };
    static const char *const arguments[] = { "primitive-byte-bridge-input",
                                             "primitive-byte-bridge-output" };
    primitive_file(arguments[0], (const char *)expected, sizeof expected);
    static const char source[] =
        "package byte_bridge_lifetime\n"
        "\n"
        "import bytes \"core:bytes\"\n"
        "\n"
        "Exact :: (value: string) -> bool {\n"
        "    return text_byte_length(value) == 4 &&\n"
        "           text_byte_at(value, 0) == 65 && text_byte_at(value, 1) == "
        "0 &&\n"
        "           text_byte_at(value, 2) == 255 && text_byte_at(value, 3) == "
        "66;\n"
        "}\n"
        "\n"
        "Build :: () -> int {\n"
        "    args := build.arguments();\n"
        "    if (length(args) != 2) { return 1; }\n"
        "    read_text := build.read_file(args[0]);\n"
        "    raw := bytes.FromString(read_text);\n"
        "    kept := bytes.ToString(raw);\n"
        "    build.platform();\n"
        "    build.root();\n"
        "    if (length(raw) != 4 || raw[0] != u8(65) || raw[1] != u8(0) ||\n"
        "        raw[2] != u8(255) || raw[3] != u8(66) ||\n"
        "        !Exact(read_text) || !Exact(kept)) { return 2; }\n"
        "    raw[0] = u8(90);\n"
        "    build.platform();\n"
        "    if (!Exact(read_text) || !Exact(kept) || raw[0] != u8(90)) { "
        "return 3; }\n"
        "    if (!build.write_file(args[1], kept)) { return 4; }\n"
        "    read_back := build.read_file(args[1]);\n"
        "    build.platform();\n"
        "    again := build.arguments();\n"
        "    if (again[0] != \"primitive-byte-bridge-input\" ||\n"
        "        again[1] != \"primitive-byte-bridge-output\") { return 5; }\n"
        "    if (!Exact(read_text) || !Exact(kept) || !Exact(read_back) ||\n"
        "        raw[0] != u8(90)) { return 6; }\n"
        "    return 0;\n"
        "}\n";
    for (int b = 0; b < 2; b++) {
        behavior_backend backend = b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        primitive_file(arguments[1], "stale", 5);
        chibi_build_options options = primitive_options(source, backend);
        options.core = CHIBI_SOURCE_ROOT "/packages/core";
        options.arguments = arguments;
        options.argument_count = 2;
        chibi_build_result result;
        double started = chibi_build_now();
        bool invoked = chibi_build_invoke(&options, &result);
        if (!invoked || result.value) {
            fprintf(
                stderr,
                "core byte bridge: backend=%d invoked=%d value=%d flow=%d "
                "elapsed=%.3f diagnostic=[%s]\n",
                b,
                invoked,
                result.value,
                result.flow_status,
                chibi_build_now() - started,
                result.diagnostic
            );
        }
        REQUIRE(invoked && !result.value && !result.steps);
        REQUIRE(
            !result.counts.overflow && !result.counts.inspection_bytecode_fallbacks &&
            !result.counts.size_bytecode_fallbacks
        );
        uint64_t entries = b ? result.counts.sljit_entries
                             : result.counts.bytecode_entries;
        uint64_t other = b ? result.counts.bytecode_entries : result.counts.sljit_entries;
        REQUIRE(entries > 0 && !other && !result.counts.wasm_entries);
        char path[256];
        snprintf(path, sizeof path, "%s/%s", root, arguments[1]);
        FILE *file = fopen(path, "rb");
        REQUIRE(file);
        unsigned char actual[sizeof expected];
        size_t count = fread(actual, 1, sizeof actual, file);
        int trailing = fgetc(file);
        bool exact = count == sizeof expected && trailing == EOF &&
                     !ferror(file) && !memcmp(actual, expected, sizeof expected);
        int closed = fclose(file);
        REQUIRE(exact && !closed);
    }
    return true;
}

TEST_CASE(private_file_reads_preserve_bytes_and_reject_invalid_files)
{
    primitive_file("primitive-text", "read me\n", 8);
    primitive_file("primitive-nul", "a\0b", 3);
    primitive_file("primitive-a", "original", 8);
    char path[256];
    snprintf(path, sizeof path, "%s/primitive-fifo", root);
    REQUIRE(!mkfifo(path, 0600));
    snprintf(path, sizeof path, "%s/primitive-large", root);
    FILE *file = fopen(path, "wb");
    assert(file && !ftruncate(fileno(file), BUILD_TEXT_LIMIT + 1));
    assert(!fclose(file));
    static const char *sources[] = {
        "package read\nBuild :: () -> int { "
        "if(build.read_file(\"primitive-text\")!=\"read me\\n\" || "
        "!build.file_exists(\"primitive-text\")) { return 1; } return 0; }",
        "package read\nBuild :: () -> int { "
        "if(build.read_file(\"primitive-missing\")!=\"\" || "
        "build.file_exists(\"primitive-missing\") || "
        "build.resolve_path(\"primitive-missing\")!=\"\") { return 1; } return "
        "0; }",
        "package read\nBuild :: () -> int { "
        "build.read_file(\"primitive-fifo\"); return 0; }",
        "package read\nBuild :: () -> int { "
        "value:=build.read_file(\"primitive-nul\"); "
        "if(text_byte_length(value)!=3 || text_byte_at(value,1)!=0 || "
        "text_byte_at(value,2)!=98 || "
        "!build.write_file(\"primitive-nul-copy\",value) || "
        "build.read_file(\"primitive-nul-copy\")!=value) { return 1; } "
        "if(build.write_file(build.join([\"primitive-\",value],\"\"),"
        "\"bad\")) "
        "{ return 2; } if(build.read_file(\"primitive-a\")!=\"original\") "
        "{ return 3; } return 0; }",
        "package read\nBuild :: () -> int { "
        "build.read_file(\"primitive-large\"); return 0; }",
        "package read\nBuild :: () -> int { build.read_file(\".\"); return "
        "0; "
        "}",
    };
    static const char *diagnostics[] = {
        NULL,
        NULL,
        "build file must be regular data of at most 16 MiB",
        NULL,
        "build file must be regular data of at most 16 MiB",
        "build file must be regular data of at most 16 MiB",
    };
    for (int b = 0; b < 2; b++) {
        behavior_backend backend = b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        for (int i = 0; i < 6; i++) {
            chibi_build_options options = primitive_options(sources[i], backend);
            chibi_build_result result;
            bool success = chibi_build_invoke(&options, &result);
            if (!success && (i < 2 || i == 3)) {
                fprintf(
                    stderr,
                    "build read: case=%d backend=%d %s\n",
                    i,
                    b,
                    result.diagnostic
                );
            }
            REQUIRE(success == (i < 2 || i == 3));
            REQUIRE(!result.steps && primitive_backend(&result, backend));
            if (i < 2 || i == 3) {
                REQUIRE(!result.value && !result.diagnostic[0]);
            } else {
                REQUIRE(!strcmp(result.diagnostic, diagnostics[i]));
            }
        }
    }
    return true;
}

TEST_CASE(private_file_and_value_limits_accept_exact_text_and_reject_growth)
{
    size_t limit = BUILD_TEXT_LIMIT;
    char *text = malloc(limit);
    assert(text);
    memset(text, 'r', limit);
    primitive_file("primitive-limit", text, limit);
    free(text);
    char many_lines[2 * 8193];
    for (int i = 0; i < 8193; i++) {
        many_lines[2 * i] = 'a';
        many_lines[2 * i + 1] = '\n';
    }
    primitive_file("primitive-many-lines", many_lines, sizeof many_lines);
    static const char *sources[] = {
        "package limit\nBuild :: () -> int { "
        "if(!build.write_file(\"primitive-limit-copy\",build.read_file("
        "\"primitive-"
        "limit\"))) { return 1; } return 0; }",
        "package limit\nBuild :: () -> int { "
        "build.join([build.read_file(\"primitive-limit\"),\"x\"],\"\"); return "
        "0; "
        "}",
        ("package limit\nBuild :: () -> int { "
         "build.lines(build.read_file(\"primitive-many-lines\")); return 0; }"),
        ("package limit\nBuild :: () -> int { "
         "build.replace(build.read_file(\"primitive-limit\"),\"r\",\"rr\"); "
         "return 0; }"),
    };
    static const char *const refusals[] = { "",
                                            "invalid build string arguments",
                                            "build array limit exceeded",
                                            "build text limit exceeded" };
    for (int b = 0; b < 2; b++) {
        behavior_backend backend = b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        for (int i = 0; i < 4; i++) {
            chibi_build_options options = primitive_options(sources[i], backend);
            chibi_build_result result;
            REQUIRE(chibi_build_invoke(&options, &result) == (i == 0));
            REQUIRE(!result.steps && primitive_backend(&result, backend));
            if (!i) {
                REQUIRE(!result.value);
                char path[256];
                snprintf(path, sizeof path, "%s/primitive-limit-copy", root);
                struct stat info;
                REQUIRE(!stat(path, &info) && info.st_size == (off_t)limit);
                FILE *file = fopen(path, "rb");
                assert(file && !fseek(file, -1, SEEK_END));
                int last = fgetc(file);
                assert(!fclose(file));
                REQUIRE(last == 'r');
            } else {
                REQUIRE(!strcmp(result.diagnostic, refusals[i]));
            }
        }
    }
    return true;
}

/* A file's bytes as the C initializer CMake writes from file(READ ... HEX),
 * NUL and high bytes included; a missing file fails the program. */
TEST_CASE(private_byte_lists_write_cmake_initializers)
{
    primitive_file(
        "primitive-bytes",
        "\0\xff"
        "a",
        3
    );
    static const char *sources[] = {
        "package bytes\nBuild :: () -> int { "
        "if (build.byte_list(\"primitive-bytes\") != \"0x00,0xff,0x61,\") { "
        "return 1; } return 0; }",
        "package bytes\nBuild :: () -> int { "
        "build.byte_list(\"primitive-absent\"); return 0; }",
    };
    for (int i = 0; i < 2; i++) {
        chibi_build_options options = primitive_options(sources[i], BEHAVIOR_BYTECODE);
        chibi_build_result result;
        REQUIRE(chibi_build_invoke(&options, &result) == !i);
        REQUIRE(
            i ? !strcmp(result.diagnostic, "build byte list input must be a regular file of at most 3.2 MiB")
              : !result.value
        );
    }
    return true;
}

/* A bank still rejects growth past its bound. Native results are imported
 * before the next callback reuses its bank; live values and arguments survive. */
TEST_CASE(private_value_storage_reuses_results_and_preserves_live_values)
{
    size_t size = 1024 * 1024;
    char *text = malloc(size);
    assert(text);
    memset(text, 's', size);
    text[0] = 'a';
    text[1] = 0;
    text[2] = 'b';
    text[3] = '\n';
    primitive_file("primitive-storage", text, size);
    build_values bank = { 0 };
    int retained = 0;
    while (retained < 300 && build_values_text(&bank, text, size)) {
        retained++;
    }
    free(text);
    REQUIRE(retained > 0 && retained < 300);
    REQUIRE(bank.error && !strcmp(bank.error, "build value storage limit exceeded"));
    REQUIRE(arena_storage_bytes(&bank.memory) <= BUILD_STORAGE_LIMIT);
    build_values_free(&bank);
    char *source = tool_read(CHIBI_SOURCE_ROOT "/test/language/BuildValueLifetime.chibi");
    REQUIRE(source);
    const char *arguments[] = { "primitive-storage", "root argument" };
    for (int b = 0; b < 2; b++) {
        behavior_backend backend = b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        chibi_build_options options = primitive_options(source, backend);
        options.arguments = arguments;
        options.argument_count = 2;
        chibi_build_result result;
        double started = chibi_build_now();
        bool invoked = chibi_build_invoke(&options, &result);
        if (!invoked || result.value) {
            fprintf(
                stderr,
                "build storage case=%d invoked=%d value=%d flow=%d "
                "elapsed=%.3f diagnostic=[%s]\n",
                b,
                invoked,
                result.value,
                result.flow_status,
                chibi_build_now() - started,
                result.diagnostic
            );
        }
        REQUIRE(invoked && !result.value);
        REQUIRE(!result.steps && primitive_backend(&result, backend));
    }
    free(source);
    return true;
}

static bool no_write_temporary(const char *prefix)
{
    DIR *directory = opendir(root);
    assert(directory);
    bool absent = true;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (!strncmp(entry->d_name, prefix, strlen(prefix))) {
            absent = false;
        }
    }
    assert(!closedir(directory));
    return absent;
}

TEST_CASE(private_atomic_writes_preserve_target_on_rename_failure)
{
    primitive_file("primitive-atomic", "original", 8);
    char directory[256], marker[256], target[256];
    snprintf(directory, sizeof directory, "%s/primitive-directory", root);
    REQUIRE(!mkdir(directory, 0700));
    snprintf(marker, sizeof marker, "%s/primitive-directory/marker", root);
    tool_write(marker, "unchanged");
    snprintf(target, sizeof target, "%s/primitive-atomic", root);
    static const char *source =
        "package write\nBuild :: () -> int { "
        "if(!build.make_directory(\"primitive/nested\") || "
        "!build.make_directory(\"primitive/nested\")) { return 1; } "
        "if(build.make_directory(\"primitive-atomic/child\")) { return "
        "2; } "
        "if(!build.write_file(\"primitive-atomic\",\"replacement\")) { "
        "return 3; } "
        "if(build.write_file(\"primitive-directory\",\"cannot replace "
        "directory\")) { return 4; } "
        "if(build.write_file(\"primitive-no-parent/file\",\"missing "
        "parent\")) { return 5; } "
        "if(build.read_file(\"primitive-atomic\")!=\"replacement\") { "
        "return 6; } return 0; }";
    for (int b = 0; b < 2; b++) {
        behavior_backend backend = b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        struct stat before, after;
        REQUIRE(!stat(target, &before));
        chibi_build_options options = primitive_options(source, backend);
        chibi_build_result result;
        REQUIRE(chibi_build_invoke(&options, &result));
        REQUIRE(!result.value && !result.steps && primitive_backend(&result, backend));
        REQUIRE(!stat(target, &after) && before.st_ino != after.st_ino);
        char *text = tool_read(marker);
        bool retained = text && !strcmp(text, "unchanged");
        free(text);
        REQUIRE(retained);
        REQUIRE(no_write_temporary("primitive-directory."));
        REQUIRE(no_write_temporary("primitive-atomic."));
    }
    return true;
}

TEST_CASE(private_argument_bounds_reject_before_authored_execution)
{
    static const char *source = "package args\nBuild :: () -> int { return "
                                "build.length(build.arguments()); }";
    chibi_build_options unowned = primitive_options(source, BEHAVIOR_BYTECODE);
    unowned.compiler = NULL;
    chibi_build_result rejected;
    REQUIRE(!chibi_build_invoke(&unowned, &rejected));
    REQUIRE(!strcmp(rejected.diagnostic, "build compiler is required"));
    REQUIRE(
        !rejected.steps && !rejected.counts.bytecode_entries && !rejected.counts.sljit_entries
    );
    const char *arguments[257];
    for (int i = 0; i < 257; i++) {
        arguments[i] = "";
    }
    char long_argument[65537];
    memset(long_argument, 'x', sizeof long_argument - 1);
    long_argument[sizeof long_argument - 1] = 0;
    const char *too_long[] = { long_argument };
    const char *missing[] = { NULL };
    for (int b = 0; b < 2; b++) {
        behavior_backend backend = b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        for (int i = 0; i < 5; i++) {
            chibi_build_options options = primitive_options(source, backend);
            options.argument_count = i == 0 ? -1 : i == 1 ? 257 : 1;
            options.arguments = i == 2 ? NULL
                              : i == 3 ? missing
                              : i == 4 ? too_long
                                       : arguments;
            chibi_build_result result;
            REQUIRE(!chibi_build_invoke(&options, &result));
            REQUIRE(
                !strcmp(result.diagnostic, i < 3 ? "invalid build host options" : "invalid build host arguments")
            );
            REQUIRE(
                !result.steps && !result.counts.bytecode_entries &&
                !result.counts.sljit_entries
            );
        }
        chibi_build_options options = primitive_options(source, backend);
        options.arguments = arguments;
        options.argument_count = 256;
        chibi_build_result result;
        REQUIRE(chibi_build_invoke(&options, &result));
        REQUIRE(result.value == 256 && primitive_backend(&result, backend));
        long_argument[65535] = 0;
        options.arguments = too_long;
        options.argument_count = 1;
        REQUIRE(chibi_build_invoke(&options, &result));
        REQUIRE(result.value == 1 && primitive_backend(&result, backend));
        long_argument[65535] = 'x';
    }
    return true;
}

static bool run_primitives(const char *source, behavior_backend backend, int steps)
{
    chibi_build_options options = primitive_options(source, backend);
    chibi_build_result result;
    bool success = chibi_build_invoke(&options, &result);
    if (!success || result.value) {
        fprintf(stderr, "build primitives: value=%d %s\n", result.value, result.diagnostic);
    }
    return success && !result.value && result.steps == steps &&
           primitive_backend(&result, backend);
}

static bool fails_with(const char *source, behavior_backend backend, const char *diagnostic)
{
    chibi_build_options options = primitive_options(source, backend);
    chibi_build_result result;
    return !chibi_build_invoke(&options, &result) &&
           !strcmp(result.diagnostic, diagnostic) &&
           primitive_backend(&result, backend);
}

static off_t file_size(const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s", root, name);
    struct stat info;
    return stat(path, &info) ? -1 : info.st_size;
}

TEST_CASE(private_step_output_fills_one_text_value_and_probes_stay_unreported)
{
    static const char *source =
        "package output\nBuild :: () -> int { "
        "worker:=build.environment(\"CHIBI_BUILD_WORKER\"); "
        "if(build.probe([worker,\"build-argv\",\"probe\"])!=23) { "
        "return 1; } "
        "if(build.after(build.last_stdout(),\"arg=5:\")!=\"probe\\n\" || "
        "build.last_stderr()!=\"worker-stderr\\n\") { return 2; } "
        "if(build.run([worker,\"build-output\",\"1048576\"])!=0) { "
        "return 3; } "
        "if(!build.write_file(\"step-stdout\",build.last_stdout()) || "
        "!build.write_file(\"step-stderr\",build.last_stderr())) { return "
        "4; } "
        "return 0; }";
    for (int b = 0; b < 2; b++) {
        behavior_backend backend = b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        chibi_build_options options = primitive_options(source, backend);
        chibi_build_result result;
        bool success = chibi_build_invoke(&options, &result);
        if (!success || result.value) {
            fprintf(stderr, "step output: value=%d %s\n", result.value, result.diagnostic);
        }
        REQUIRE(success && !result.value && result.steps == 2);
        REQUIRE(primitive_backend(&result, backend));
        REQUIRE(
            file_size("step-stdout") == 1024 * 1024 &&
            file_size("step-stderr") == 1024 * 1024
        );
        /* The reported step fills the result; the probe added nothing first. */
        REQUIRE(result.stdout_text[0] == 'o' && result.stderr_text[0] == 'e');
        REQUIRE(result.stdout_size == CHIBI_BUILD_OUTPUT_CAPACITY - 1 && result.stdout_truncated);
        REQUIRE(result.stderr_size == CHIBI_BUILD_OUTPUT_CAPACITY - 1 && result.stderr_truncated);
    }
    return true;
}

static void primitive_link(const char *target, const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s", root, name);
    assert(!symlink(target, path));
}

static void primitive_directory(const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s", root, name);
    assert(!mkdir(path, 0700));
}

TEST_CASE(private_metadata_reports_kinds_stamps_and_sorted_entries)
{
    primitive_directory("meta");
    primitive_directory("meta/c");
    primitive_file("meta/b", "b", 1);
    primitive_file("meta/a", "a", 1);
    primitive_file("meta/.hidden", "", 0);
    primitive_link("b", "meta/link");
    primitive_link("nowhere", "meta/dangling");
    primitive_link("loop", "meta/loop");
    char path[256];
    snprintf(path, sizeof path, "%s/meta/fifo", root);
    REQUIRE(!mkfifo(path, 0600));
    static const char *source =
        "package meta\nBuild :: () -> int { "
        "kinds:=[build.file_kind(\"meta\"),build.file_kind(\"meta/"
        "a\"),build.file_kind(\"meta/link\"),"
        "build.file_kind(\"meta/dangling\"),build.file_kind(\"meta/"
        "fifo\"),build.file_kind(\"meta/none\"),"
        "build.file_kind(\"meta/a/below\")]; "
        "if(build.join(kinds,\"|\")!=\"directory|file|link|link|other||"
        "\") { return 1; } "
        "if(build.join(build.list_directory(\"meta\"),\"|\")!=\".hidden|"
        "a|b|c|dangling|fifo|link|loop\") { "
        "return 2; } "
        "if(build.length(build.list_directory(\"meta/c\"))!=0 || "
        "build.length(build.list_directory(\"meta/none\"))!=0) { return "
        "3; } "
        "stamp:=build.file_stamp(\"meta/a\"); "
        "if(stamp==\"\" || "
        "build.file_stamp(\"meta/link\")!=build.file_stamp(\"meta/b\") || "
        "build.file_stamp(\"meta/b\")==stamp) { return 4; } "
        "if(build.file_stamp(\"meta/dangling\")!=\"\" || "
        "build.file_stamp(\"meta/none\")!=\"\") { return 5; } "
        "if(!build.write_file(\"meta/a\",\"a\") || "
        "build.file_stamp(\"meta/a\")==stamp) { return 6; } "
        "return 0; }";
    static const char *failures[] = {
        "package meta\nBuild :: () -> int { "
        "build.list_directory(\"meta/a\"); "
        "return 0; }",
        "package meta\nBuild :: () -> int { "
        "build.file_stamp(\"meta/loop\"); "
        "return 0; }",
        "package meta\nBuild :: () -> int { "
        "build.file_kind(\"meta/loop/x\"); "
        "return 0; }",
        "package meta\nBuild :: () -> int { "
        "build.list_directory(\"many\"); "
        "return 0; }",
    };
    static const char *diagnostics[] = {
        "build directory list failed",
        "build file metadata failed",
        "build file metadata failed",
        "build array limit exceeded",
    };
    primitive_directory("many");
    for (int i = 0; i < 8193; i++) {
        snprintf(path, sizeof path, "many/%d", i);
        primitive_file(path, "", 0);
    }
    for (int b = 0; b < 2; b++) {
        behavior_backend backend = b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        REQUIRE(run_primitives(source, backend, 0));
        for (int i = 0; i < 4; i++) {
            REQUIRE(fails_with(failures[i], backend, diagnostics[i]));
        }
    }
    return true;
}

TEST_CASE(private_copies_renames_and_removals_leave_whole_files)
{
    primitive_file("copy-source", "bytes\0and more", 15);
    char path[256];
    snprintf(path, sizeof path, "%s/copy-source", root);
    REQUIRE(!chmod(path, 0751));
    primitive_file("copy-existing", "old", 3);
    primitive_directory("copy-directory");
    primitive_file("copy-directory/keep", "", 0);
    primitive_directory("copy-empty");
    primitive_link("copy-source", "copy-link");
    static const char *source =
        "package copy\nBuild :: () -> int { "
        "if(!build.copy_file(\"copy-source\",\"copy-existing\") || "
        "!build.copy_file(\"copy-link\",\"copy-new\")) { return 1; } "
        "if(build.copy_file(\"copy-missing\",\"copy-other\") || "
        "build.copy_file(\"copy-directory\",\"copy-other\") || "
        "build.copy_file(\"copy-source\",\"copy-none/file\") || "
        "build.copy_file(\"copy-source\",\"copy-directory\")) { return 2; } "
        "if(!build.rename(\"copy-new\",\"copy-renamed\") || "
        "build.file_kind(\"copy-new\")!=\"\" || "
        "build.rename(\"copy-missing\",\"copy-other\") || "
        "build.rename(\"copy-renamed\",\"copy-none/file\")) { return 3; } "
        "if(!build.remove(\"copy-renamed\") || !build.remove(\"copy-renamed\") "
        "|| "
        "!build.remove(\"copy-empty\") || build.remove(\"copy-directory\")) { "
        "return 4; } "
        "if(!build.remove(\"copy-link\") || "
        "build.file_kind(\"copy-source\")!=\"file\") { return 5; } "
        "return 0; }";
    for (int b = 0; b < 2; b++) {
        behavior_backend backend = b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        struct stat before, after;
        snprintf(path, sizeof path, "%s/copy-existing", root);
        REQUIRE(!stat(path, &before));
        REQUIRE(run_primitives(source, backend, 0));
        REQUIRE(!stat(path, &after) && before.st_ino != after.st_ino);
        REQUIRE((after.st_mode & 0777) == 0751 && after.st_size == 15);
        FILE *file = fopen(path, "rb");
        char bytes[16] = { 0 };
        REQUIRE(file && fread(bytes, 1, sizeof bytes, file) == 15);
        fclose(file);
        REQUIRE(!memcmp(bytes, "bytes\0and more", 15));
        REQUIRE(file_size("copy-directory/keep") == 0 && file_size("copy-other") < 0);
        REQUIRE(no_write_temporary("copy-directory.") && no_write_temporary("copy-existing."));
        primitive_link("copy-source", "copy-link");
        primitive_directory("copy-empty");
    }
    return true;
}

TEST_CASE(private_directory_publication_refuses_existing_paths)
{
    static const char *source =
        "package publish\nBuild :: () -> int { "
        "if(!build.reserve_directory(\"publish/stage\") || "
        "build.reserve_directory(\"publish/stage\") || "
        "build.reserve_directory(\"publish/file\") || "
        "build.reserve_directory(\"publish/link\") || "
        "build.reserve_directory(\"publish/dangling\") || "
        "build.reserve_directory(\"publish/missing/child\")) { return 1; } "
        "if(build.file_kind(\"publish/missing\")!=\"\" || "
        "!build.write_file(\"publish/stage/payload\",\"source bytes\")) { "
        "return 2; } "
        "stamp:=build.file_stamp(\"publish/stage/payload\"); "
        "if(build.publish_directory(\"publish/stage\",\"publish/file\") || "
        "build.publish_directory(\"publish/stage\",\"publish/empty\") || "
        "build.publish_directory(\"publish/stage\",\"publish/link\") || "
        "build.publish_directory(\"publish/stage\",\"publish/dangling\") || "
        "build.publish_directory(\"publish/stage\",\"publish/stage\") || "
        "build.publish_directory(\"publish/stage\",\"publish/missing/child\")) "
        "{ return 3; } "
        "if(build.file_kind(\"publish/stage\")!=\"directory\" || "
        "build.read_file(\"publish/stage/payload\")!=\"source bytes\" || "
        "build.file_stamp(\"publish/stage/payload\")!=stamp) { return 4; } "
        "if(build.publish_directory(\"publish/file\",\"publish/from-file\") || "
        "build.publish_directory(\"publish/source-link\",\"publish/"
        "from-link\") || "
        "build.publish_directory(\"publish/absent\",\"publish/from-missing\")) "
        "{ return 5; } "
        "if(build.file_kind(\"publish/from-file\")!=\"\" || "
        "build.file_kind(\"publish/from-link\")!=\"\" || "
        "build.file_kind(\"publish/from-missing\")!=\"\" || "
        "build.read_file(\"publish/file\")!=\"keep\" || "
        "build.file_kind(\"publish/source-link\")!=\"link\") { return 6; } "
        "if(!build.publish_directory(\"publish/stage\",\"publish/ready\") || "
        "build.file_kind(\"publish/stage\")!=\"\" || "
        "build.read_file(\"publish/ready/payload\")!=\"source bytes\" || "
        "build.file_stamp(\"publish/ready/payload\")!=stamp) { return 7; } "
        "return 0; }";
    static const char *const existing[] = { "file", "empty", "link", "dangling", "source-link" };
    for (int b = 0; b < 2; b++) {
        primitive_directory("publish");
        primitive_directory("publish/empty");
        primitive_file("publish/file", "keep", 4);
        primitive_link("file", "publish/link");
        primitive_link("absent", "publish/dangling");
        primitive_link("stage", "publish/source-link");
        struct stat before[5], after;
        char path[256];
        for (int i = 0; i < 5; i++) {
            snprintf(path, sizeof path, "%s/publish/%s", root, existing[i]);
            REQUIRE(!lstat(path, &before[i]));
        }
        behavior_backend backend = b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        REQUIRE(run_primitives(source, backend, 0));
        for (int i = 0; i < 5; i++) {
            snprintf(path, sizeof path, "%s/publish/%s", root, existing[i]);
            REQUIRE(!lstat(path, &after));
            REQUIRE(
                after.st_ino == before[i].st_ino && after.st_mode == before[i].st_mode
            );
        }
        snprintf(path, sizeof path, "%s/publish/ready/payload", root);
        char *text = tool_read(path);
        REQUIRE(text && !strcmp(text, "source bytes"));
        free(text);
        snprintf(path, sizeof path, "%s/publish/file", root);
        text = tool_read(path);
        REQUIRE(text && !strcmp(text, "keep"));
        free(text);
        snprintf(path, sizeof path, "%s/publish/empty", root);
        DIR *empty = opendir(path);
        REQUIRE(empty);
        int entries = 0;
        struct dirent *entry;
        while ((entry = readdir(empty))) {
            entries += strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..");
        }
        REQUIRE(!closedir(empty) && !entries);
        snprintf(path, sizeof path, "%s/publish", root);
        tool_remove_tree(path);
    }
    return true;
}

/* Fixed binary inputs cover padding/block boundaries and incremental input.
 * Expected digests are independently generated by Python hashlib/OpenSSL. */
TEST_CASE(sha256_streaming_preserves_padding_and_block_boundaries)
{
    static const struct
    {
        size_t size;
        const char *digest;
    } cases[] = {
        { 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
        { 1, "4a64a107f0cb32536e5bce6c98c393db21cca7f4ea187ba8c4dca8b51d4ea80a" },
        { 3, "2205790923f0b1097839d7f9802f21d284e1a7cdbed8b3334d9464a5c0eb1c26" },
        { 55, "80101b9d1281ccb3b9fb63729c2f80dab4ae7a56b55b954e9cc6451bf2825b52" },
        { 56, "0814299f4494565303090505b77ee5863c6672a94e4999fbbff8ec396d58bf55" },
        { 57, "c348d6554ce6b8351788cadbb793321152f120a203c6a2ba7202575d04bb7ce2" },
        { 63, "4bc2da88441f15a6ed90d3dacd20cc601703a419c7568a7dc7bf59d88c9a21fb" },
        { 64, "281202b0dcadec296df6e31b394a7604a0eaad74f1dd7bc127c944ba18c2fc83" },
        { 65, "f0a83190bd50aa46bb2740275e374e918b0ea3d8330fb8679bd88f1af8d761e7" },
        { 119, "133b3094c30acf42a9891b9355467be5df32568fa40de0375bdfe4538ed3c4af" },
        { 120, "3b74c1a8432965a33c0b38af1c3621ee9b2ff1a152639648f63f029dfe85302f" },
        { 121, "9d3344e68d45b53674275862a5d56287e2f0f8adb742987453d1eae6ea797f1c" },
        { 127, "26f60b9c6afc0d5a02c6abc9b17883b9b876979d9ef08fa9611c1dc4c6a15b3a" },
        { 128, "9a3cb848c8219e3d94ef5075dfedcbade55bcc5b9e016a3a7e2dc9d893befd92" },
        { 129, "9d10ceb3d0a8264cf9531f5462b258e898412d1268b89f0d9322bbeab2e1fcd6" },
        { 4095, "ae83015642bddda808fdff10deedf02fb2d6c46458edeb697cc52189e7e10034" },
        { 4096, "daf6bc4926a633c4077b0b74d935141a9e416aeb67b4550960495110ddc96f9f" },
        { 4097, "c44059639eeff4361fa228a700efab50210b7f95422a09d6a4344a488b2533b7" },
    };
    static const size_t chunks[] = { 1, 7, 55, 63, 64, 65, 127, 4096 };
    unsigned char bytes[4097];
    for (size_t i = 0; i < sizeof bytes; i++) {
        bytes[i] = (unsigned char)(i * 37 + 17);
    }
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        char digest[65];
        sha256_hex(bytes, cases[i].size, digest);
        REQUIRE(!strcmp(digest, cases[i].digest));
        for (size_t c = 0; c < sizeof chunks / sizeof *chunks; c++) {
            sha256 hash;
            sha256_begin(&hash);
            for (size_t at = 0; at < cases[i].size;) {
                size_t size = cases[i].size - at;
                if (size > chunks[c]) {
                    size = chunks[c];
                }
                sha256_add(&hash, bytes + at, size);
                sha256_add(&hash, bytes + at, 0);
                at += size;
            }
            sha256_end(&hash, digest);
            REQUIRE(!strcmp(digest, cases[i].digest));
        }
    }
    return true;
}

/* FIPS 180-4 example digests: "abc", the 448-bit two-block message and one
 * million 'a' bytes, which also spans many read chunks. */
TEST_CASE(private_hashes_match_sha256_vectors)
{
    primitive_file("hash-abc", "abc", 3);
    size_t size = 1000000;
    char *million = malloc(size);
    assert(million);
    memset(million, 'a', size);
    primitive_file("hash-million", million, size);
    free(million);
    static const char *source =
        "package hash\nBuild :: () -> int { "
        "abc:="
        "\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"; "
        "if(build.hash_file(\"hash-abc\")!=abc || "
        "build.hash_text(\"abc\")!=abc) { "
        "return 1; } "
        "if(build.hash_text(\"\")!="
        "\"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\") "
        "{ "
        "return 2; } "
        "if(build.hash_text("
        "\"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq\")!="
        "\"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1\") "
        "{ return 3; } "
        "if(build.hash_file(\"hash-million\")!="
        "\"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0\") "
        "{ return 4; } "
        "if(build.hash_file(\"hash-missing\")!=\"\") { return 5; } "
        "return 0; }";
    static const char *directory = "package hash\nBuild :: () -> int { "
                                   "build.hash_file(\".\"); return 0; }";
    for (int b = 0; b < 2; b++) {
        behavior_backend backend = b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        REQUIRE(run_primitives(source, backend, 0));
        REQUIRE(fails_with(directory, backend, "build file hash needs a regular file"));
    }
    return true;
}

TEST_CASE(private_environment_names_list_the_allowed_snapshot)
{
    REQUIRE(!setenv("CHIBI_BUILD_WORKER", worker_path, 1));
    REQUIRE(!setenv("CHIBI_BUILD_SECRET", "parent-only", 1));
    char *saved = getenv("CCACHE_TEMPDIR") ? strdup(getenv("CCACHE_TEMPDIR")) : NULL;
    REQUIRE(!setenv("CCACHE_TEMPDIR", "", 1));
    static const char *
        source = "package names\nBuild :: () -> int { "
                 "names:=build.join([\"\",build.join(build.environment_names(),"
                 "\"|"
                 "\"),\"\"],\"|\"); "
                 "if(build.before(names,\"|CHIBI_BUILD_WORKER|\")==names || "
                 "build.before(names,\"|CCACHE_TEMPDIR|\")==names) { return 1; "
                 "} "
                 "if(build.before(names,\"SECRET\")!=names || "
                 "build.environment(\"CCACHE_TEMPDIR\")!=\"\") { "
                 "return 2; } "
                 "return 0; }";
    bool passed = true;
    for (int b = 0; b < 2; b++) {
        passed &= run_primitives(source, b ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE, 0);
    }
    if (saved) {
        assert(!setenv("CCACHE_TEMPDIR", saved, 1));
    } else {
        assert(!unsetenv("CCACHE_TEMPDIR"));
    }
    free(saved);
    REQUIRE(passed);
    return true;
}

typedef enum
{
    BUILD_PROCESS_ALL,
    BUILD_PROCESS_FILES,
    BUILD_PROCESS_TIMINGS
} build_process_cases;

static void build_process(const char *worker, const char *host, build_process_cases cases)
{
    worker_path = worker;
    host_path = host;
    char error[256] = { 0 };
    /* The job's compiler cache, as the build host uses it: these cases time
     * host behaviour, not recovering a compiler for a stale seed. */
    const char *cache = getenv("CHIBI_COMPILER_CACHE");
    build_compiler = chibi_build_compiler_load(
        CHIBI_SOURCE_ROOT "/tools/compiler",
        CHIBI_SOURCE_ROOT "/packages/core",
        cache && *cache && strcmp(cache, "off") ? cache : NULL,
        error,
        sizeof error
    );
    if (!build_compiler) {
        fprintf(stderr, "build compiler: %s\n", error);
    }
    assert(build_compiler);

    strcpy(root, "/tmp/chibi-build-process-XXXXXX");
    assert(mkdtemp(root));
    char *resolved = realpath(root, NULL);
    assert(resolved && strlen(resolved) < sizeof root);
    strcpy(root, resolved);
    free(resolved);
    saved_worker = getenv("CHIBI_BUILD_WORKER")
                     ? strdup(getenv("CHIBI_BUILD_WORKER"))
                     : NULL;
    fixture_source = tool_read(CHIBI_SOURCE_ROOT "/test/language/BuildProcessFixture.chibi");
    assert(fixture_source);
    saved_secret = getenv("CHIBI_BUILD_SECRET")
                     ? strdup(getenv("CHIBI_BUILD_SECRET"))
                     : NULL;
    saved_allowed = getenv("CHIBI_BUILD_ALLOWED")
                      ? strdup(getenv("CHIBI_BUILD_ALLOWED"))
                      : NULL;
    if (cases == BUILD_PROCESS_TIMINGS) {
        assert(!setenv("CHIBI_BUILD_WORKER", worker_path, 1));
        RUN_TEST_CASE(build_timings_are_opt_in_bounded_and_exclusive);
        RUN_TEST_CASE(build_timings_classify_without_recording_arguments);
        RUN_TEST_CASE(steps_take_up_to_1023_arguments);
        RUN_TEST_CASE(private_started_steps_run_together);
        RUN_TEST_CASE(private_unwaited_and_late_steps_stop);
        RUN_TEST_CASE(build_timings_record_interrupted_children);
        goto teardown;
    }
    if (cases == BUILD_PROCESS_FILES) {
        RUN_TEST_CASE(private_core_bytes_survive_native_result_bank_reuse);
        RUN_TEST_CASE(private_programs_import_core_packages_and_format_text);
        RUN_TEST_CASE(private_compiled_programs_are_kept_and_reused);
        RUN_TEST_CASE(private_value_storage_reuses_results_and_preserves_live_values);
        RUN_TEST_CASE(private_directory_publication_refuses_existing_paths);
        goto teardown;
    }
    RUN_TEST_CASE(argv_cwd_and_explicit_environment);
    RUN_TEST_CASE(build_timings_are_opt_in_bounded_and_exclusive);
    RUN_TEST_CASE(build_timings_classify_without_recording_arguments);
    RUN_TEST_CASE(separate_output_bounds);
    RUN_TEST_CASE(exits_and_spawn_errors);
    RUN_TEST_CASE(timeout_and_active_cancellation);
    RUN_TEST_CASE(descendants_stop_on_timeout_and_leader_exit);
    RUN_TEST_CASE(authored_fixture_runs_real_children_on_each_backend);
    RUN_TEST_CASE(authored_capability_errors_do_not_become_exit_codes);
    RUN_TEST_CASE(original_workflow_preserves_order_and_failure_on_real_host);
    RUN_TEST_CASE(private_host_protocol_probe_precedes_source_and_budget_requirements);
    RUN_TEST_CASE(private_host_cli_executes_source_and_reports_errors);
    RUN_TEST_CASE(default_compiler_follows_relocated_read_only_sdk_and_refuses_corrupt_seed);
    RUN_TEST_CASE(private_host_cli_deadline_covers_changed_compiler_recovery);
    RUN_TEST_CASE(private_host_cli_sigint_covers_changed_compiler_recovery);
    RUN_TEST_CASE(private_host_cli_sigterm_covers_changed_compiler_recovery);
    RUN_TEST_CASE(private_host_cli_deadline_unblocks_inherited_signal_masks);
    RUN_TEST_CASE(private_host_cli_runtime_signals_still_clean_up_descendants);
    RUN_TEST_CASE(steps_take_up_to_1023_arguments);
    RUN_TEST_CASE(private_values_and_process_snapshots_survive_later_calls);
    RUN_TEST_CASE(private_string_utilities_preserve_delimiters_and_empty_values);
    RUN_TEST_CASE(private_sinks_receive_reports_and_step_output_as_they_happen);
    RUN_TEST_CASE(private_natives_resolve_only_under_the_build_namespace);
    RUN_TEST_CASE(private_core_bytes_survive_native_result_bank_reuse);
    RUN_TEST_CASE(private_programs_import_core_packages_and_format_text);
    RUN_TEST_CASE(private_programs_import_named_collections);
    RUN_TEST_CASE(private_programs_allocate_managed_pointers);
    RUN_TEST_CASE(private_embed_file_writes_cmake_byte_arrays);
    RUN_TEST_CASE(private_programs_report_syntax_errors_with_a_location);
    RUN_TEST_CASE(private_programs_reject_yield_and_unawaited_tasks);
    RUN_TEST_CASE(private_runtime_failures_name_their_cause);
    RUN_TEST_CASE(private_started_steps_run_together);
    RUN_TEST_CASE(private_waits_take_output_in_wait_order);
    RUN_TEST_CASE(private_waits_refuse_unknown_and_repeated_handles);
    RUN_TEST_CASE(private_waits_have_limits_and_steps_can_be_killed);
    RUN_TEST_CASE(private_unwaited_and_late_steps_stop);
    RUN_TEST_CASE(build_timings_record_interrupted_children);
    RUN_TEST_CASE(private_value_helpers_preserve_nul_and_validate_c_boundaries);
    RUN_TEST_CASE(private_last_output_preserves_bytes_and_refuses_truncation);
    RUN_TEST_CASE(private_file_reads_preserve_bytes_and_reject_invalid_files);
    RUN_TEST_CASE(private_atomic_writes_preserve_target_on_rename_failure);
    RUN_TEST_CASE(private_file_and_value_limits_accept_exact_text_and_reject_growth);
    RUN_TEST_CASE(private_value_storage_reuses_results_and_preserves_live_values);
    RUN_TEST_CASE(private_byte_lists_write_cmake_initializers);
    RUN_TEST_CASE(private_argument_bounds_reject_before_authored_execution);
    RUN_TEST_CASE(private_step_output_fills_one_text_value_and_probes_stay_unreported);
    RUN_TEST_CASE(private_metadata_reports_kinds_stamps_and_sorted_entries);
    RUN_TEST_CASE(private_copies_renames_and_removals_leave_whole_files);
    RUN_TEST_CASE(private_directory_publication_refuses_existing_paths);
    RUN_TEST_CASE(sha256_streaming_preserves_padding_and_block_boundaries);
    RUN_TEST_CASE(private_hashes_match_sha256_vectors);
    RUN_TEST_CASE(private_environment_names_list_the_allowed_snapshot);
teardown:
    chibi_build_compiler_free(build_compiler);
    build_compiler = NULL;
    tool_remove_tree(root);
    if (saved_worker) {
        assert(!setenv("CHIBI_BUILD_WORKER", saved_worker, 1));
    } else {
        assert(!unsetenv("CHIBI_BUILD_WORKER"));
    }
    free(saved_worker);
    free(fixture_source);
    if (saved_secret) {
        assert(!setenv("CHIBI_BUILD_SECRET", saved_secret, 1));
    } else {
        assert(!unsetenv("CHIBI_BUILD_SECRET"));
    }
    if (saved_allowed) {
        assert(!setenv("CHIBI_BUILD_ALLOWED", saved_allowed, 1));
    } else {
        assert(!unsetenv("CHIBI_BUILD_ALLOWED"));
    }
    free(saved_secret);
    free(saved_allowed);
}

void test_tool_build_process(const char *worker, const char *host)
{
    build_process(worker, host, BUILD_PROCESS_ALL);
}
void test_tool_build_process_files(const char *worker, const char *host)
{
    build_process(worker, host, BUILD_PROCESS_FILES);
}
void test_tool_build_timings(const char *worker, const char *host)
{
    build_process(worker, host, BUILD_PROCESS_TIMINGS);
}
