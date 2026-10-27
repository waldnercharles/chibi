#include "authoring/package_tests.h"

#include "authoring/native_bindings.h"
#include "backend.h"
#include "package_core_source.h"
#include "runtime/arrays_definition.h"
#include "runtime/core_definition.h"
#include "runtime/math_definition.h"
#include "runtime/tasks.h"
#ifdef CHIBI_PACKAGE_TEST_POOL
#include "runtime/conversions.h"
#include "runtime/task_pool.h"
#include "types/text.h"
#endif
#include "runtime/text_definition.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/value_format.h"

#include <assert.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

/* Project-root and multi-file packages plus collection imports exercise
 * discovery, isolated results and worker barriers under a 100000-instruction
 * budget. The native overlap fixture has a five-second rendezvous deadline.
 * No filesystem, processes or frames. */
enum
{
    BUDGET = 100000,
    TESTS = 9,
    DIAGNOSTIC_LIMIT = 4
};
static native_capabilities *table;
static authoring_packages *packages;
static package_test *tests;

/* No declared effects is not proof that an external callback is pure. */
static const char *opaque(void *, const pin_value *, pin_value *out)
{
    out[0].b = true;
    return NULL;
}
static const native_parameter_definition OPAQUE_RESULT[] = {
    { .name = "result", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_BOOL, .type_name = "bool" }
};
static const native_capability_definition OPAQUE_DEFINITION[] = {
    { .canonical_name = "Test Opaque",
      .source_name = "opaque",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .completion_output = -1,
      .outputs = OPAQUE_RESULT,
      .output_count = 1 },
};
static const node_host_call OPAQUE_CALL = { .invoke = opaque };
static const native_definition_binding OPAQUE_BINDING[] = {
    { .canonical_name = "Test Opaque", .implementation = { .call = &OPAQUE_CALL } },
};
static const native_definition_pack OPAQUE_PACK = {
    .definitions = OPAQUE_DEFINITION,
    .definition_count = 1,
    .bindings = OPAQUE_BINDING,
    .binding_count = 1,
};

#ifdef CHIBI_PACKAGE_TEST_POOL
/* Instrumentation is confined to these fixture natives. It uses atomics, not
 * pico assertions on a worker, and a bounded rendezvous rather than a speed
 * comparison to prove that two authored invocations really overlap. */
static atomic_int active, peak, started, arrived, formatted, checked, completed,
    exclusive_calls;
static int expected_workers;
static double rendezvous_deadline;

