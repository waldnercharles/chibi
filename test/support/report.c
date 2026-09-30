#include "test/support/report.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static int passed, failed;

bool test_verbose()
{
    static int verbose = -1;
    if (verbose < 0) {
        const char *value = getenv("VERBOSE");
        verbose = value && *value;
    }
    return verbose;
}

void test_note(const char *format, ...)
{
    if (!test_verbose()) {
        return;
    }
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void test_check(int ok, const char *message)
{
    if (!ok) {
        failed++;
        printf("FAIL: %s\n", message);
        return;
    }
    passed++;
    test_note("PASS: %s\n", message);
}

int test_failures()
{
    return failed;
}

void test_summary(const char *name)
{
    if (failed) {
        printf("%s: %d of %d checks FAILED\n", name, failed, passed + failed);
        return;
    }
    if (passed) {
        printf("%s: %d checks passed\n", name, passed);
    }
}
