#include "authoring/package_tests.h"

#include "authoring/native_bindings.h"
#include "backend.h"
#include "package_core_source.h"
#include "runtime/core_definition.h"
#include "runtime/text_definition.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <string.h>

/* Two cases. A project package at the root, one of two files and two
 * collection packages; each test runs once per backend under a
 * 100000-instruction budget, which one exhausts. No filesystem, processes or
 * frames. */
enum
{
    BUDGET = 100000,
    TESTS = 8
};
static native_capabilities *table;
static authoring_packages *packages;
static package_test *tests;

static const char
    FACTS[] = "package facts\n"
              "import strings \"core:strings\"\n"
              "Check :: fn(amount: int) {\n"
              "    expect(amount > 2, \"amount is large\");\n"
              "}\n"
              "@test\n"
              "Passes :: fn() {\n"
              "    expect(strings.ByteLength(\"abc\") == 3, \"three bytes\");\n"
              "}\n"
              "@test\n"
              "Fails :: fn() {\n"
              "    Check(1);\n"
              "}\n"
              "@test\n"
              "Faults :: fn() {\n"
              "    values: int[] = [1];\n"
              "    expect(values[3] == 1, \"unreachable\");\n"
              "}\n"
              "@test\n"
              "Spins :: fn() {\n"
              "    while (true) {\n"
              "    }\n"
              "}\n"
              "@test\n"
              "Yields :: fn() {\n"
              "    yield\n"
              "}\n"
              "@test\n"
              "Starts :: fn() {\n"
              "    Yields();\n"
              "}\n";
static const char FIRST[] = "package facts\n@test\nFirst :: fn() {\n}\n";
/* The package at the root itself is named by its declaration. */
static const char TOP[] = "package top\n@test\nTop :: fn() {\n}\n";
/* A collection's tests belong to the collection, not to this project. */
static const char MARKS[] = "package marks\n@test\nCollected :: fn() {\n}\n";

static void cleanup()
{
    afree(tests);
    tests = NULL;
    authoring_packages_free(packages);
    packages = NULL;
    native_capabilities_free(table);
    table = NULL;
}
static bool prepare(behavior_backend backend)
{
    cleanup();
    native_definition_pack packs[] = { CORE_DEFINITION_PACK,
                                       NATIVE_TEXT_DEFINITION_PACK };
    char error[192];
    table = native_capabilities_from_definition_packs(NULL, 0, packs, 2, error, sizeof error);
    REQUIRE(table);
    source_package_input files[] = {
        { "/sdk/core/strings/Strings.chibi", CORE_STRINGS_SOURCE, strlen(CORE_STRINGS_SOURCE) },
        { "/sdk/core/marks/Marks.chibi", MARKS, strlen(MARKS) },
        { "facts/Facts.chibi", FACTS, strlen(FACTS) },
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
    REQUIRE(authoring_packages_valid(packages));
    tests = package_tests_find(packages);
    REQUIRE(acount(tests) == TESTS);
    return true;
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
 * the expect that failed, an out-of-range read or the exhausted budget. A
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
        REQUIRE(!strcmp(failure.message, "Expectation failed: amount is large"));
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
    }
    return true;
}

void test_language_package_tests()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_tests_find_the_project_tests_in_order);
    RUN_TEST_CASE(package_tests_report_where_each_failure_stopped);
    pu_clear_setup();
}