static double now()
{
    struct timespec time;
    return timespec_get(&time, TIME_UTC) == TIME_UTC
             ? (double)time.tv_sec + (double)time.tv_nsec / 1e9
             : rendezvous_deadline;
}
static const char *rendezvous(void *, const pin_value *, pin_value *out)
{
    int simultaneous = atomic_fetch_add(&active, 1) + 1;
    int maximum = atomic_load(&peak);
    while (maximum < simultaneous &&
           !atomic_compare_exchange_weak(&peak, &maximum, simultaneous)) {
    }
    int ordinal = atomic_fetch_add(&started, 1) + 1;
    pin_value value = { .type = PIN_I32, .i = ordinal };
    const char *text = pin_value_to_string(&value);
    char expected[32];
    snprintf(expected, sizeof expected, "%d", ordinal);
    atomic_fetch_add(&arrived, 1);
    int pair = (ordinal + 1) / 2 * 2;
    while (expected_workers > 1 && atomic_load(&arrived) < pair &&
           now() < rendezvous_deadline) {
    }
    out[0].b = expected_workers == 1 || atomic_load(&arrived) >= pair;
    if (expected_workers > 1 && !(ordinal % 2)) {
        /* Replace every display-ring slot on this worker before its peer
         * reads its retained text. A process-global ring fails deterministically. */
        for (int i = 0; i < 8; i++) {
            pin_value_to_string(&value);
        }
        atomic_store(&formatted, pair);
        while (atomic_load(&checked) < pair && now() < rendezvous_deadline) {
        }
        out[0].b &= atomic_load(&checked) >= pair;
    }
    if (expected_workers > 1 && ordinal % 2) {
        while (atomic_load(&formatted) < pair && now() < rendezvous_deadline) {
        }
        out[0].b &= atomic_load(&formatted) >= pair && !strcmp(text, expected);
        atomic_store(&checked, pair);
    }
    arena storage = { 0 };
    for (int i = 0; i < 64; i++) {
        pin_value converted = native_conversion_To_String(&value, 1, &storage);
        pin_string copied = pin_text(converted);
        out[0].b &= copied.length == strlen(expected) &&
                    !memcmp(copied.bytes, expected, copied.length);
    }
    arena_free(&storage);
    atomic_fetch_sub(&active, 1);
    atomic_fetch_add(&completed, 1);
    return NULL;
}
static const char *exclusive(void *, const pin_value *, pin_value *out)
{
    out[0].b = atomic_load(&active) == 0 && atomic_load(&completed) == 2;
    atomic_fetch_add(&exclusive_calls, 1);
    return NULL;
}
static const native_parameter_definition PROBE_RESULT[] = {
    { .name = "result", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_BOOL, .type_name = "bool" }
};
static const native_capability_definition PROBE_DEFINITIONS[] = {
    { .canonical_name = "Test Rendezvous",
      .source_name = "rendezvous",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .completion_output = -1,
      .outputs = PROBE_RESULT,
      .output_count = 1,
      .pure = true },
    { .canonical_name = "Test Exclusive",
      .source_name = "exclusive",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .completion_output = -1,
      .outputs = PROBE_RESULT,
      .output_count = 1,
      .effects = SCRIPT_EFFECT_HOST },
};
static const node_host_call RENDEZVOUS_CALL = { .invoke = rendezvous };
static const node_host_call EXCLUSIVE_CALL = { .invoke = exclusive };
static const node_access PROBE_PURE = { .flags = NODE_EFFECT_PURE };
static const node_access PROBE_HOST = { .flags = NODE_EFFECT_HOST_SINK };
static const native_definition_binding PROBE_BINDINGS[] = {
    { .canonical_name = "Test Rendezvous",
      .implementation = { .call = &RENDEZVOUS_CALL, .access = &PROBE_PURE } },
    { .canonical_name = "Test Exclusive",
      .implementation = { .call = &EXCLUSIVE_CALL, .access = &PROBE_HOST } },
};
static const native_definition_pack PROBE_PACK = {
    .definitions = PROBE_DEFINITIONS,
    .definition_count = 2,
    .bindings = PROBE_BINDINGS,
    .binding_count = 2,
};
#endif

static const char
    FACTS[] = "package facts\n"
              "import strings \"core:strings\"\n"
              "Check :: (amount: int) {\n"
              "    assert(amount > 2, \"amount is large\");\n"
              "}\n"
              "@test\n"
              "Passes :: () {\n"
              "    assert(strings.ByteLength(\"abc\") == 3, \"three bytes\");\n"
              "}\n"
              "@test\n"
              "Fails :: () {\n"
              "    Check(1);\n"
              "}\n"
              "@test\n"
              "Faults :: () {\n"
              "    values: int[] = [1];\n"
              "    assert(values[3] == 1, \"unreachable\");\n"
              "}\n"
              "@test\n"
              "Spins :: () {\n"
              "    while (true) {\n"
              "    }\n"
              "}\n"
              "@test\n"
              "Yields :: () {\n"
              "    yield\n"
              "}\n"
              "@test\n"
              "Starts :: () {\n"
              "    Yields();\n"
              "}\n"
              "@test\n"
              "Unsure :: () {\n"
              "    assert(strings.ByteLength(\"ab\") == 3);\n"
              "}\n";
static const char FIRST[] = "package facts\n@test\nFirst :: () {\n}\n";
/* The package at the root itself is named by its declaration. */
static const char TOP[] = "package top\n@test\nTop :: () {\n}\n";
/* A collection's tests belong to the collection, not to this project. */
static const char MARKS[] = "package marks\n@test\nCollected :: () {\n}\n"
                            "pub Draw :: () -> number { return rand(); }\n"
                            "pub Opaque :: () -> bool { return opaque(); }\n";

