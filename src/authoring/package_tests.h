#ifndef CHIBI_AUTHORING_PACKAGE_TESTS_H
#define CHIBI_AUTHORING_PACKAGE_TESTS_H

#include "authoring/source_package_prepare.h"

/* Instructions chibi test lets one test run before it fails. */
#define PACKAGE_TEST_BUDGET 100000000u

/* One `@test` function of a prepared project package. Views are borrowed from
 * the packages they were found in. */
typedef struct
{
    const char *package; /* Its declared name, unique among the packages. */
    const char *name, *path;
    int line, column;
    const behavior_function *body;
} package_test;
typedef struct
{
    const char *path; /* Borrowed from the failing module; NULL when unknown. */
    int line, column;
    char message[256];
} package_test_failure;

/* The tests of the valid project: packages, ordered by file and position;
 * collection packages such as core: contribute none. An array to afree(). */
package_test *package_tests_find(const authoring_packages *);
/* Runs one test, prepared for backend, for at most budget instructions and
 * describes where a failed one stopped: at the innermost call, such as the
 * expect that failed. */
bool package_test_run(const package_test *, behavior_backend, uintptr_t budget, package_test_failure *);

#endif
