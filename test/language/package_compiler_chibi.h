#ifndef CHIBI_TEST_PACKAGE_COMPILER_CHIBI_H
#define CHIBI_TEST_PACKAGE_COMPILER_CHIBI_H

#include "compiler_fixture.h"

void test_language_package_compiler_chibi_scalar();
void test_language_package_compiler_chibi_native();
/* The child mode of the native test: calls into one built library. */
int test_language_package_compiler_chibi_native_call(int count, char **arguments);
/* The child mode of the stack overflow test: takes one fault as `mode` says. */
int test_language_stack_overflow_child(const char *mode);

#endif