static void cleanup()
{
    afree(tests);
    tests = NULL;
    authoring_packages_free(packages);
    packages = NULL;
    native_capabilities_free(table);
    table = NULL;
}
static bool prepare_text(behavior_backend backend, const char *source, int expected)
{
    cleanup();
    native_definition_pack packs[] = {
        CORE_DEFINITION_PACK,
        NATIVE_TEXT_DEFINITION_PACK,
        NATIVE_ARRAY_DEFINITION_PACK,
        NATIVE_MATH_DEFINITION_PACK,
        OPAQUE_PACK,
#ifdef CHIBI_PACKAGE_TEST_POOL
        PROBE_PACK,
#endif
    };
    char error[192];
    int pack_count = (int)(sizeof packs / sizeof *packs);
    table = native_capabilities_from_definition_packs(NULL, 0, packs, pack_count, error, sizeof error);
    REQUIRE(table);
    source_package_input files[] = {
        { "/sdk/core/strings/Strings.chibi", CORE_STRINGS_SOURCE, strlen(CORE_STRINGS_SOURCE) },
        { "/sdk/core/marks/Marks.chibi", MARKS, strlen(MARKS) },
        { "facts/Facts.chibi", source, strlen(source) },
        { "facts/A.chibi", FIRST, strlen(FIRST) },
        { "Root.chibi", TOP, strlen(TOP) },
    };
    source_package_collection collections[] = { { "core", "/sdk/core" } };
    source_package_snapshot_options options = { .project_root = "/project",
                                                .files = files,
                                                .file_count = 5,
                                                .collections = collections,
                                                .collection_count = 1 };
    script_lower_bindings bindings = authoring_native_bindings(table);
    packages = authoring_packages_prepare(
        source_package_snapshot_new(&options),
        native_capabilities_catalog(table),
        &bindings,
        backend,
        NULL
    );
    if (!authoring_packages_valid(packages)) {
        const char *error = authoring_packages_error(packages);
        if (error) {
            fprintf(stderr, "package tests: %.256s\n", error);
        }
        int shown = 0;
        for (int i = 0;
             i < authoring_packages_count(packages) && shown < DIAGNOSTIC_LIMIT;
             i++) {
            const authoring_package *package = authoring_packages_at(packages, i);
            for (int d = 0; d < package->diagnostic_count && shown < DIAGNOSTIC_LIMIT;
                 d++, shown++) {
                const script_diagnostic *diagnostic = &package->diagnostics[d];
                fprintf(
                    stderr,
                    "%.128s:%d:%d: %s\n",
                    package->identity,
                    diagnostic->range.line,
                    diagnostic->range.column,
                    diagnostic->message
                );
            }
        }
    }
    REQUIRE(authoring_packages_valid(packages));
    tests = package_tests_find(packages);
    REQUIRE(acount(tests) == expected);
    return true;
}

static bool prepare(behavior_backend backend)
{
    return prepare_text(backend, FACTS, TESTS);
}

/* The project's tests, by file and then position, with their declarations. */
TEST_CASE(package_tests_find_the_project_tests_in_order)
{
    REQUIRE(prepare(BEHAVIOR_BYTECODE));
    const struct
    {
        const char *package, *name, *path;
        int line;
    } expected[TESTS] = {
        { "top", "Top", "/project/Root.chibi", 3 },
        { "facts", "First", "/project/facts/A.chibi", 3 },
        { "facts", "Passes", "/project/facts/Facts.chibi", 7 },
        { "facts", "Fails", "/project/facts/Facts.chibi", 11 },
        { "facts", "Faults", "/project/facts/Facts.chibi", 15 },
        { "facts", "Spins", "/project/facts/Facts.chibi", 20 },
        { "facts", "Yields", "/project/facts/Facts.chibi", 25 },
        { "facts", "Starts", "/project/facts/Facts.chibi", 29 },
        { "facts", "Unsure", "/project/facts/Facts.chibi", 33 },
    };
    for (int i = 0; i < TESTS; i++) {
        REQUIRE(!strcmp(tests[i].package, expected[i].package));
        REQUIRE(!strcmp(tests[i].name, expected[i].name));
        REQUIRE(!strcmp(tests[i].path, expected[i].path));
        REQUIRE(tests[i].line == expected[i].line && tests[i].column == 1);
    }
    return true;
}

/* A test passes when it returns; otherwise the innermost failure explains it:
 * the assert that failed, an out-of-range read or the exhausted budget. A
 * test runs alone, so one that waits or starts a task fails. */
