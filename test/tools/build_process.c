#include "authoring/source_prepare.h"
#include "host/build_host.h"
#ifdef CHIBI_TEST_NATIVE_RUNTIME
#include "host/native_default.h"
#endif
#include "host/process.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static char root[128];
static const char *worker_path;
static const char *host_path;
static char *saved_secret, *saved_allowed, *saved_worker;
static char *fixture_source;
static char output[1024], errors[1024];
static chibi_process_output out, err;
static volatile sig_atomic_t cancelled;

static double now()
{
    return chibi_build_now();
}

static double deadline_after(double seconds)
{
    double deadline = now() + seconds;
    const char *inherited = getenv("CHIBI_TEST_DEADLINE");
    if (inherited) {
        char *end;
        double shared = strtod(inherited, &end);
        assert(end != inherited && !*end && shared > 0);
        if (shared < deadline) {
            deadline = shared;
        }
    }
    return deadline;
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
    const char *envp[] = { "PATH=/usr/bin:/bin",
                           "CHIBI_BUILD_ALLOWED=child-only",
                           "CHIBI_TEST_DEADLINE=123.5",
                           NULL };
    chibi_process_capture_result result = capture(argv, envp, 2);
    REQUIRE(result.error == CHIBI_PROCESS_OK);
    REQUIRE(result.status == 23);
    char expected[1024];
    snprintf(expected, sizeof expected, "cwd=%s\nallowed=child-only\nsecret=\ndeadline=123.5\narg=0:\narg=23:space \"quote\";$literal*\n", root);
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
    const char *argv[] = { worker_path, "build-output", "unused", NULL };
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
        const char *deadline = getenv("CHIBI_TEST_DEADLINE");
        snprintf(
            expected,
            sizeof expected,
            "cwd=%s\nallowed=\nsecret=\ndeadline=%s\narg=0:\narg=23:space "
            "\"quote\";$literal*\n",
            root,
            deadline ? deadline : ""
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
        "library Failure { Build :: fn() -> int { return "
        "RunBuildStep([\"/chibi/nonexistent/worker\"]); } }",
        "library Failure { Build :: fn() -> int { return RunBuildStep([\"\"]); "
        "} }",
        "library Failure { Build :: fn() -> int { return "
        "RunBuildStep([BuildEnvironment(\"CHIBI_BUILD_WORKER\"), \"sleep\", "
        "\"3\"]); } }",
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

TEST_CASE(ordinary_catalog_rejects_private_process_capabilities)
{
#ifdef CHIBI_TEST_NATIVE_RUNTIME
    void test_native_catalog_enable();
    test_native_catalog_enable();
    char error[256];
    native_capabilities *game = host_native_default_catalog_new(NULL, 0, NULL, 0, error, sizeof error);
    REQUIRE(game);
    const script_catalog *catalog = native_capabilities_catalog(game);
    bool absent = script_catalog_find_capability(catalog, "RunBuildStep", SCRIPT_CAPABILITY_FUNCTION) <
                      0 &&
                  script_catalog_find_capability(catalog, "BuildEnvironment", SCRIPT_CAPABILITY_FUNCTION) <
                      0;
#else
    const script_catalog empty = { 0 };
    const script_catalog *catalog = &empty;
    bool absent = true;
#endif
    const script_lower_bindings bindings = { 0 };
    authoring_source *source = authoring_source_prepare(
        "BuildProcessFixture.chibi",
        fixture_source,
        catalog,
        &bindings,
        BEHAVIOR_BYTECODE
    );
    bool rejected = source && !source->valid && source->diagnostic_count > 0;
    authoring_source_free(source);
#ifdef CHIBI_TEST_NATIVE_RUNTIME
    native_capabilities_free(game);
#endif
    REQUIRE(absent && rejected);
    return true;
}

TEST_CASE(private_host_cli_executes_source_and_reports_errors)
{
    char source[256], log[256];
    snprintf(source, sizeof source, "%s/BuildProcessFixture.chibi", root);
    snprintf(log, sizeof log, "%s/cli.log", root);
    tool_write(source, fixture_source);
    const char *argv[] = {
        host_path, "--root",       root,           "--source", source,
        "--entry", "BuildFixture", "--timeout-ms", "1000",     NULL,
    };
    REQUIRE(tool_run(argv, root, log) == 23);
    char *text = tool_read(log);
    bool emitted = text && strstr(text, "arg=0:\n") && strstr(text, "worker-stderr");
    free(text);
    REQUIRE(emitted);
    const char *missing[] = {
        host_path, "--root", root,           "--source", source,
        "--entry", "Absent", "--timeout-ms", "1000",     NULL,
    };
    REQUIRE(tool_run(missing, root, log) != 0);
    text = tool_read(log);
    bool diagnostic = text && strstr(text, "entry");
    free(text);
    REQUIRE(diagnostic);
    return true;
}

void test_tool_build_process(const char *worker, const char *host)
{
    worker_path = worker;
    host_path = host;
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
    RUN_TEST_CASE(argv_cwd_and_explicit_environment);
    RUN_TEST_CASE(separate_output_bounds);
    RUN_TEST_CASE(exits_and_spawn_errors);
    RUN_TEST_CASE(timeout_and_active_cancellation);
    RUN_TEST_CASE(descendants_stop_on_timeout_and_leader_exit);
    RUN_TEST_CASE(authored_fixture_runs_real_children_on_each_backend);
    RUN_TEST_CASE(authored_capability_errors_do_not_become_exit_codes);
    RUN_TEST_CASE(original_workflow_preserves_order_and_failure_on_real_host);
    RUN_TEST_CASE(ordinary_catalog_rejects_private_process_capabilities);
    RUN_TEST_CASE(private_host_cli_executes_source_and_reports_errors);
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
