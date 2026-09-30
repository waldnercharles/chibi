#ifndef CHIBI_TEST_REPORT_H
#define CHIBI_TEST_REPORT_H

#include <stdbool.h>

/* Test drivers stay quiet on success. Failures always print; passing checks
 * and progress notes print only when VERBOSE is set in the environment. */
bool test_verbose();
/* Prints like printf, only when verbose. */
void test_note(const char *format, ...) __attribute__((format(printf, 1, 2)));
/* Counts one check and prints it when it fails or when verbose. */
void test_check(int ok, const char *message);
int test_failures();
/* Prints "<name>: N checks passed" or the failure count, when any check ran. */
void test_summary(const char *name);

#endif
