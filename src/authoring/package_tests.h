#ifndef CHIBI_AUTHORING_PACKAGE_TESTS_H
#define CHIBI_AUTHORING_PACKAGE_TESTS_H

#include "runtime/behaviors/package.h"
#include "runtime/native_definitions.h"

typedef struct task_system task_system;

/* Instructions chibi test lets one test run before it fails. */
#define PACKAGE_TEST_BUDGET 100000000u

/* One `@test` function of a prepared package. Views are borrowed from
 * the packages they were found in. */
typedef struct
{
    const char *package; /* Its declared name, unique among the packages. */
    const char *name, *path;
    int line, column;
    const behavior_function *body;
    /* Inferred deterministic closure with no external effects. */
    bool parallel;
} package_test;
/* The run-time failures a test can expect with core:testing's ExpectFailure,
 * in the order of its Failure union's variants. */
typedef enum
{
    PACKAGE_TEST_FAILURE_NONE,
    PACKAGE_TEST_FAILURE_INDEX_OUT_OF_RANGE,
    PACKAGE_TEST_FAILURE_MAP_KEY_ABSENT,
    PACKAGE_TEST_FAILURE_VIEW_OUT_OF_RANGE,
    PACKAGE_TEST_FAILURE_ASSERTION_FAILED,
    PACKAGE_TEST_FAILURE_BUDGET_EXHAUSTED,
    PACKAGE_TEST_FAILURE_DEPTH_EXCEEDED,
    PACKAGE_TEST_FAILURE_COUNT
} package_test_failure_kind;
typedef struct
{
    const char *path; /* Borrowed from the failing module; NULL when unknown. */
    int line, column;
    char message[256];
    /* What the test said it ends with, or NONE. */
    package_test_failure_kind expected;
} package_test_failure;
/* core:testing's natives: test_expect_failure(kind), which records the kind
 * of failure the running test must end with. */
extern const native_definition_pack TESTING_DEFINITION_PACK;
typedef struct
{
    bool passed;
    package_test_failure failure;
} package_test_result;

/* The tests of the packages of the project: collection compiled by the authored
 * compiler, ordered by package, file and position. A package exports each test
 * as "@test NAME". An array to afree(). */
package_test *package_tests_find_compiled(const package_collection *);
/* Runs one test, prepared for backend, for at most budget instructions and
 * describes where a failed one stopped: at the innermost call, such as the
 * expect that failed. */
bool package_test_run(const package_test *, behavior_backend, uintptr_t budget, package_test_failure *);
/* Runs all tests exactly once with independent invocation storage. An optional
 * borrowed executor must be in an open submission scope on its controlling
 * thread. Impure tests run alone, between completed parallel spans. Results
 * keep input order; no worker prints. A caller's execution scope forces serial
 * execution so its shared budget and cancellation remain authoritative. */
void package_tests_run(
    const package_test *,
    int count,
    behavior_backend,
    uintptr_t budget,
    const task_system *,
    package_test_result *
);

#endif