TEST_CASE(package_tests_report_where_each_failure_stopped)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(backends[b]));
        package_test_failure failure;
        for (int i = 0; i < 3; i++) {
            REQUIRE(package_test_run(&tests[i], backends[b], BUDGET, &failure));
            REQUIRE(!failure.message[0]);
        }

        REQUIRE(!package_test_run(&tests[3], backends[b], BUDGET, &failure));
        REQUIRE(!strcmp(failure.message, "Assertion failed: amount is large"));
        REQUIRE(failure.path && !strcmp(failure.path, "/project/facts/Facts.chibi"));
        REQUIRE(failure.line == 4 && failure.column == 5);

        REQUIRE(!package_test_run(&tests[4], backends[b], BUDGET, &failure));
        REQUIRE(!strcmp(failure.message, "array index out of range"));
        REQUIRE(failure.line == 17 && failure.column == 12);

        REQUIRE(!package_test_run(&tests[5], backends[b], BUDGET, &failure));
        REQUIRE(!strcmp(failure.message, "instruction budget exhausted"));
        REQUIRE(failure.line == 21 && failure.column == 18);

        REQUIRE(!package_test_run(&tests[6], backends[b], BUDGET, &failure));
        REQUIRE(!strcmp(failure.message, "a test must finish without waiting"));
        REQUIRE(failure.line == 26 && failure.column == 5);
        REQUIRE(!package_test_run(&tests[7], backends[b], BUDGET, &failure));
        REQUIRE(!strcmp(failure.message, "a test cannot start a task"));

        /* Without a message, the condition's text from its own file. */
        REQUIRE(!package_test_run(&tests[8], backends[b], BUDGET, &failure));
        REQUIRE(!strcmp(failure.message, "Assertion failed: strings.ByteLength(\"ab\") == 3"));
        REQUIRE(failure.line == 34 && failure.column == 5);
    }
    return true;
}

/* A delayed executor finishes in reverse order. It exercises result slots and
 * join-before-return without depending on thread scheduling. */
typedef struct
{
    task_range *run;
    void *context;
    int count, submissions, finishes;
    bool pending;
} reverse_executor;

static void *enqueue_reverse(task_range *run, int count, int minimum, void *context, void *user)
{
    reverse_executor *executor = user;
    assert(!executor->pending && minimum == 1);
    (void)minimum;
    executor->run = run;
    executor->context = context;
    executor->count = count;
    executor->submissions++;
    executor->pending = true;
    return executor;
}
static void finish_reverse(void *task, void *user)
{
    reverse_executor *executor = user;
    assert(task == executor && executor->pending);
    (void)task;
    for (int i = executor->count; i-- > 0;) {
        executor->run(i, i + 1, (uint32_t)(i % 2), executor->context);
    }
    executor->pending = false;
    executor->finishes++;
}

static bool cancelled(void *)
{
    return true;
}

TEST_CASE(package_tests_keep_results_and_failures_in_discovery_order)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(backends[b]));
        package_test_result serial[TESTS], parallel[TESTS];
        package_tests_run(tests, TESTS, backends[b], BUDGET, NULL, serial);
        reverse_executor executor = { 0 };
        task_system tasks = { 2, &executor, enqueue_reverse, finish_reverse };
        package_tests_run(tests, TESTS, backends[b], BUDGET, &tasks, parallel);
        REQUIRE(executor.submissions == 1 && executor.finishes == 1 && !executor.pending);
        for (int i = 0; i < TESTS; i++) {
            REQUIRE(serial[i].passed == parallel[i].passed);
            REQUIRE(serial[i].failure.path == parallel[i].failure.path);
            REQUIRE(serial[i].failure.line == parallel[i].failure.line);
            REQUIRE(serial[i].failure.column == parallel[i].failure.column);
            REQUIRE(!strcmp(serial[i].failure.message, parallel[i].failure.message));
        }
        /* A serial executor, no tests, and an enclosing cancellation/budget
         * scope must never enqueue work. */
        executor = (reverse_executor){ 0 };
        tasks.workers = 1;
        package_tests_run(tests, TESTS, backends[b], BUDGET, &tasks, parallel);
        REQUIRE(executor.submissions == 0);
        tasks.workers = 2;
        package_tests_run(NULL, 0, backends[b], BUDGET, &tasks, NULL);
        behavior_execution_scope scope = { .remaining = BUDGET };
        behavior_execution_scope_begin(&scope);
        package_tests_run(tests, 3, backends[b], BUDGET, &tasks, parallel);
        behavior_execution_scope_end(&scope);
        REQUIRE(executor.submissions == 0);
        REQUIRE(parallel[0].passed && parallel[1].passed && parallel[2].passed);
        scope = (behavior_execution_scope){ 0 };
        behavior_execution_scope_begin(&scope);
        package_tests_run(tests, 3, backends[b], BUDGET, &tasks, parallel);
        behavior_execution_scope_end(&scope);
        REQUIRE(executor.submissions == 0);
        REQUIRE(!parallel[2].passed);
        REQUIRE(!strcmp(parallel[2].failure.message, "shared instruction budget exhausted"));
        scope = (behavior_execution_scope){ .remaining = BUDGET, .cancelled = cancelled };
        behavior_execution_scope_begin(&scope);
        package_tests_run(tests, 3, backends[b], BUDGET, &tasks, parallel);
        behavior_execution_scope_end(&scope);
        REQUIRE(executor.submissions == 0);
        REQUIRE(!parallel[2].passed);
        REQUIRE(!strcmp(parallel[2].failure.message, "execution cancelled"));
    }
    return true;
}

