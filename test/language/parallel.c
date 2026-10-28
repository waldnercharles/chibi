#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "test/parallel.h"

#include "third_party/pico_headers/pico_unit.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static atomic_int active, peak, started, setups, teardowns, dirty;
static atomic_bool second_finished, reversed;
static _Thread_local int fixture;
static _Thread_local int clock_calls;
static double seconds();

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
TEST_CASE(broken_output)
{
    REQUIRE(freopen("/dev/null", "r", pu_output()));
    fputs("This diagnostic must not disappear silently", pu_output());
    return true;
}

/* Run first in the standalone selector, before the parent creates threads.
 * Only the child owns the deliberately broken case stream; no process stdout
 * redirection or production injection hook is needed. */
TEST_CASE(parallel_stream_errors_fail_closed)
{
    pid_t child = fork();
    REQUIRE(child >= 0);
    if (!child) {
        struct rlimit no_core = { 0, 0 };
        if (setrlimit(RLIMIT_CORE, &no_core) ||
            setenv("CHIBI_TEST_THREADS", "1", 1) || !test_parallel_init()) {
            _Exit(3);
        }
        test_parallel_begin();
        RUN_TEST_CASE(broken_output);
        test_parallel_end();
        _Exit(0);
    }
    int status = 0;
    double deadline = seconds() + 5;
    pid_t waited;
    for (;;) {
        waited = waitpid(child, &status, WNOHANG);
        if (waited == child || (waited < 0 && errno != EINTR)) {
            break;
        }
        if (seconds() >= deadline) {
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
            REQUIRE(false);
        }
        struct timespec pause = { .tv_nsec = 1000000 };
        nanosleep(&pause, NULL);
    }
    REQUIRE(waited == child);
    REQUIRE(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    return true;
}
#endif

static double seconds()
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) {
        abort();
    }
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

static void setup()
{
    if (fixture) {
        atomic_fetch_add(&dirty, 1);
    }
    fixture = 1;
    atomic_fetch_add(&setups, 1);
}

static void other_setup()
{
    setup();
    fixture = 2;
}

static void teardown()
{
    fixture = 0;
    atomic_fetch_add(&teardowns, 1);
}

TEST_CASE(overlapping_case)
{
    int live = atomic_fetch_add(&active, 1) + 1;
    int high = atomic_load(&peak);
    while (high < live && !atomic_compare_exchange_weak(&peak, &high, live)) {
    }
    atomic_fetch_add(&started, 1);
    double deadline = seconds() + 5;
    while (test_parallel_workers() > 1 && atomic_load(&started) < 2 &&
           seconds() < deadline) {
    }
    bool overlapped = test_parallel_workers() == 1 || atomic_load(&started) >= 2;
    atomic_fetch_sub(&active, 1);
    REQUIRE(overlapped && fixture == 1 && !pu_test_failed());
    return true;
}

TEST_CASE(first_failure)
{
    double deadline = seconds() + 5;
    while (test_parallel_workers() > 1 && !atomic_load(&second_finished) &&
           seconds() < deadline) {
    }
    atomic_store(&reversed, test_parallel_workers() == 1 || atomic_load(&second_finished));
    return pu_require(
        false,
        fixture == 2 ? "first expected failure" : "wrong first fixture",
        __FILE__,
        __LINE__,
        NULL
    );
}

TEST_CASE(second_failure)
{
    atomic_store(&second_finished, true);
    return pu_require(
        false,
        fixture == 1 ? "second expected failure" : "wrong second fixture",
        __FILE__,
        __LINE__,
        NULL
    );
}

static bool scenario()
{
    test_parallel_begin();
    pu_setup(setup, teardown);
    /* Cross the bounded queue boundary to test multiple flushes, worker reuse
     * and case-local hooks even when registration changes them before flush. */
    for (int i = 0; i < 66; i++) {
        RUN_TEST_CASE(overlapping_case);
    }
    pu_setup(other_setup, teardown);
    RUN_TEST_CASE(first_failure);
    pu_setup(setup, teardown);
    RUN_TEST_CASE(second_failure);
    pu_clear_setup();
    test_parallel_end();
    return true;
}

