#ifndef CHIBI_TEST_PACKAGE_COMPILER_CHIBI_H
#define CHIBI_TEST_PACKAGE_COMPILER_CHIBI_H

#include "compiler_fixture.h"

bool package_compiler_authored_tests(
    const compiler_fixture *,
    const source_package_input *,
    int count,
    uintptr_t budget
);
/* Compile public source inputs with the embedded current compiler backend
 * (0 bytecode, 1 compiled).
 * Success returns malloc-owned closure bytes. Failure leaves NULL/zero output
 * and reports the authored diagnostic through the ordinary test output. */
bool package_compiler_closure(
    int compiler_backend,
    const source_package_input *,
    int count,
    uintptr_t budget,
    unsigned char **bytes,
    size_t *length
);
/* CompilerSources.txt must preserve the supported bootstrap producer's order. */
bool package_compiler_bootstrap_source_order(const char *inventory, size_t length);

void test_language_package_compiler_chibi();
void test_language_package_compiler_chibi_types();
void test_language_package_compiler_chibi_fallible();
void test_language_package_compiler_chibi_math_native();
void test_language_package_compiler_chibi_imports();
void test_language_package_compiler_chibi_compiled();
void test_language_package_compiler_chibi_types_compiled();
void test_language_package_compiler_chibi_collections();
void test_language_package_compiler_chibi_collections_compiled();
void test_language_package_compiler_chibi_imports_compiled();
void test_language_package_compiler_chibi_bootstrap();
void test_language_package_compiler_chibi_literals();
void test_language_package_compiler_chibi_behaviors();
void test_language_package_compiler_chibi_actions();
void test_language_package_compiler_chibi_behaviors_rest();
void test_language_package_compiler_chibi_queries();
void test_language_package_compiler_chibi_sharing();
void test_language_package_compiler_chibi_records();
void test_language_package_compiler_chibi_debug_free();
void test_language_package_compiler_chibi_scalar();
void test_language_package_compiler_chibi_native();
/* The child mode of the native test: calls into one built library. */
int test_language_package_compiler_chibi_native_call(int count, char **arguments);
/* The child mode of the stack overflow test: takes one fault as `mode` says. */
int test_language_stack_overflow_child(const char *mode);

void test_language_package_compiler_chibi_libraries();
void test_language_package_compiler_chibi_native_arguments();

#endif