TEST_CASE(package_tests_infer_serial_effects_across_calls_and_imports)
{
    const char
        source[] = "package facts\nimport marks \"core:marks\"\n"
                   "Draw :: () -> number { return rand(); }\n"
                   "@test\nDirect :: () { assert(rand() >= 0); }\n"
                   "@test\nIndirect :: () { assert(Draw() >= 0); }\n"
                   "@test\nImported :: () { assert(marks.Draw() >= 0); }\n"
                   "@test\nLogs :: () { print_string(\"quiet\"); }\n"
                   "@test\nLocal :: () { values: int[] = [1]; values[0] = 2; "
                   "assert(values[0] == 2); }\n"
                   "Helper :: () -> bool { return opaque(); }\n"
                   "@test\nUnknown :: () { assert(opaque()); }\n"
                   "@test\nUnknownHelper :: () { assert(Helper()); }\n"
                   "@test\nUnknownImport :: () { assert(marks.Opaque()); }\n"
                   "Depth :: (n: int) -> int { if (n == 0) { return 0; } "
                   "return Depth(n - 1) + 1; }\n"
                   "@test\nRecursive :: () { assert(Depth(3) == 3); }\n";
    REQUIRE(prepare_text(BEHAVIOR_BYTECODE, source, 11));
    REQUIRE(tests[0].parallel && tests[1].parallel);
    for (int i = 2; i < 6; i++) {
        REQUIRE(!tests[i].parallel);
    }
    REQUIRE(tests[6].parallel);
    for (int i = 7; i < 10; i++) {
        REQUIRE(!tests[i].parallel);
    }
    REQUIRE(tests[10].parallel);
    return true;
}

/* Each run allocates managed pointers in a heap of its own. Identity tests
 * that only compare pointers are inferred parallel and overlap with a pure
 * test; tests that dereference or write through a pointer are inferred serial
 * and run alone. Each mix runs serially, on a two-worker executor and on the
 * task pool. */
TEST_CASE(package_tests_allocate_managed_pointers)
{
    enum
    {
        POINTER_TESTS = 5
    };
    const char
        source[] = "package facts\n"
                   "Link :: struct { value: int; next: ^Link?; }\n"
                   "Bump :: (p: ^int) { p^ += 1; }\n"
                   "@test\nAlias :: () { x: int = 1; p := &x; Bump(p); "
                   "p^ += 1; assert(x == 3); }\n"
                   "@test\nPure :: () { assert(1 + 2 == 3); }\n"
                   "@test\nSame :: () { x := 1; p := &x; assert(p == p); }\n"
                   "@test\nDistinct :: () { x := 1; y := 2; p := &x; q := &y; "
                   "assert(p != q); }\n"
                   "@test\nCycle :: () { a: Link = {value: 1, next: nil}; "
                   "b: Link = {value: 2, next: nil}; p := &a; q := &b; "
                   "p.next = q; q.next = p; "
                   "assert(p.value * 10 + q.value == 12); }\n";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        /* Root.chibi and facts/A.chibi each add one test before these. */
        REQUIRE(prepare_text(backends[b], source, POINTER_TESTS + 2));
        REQUIRE(!tests[2].parallel && !tests[6].parallel);
        REQUIRE(tests[3].parallel && tests[4].parallel && tests[5].parallel);
        package_test_result results[POINTER_TESTS];
        for (int run = 0; run < 3; run++) {
            reverse_executor executor = { 0 };
            task_system tasks = { 2, &executor, enqueue_reverse, finish_reverse };
#ifdef CHIBI_PACKAGE_TEST_POOL
            task_pool *pool = run == 2 ? task_pool_acquire_workers(2) : NULL;
            if (pool) {
                task_pool_begin(pool, true);
            }
            const task_system *system = run == 0 ? NULL
                                      : pool     ? task_pool_tasks(pool)
                                                 : &tasks;
#else
            const task_system *system = run == 0 ? NULL : &tasks;
#endif
            package_tests_run(tests + 2, POINTER_TESTS, backends[b], BUDGET, system, results);
#ifdef CHIBI_PACKAGE_TEST_POOL
            if (pool) {
                task_pool_end(pool);
                task_pool_release(pool);
            }
#endif
            for (int i = 0; i < POINTER_TESTS; i++) {
                if (!results[i].passed) {
                    fprintf(
                        stderr,
                        "managed pointer test %d: %s\n",
                        i,
                        results[i].failure.message
                    );
                }
                REQUIRE(results[i].passed);
            }
        }
    }
    return true;
}