static char *read_log(FILE *file)
{
    if (fflush(file) || fseek(file, 0, SEEK_END)) {
        abort();
    }
    long size = ftell(file);
    if (size < 0 || size > 32768) {
        abort();
    }
    char *text = malloc((size_t)size + 1);
    if (!text) {
        abort();
    }
    rewind(file);
    if (fread(text, 1, (size_t)size, file) != (size_t)size) {
        abort();
    }
    text[size] = 0;
    return text;
}

TEST_CASE(parallel_cases_isolate_hooks_counts_and_order)
{
    atomic_store(&active, 0);
    atomic_store(&peak, 0);
    atomic_store(&started, 0);
    atomic_store(&setups, 0);
    atomic_store(&teardowns, 0);
    atomic_store(&dirty, 0);
    atomic_store(&second_finished, false);
    atomic_store(&reversed, false);
    pu_test_context context = pu_capture_context();
    context.setup = context.teardown = NULL;
    context.time = false;
    context.quiet = false;
    context.output = tmpfile();
    context.errors = tmpfile();
    if (!context.output || !context.errors) {
        abort();
    }
    pu_run_test_context(&context, "scenario", scenario);
    char *output = read_log(context.output), *errors = read_log(context.errors);
    fclose(context.output);
    fclose(context.errors);
    const char *first = strstr(errors, "first expected failure");
    const char *second = strstr(errors, "second expected failure");
    bool ordered = first && second && first < second;
    const char *first_case = strstr(output, "Running: first_failure");
    const char *second_case = strstr(output, "Running: second_failure");
    ordered &= first_case && second_case && first_case < second_case;
    free(output);
    free(errors);
    REQUIRE(ordered);
    REQUIRE(atomic_load(&reversed));
    REQUIRE(context.passed == 67 && context.failed == 2 && context.asserts == 68);
    REQUIRE(atomic_load(&setups) == 68 && atomic_load(&teardowns) == 68);
    REQUIRE(atomic_load(&dirty) == 0 && atomic_load(&active) == 0 && fixture == 0);
    REQUIRE(atomic_load(&peak) <= test_parallel_workers());
    REQUIRE(test_parallel_workers() == 1 || atomic_load(&peak) > 1);
    REQUIRE(!pu_test_failed());
    return true;
}

TEST_CASE(changes_display)
{
    REQUIRE(!pu_test_failed());
    REQUIRE(pu_is_quiet());
    pu_display_quiet(!pu_is_quiet());
    return true;
}

static bool display_scenario()
{
    test_parallel_begin();
    RUN_TEST_CASE(changes_display);
    RUN_TEST_CASE(changes_display);
    test_parallel_end();
    return true;
}

TEST_CASE(parallel_display_settings_are_case_local)
{
    pu_test_context context = pu_capture_context();
    context.setup = context.teardown = NULL;
    context.time = false;
    context.quiet = true;
    context.output = tmpfile();
    context.errors = tmpfile();
    if (!context.output || !context.errors) {
        abort();
    }
    pu_run_test_context(&context, "display", display_scenario);
    fclose(context.output);
    fclose(context.errors);
    REQUIRE(context.quiet);
    REQUIRE(context.passed == 3 && context.failed == 0 && context.asserts == 4);
    return true;
}

static double fixture_clock()
{
    return (double)++clock_calls;
}

static bool clock_case()
{
    return true;
}

TEST_CASE(parallel_passive_clock_uses_the_captured_clock)
{
    pu_test_context context = pu_capture_context();
    context.setup = context.teardown = NULL;
    context.time = true;
    context.quiet = false;
    context.seconds = fixture_clock;
    context.output = tmpfile();
    if (!context.output) {
        abort();
    }
    clock_calls = 0;
    pu_run_test_context(&context, "clock", clock_case);
    char *output = read_log(context.output);
    fclose(context.output);
    bool elapsed = strstr(output, "(1.000000 secs)") != NULL;
    free(output);
    REQUIRE(clock_calls == 2 && elapsed);
    REQUIRE(context.passed == 1 && context.failed == 0);
    return true;
}

void test_language_parallel()
{
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    RUN_TEST_CASE(parallel_stream_errors_fail_closed);
#endif
    RUN_TEST_CASE(parallel_cases_isolate_hooks_counts_and_order);
    RUN_TEST_CASE(parallel_display_settings_are_case_local);
    RUN_TEST_CASE(parallel_passive_clock_uses_the_captured_clock);
}