#ifdef CHIBI_PACKAGE_TEST_POOL
TEST_CASE(package_tests_overlap_with_private_storage_and_exclusive_barriers)
{
    const char source[] = "package facts\nimport strings \"core:strings\"\n"
                          "Check :: (n: int, expected: string) {\n"
                          "    values: int[] = [n, 2];\n"
                          "    assert(values[0] == n);\n"
                          "    values[0] = n + 1;\n"
                          "    assert(rendezvous());\n"
                          "    assert(values[0] == n + 1 && values[1] == 2);\n"
                          "    assert(strings.ByteLength(\"private\") == 7);\n"
                          "    for (i in 0..64) {\n"
                          "        assert(format_text(\"{}:{}\", n, true) == "
                          "expected);\n"
                          "    }\n"
                          "}\n"
                          "@test\nA :: () { Check(11, \"11:true\"); }\n"
                          "@test\nB :: () { Check(22, \"22:true\"); }\n"
                          "@test\nBarrier :: () { assert(exclusive()); }\n"
                          "@test\nC :: () { Check(33, \"33:true\"); }\n"
                          "@test\nD :: () { Check(44, \"44:true\"); }\n";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare_text(backends[b], source, 7));
        REQUIRE(tests[2].parallel && tests[3].parallel);
        REQUIRE(!tests[4].parallel);
        REQUIRE(tests[5].parallel && tests[6].parallel);
        /* Exercise the automatic default as well as explicit serial/two-worker budgets. */
        for (int requested = 0; requested <= 2; requested++) {
            task_pool *pool = task_pool_acquire_workers(requested);
            expected_workers = task_pool_tasks(pool)->workers > 1 ? 2 : 1;
            atomic_store(&active, 0);
            atomic_store(&peak, 0);
            atomic_store(&started, 0);
            atomic_store(&arrived, 0);
            atomic_store(&formatted, 0);
            atomic_store(&checked, 0);
            atomic_store(&completed, 0);
            atomic_store(&exclusive_calls, 0);
            rendezvous_deadline = now() + 5;
            package_test_result results[5];
            task_pool_begin(pool, true);
            package_tests_run(tests + 2, 5, backends[b], BUDGET, task_pool_tasks(pool), results);
            task_pool_end(pool);
            task_pool_release(pool);
            for (int i = 0; i < 5; i++) {
                REQUIRE(results[i].passed);
            }
            REQUIRE(atomic_load(&peak) == expected_workers);
            REQUIRE(atomic_load(&arrived) == 4 && atomic_load(&completed) == 4);
            REQUIRE(atomic_load(&exclusive_calls) == 1);
            REQUIRE(atomic_load(&active) == 0);
        }
    }
    return true;
}
#endif

void test_language_package_tests()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_tests_find_the_project_tests_in_order);
    RUN_TEST_CASE(package_tests_report_where_each_failure_stopped);
    RUN_TEST_CASE(package_tests_keep_results_and_failures_in_discovery_order);
    RUN_TEST_CASE(package_tests_infer_serial_effects_across_calls_and_imports);
    RUN_TEST_CASE(package_tests_allocate_managed_pointers);
#ifdef CHIBI_PACKAGE_TEST_POOL
    RUN_TEST_CASE(package_tests_overlap_with_private_storage_and_exclusive_barriers);
#endif
    pu_clear_setup();
}
