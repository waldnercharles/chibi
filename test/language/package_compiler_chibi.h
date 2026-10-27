#ifndef CHIBI_TEST_PACKAGE_COMPILER_CHIBI_H
#define CHIBI_TEST_PACKAGE_COMPILER_CHIBI_H

void test_language_package_compiler_chibi();
void test_language_package_compiler_chibi_types();
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
void test_language_package_compiler_chibi_debug_free();
void test_language_package_compiler_chibi_scalar();
void test_language_package_compiler_chibi_native();
/* The child mode of the native test: calls into one built library. */
int test_language_package_compiler_chibi_native_call(int count, char **arguments);
/* The child mode of the stack overflow test: takes one fault as `mode` says. */
int test_language_stack_overflow_child(const char *mode);

void test_language_package_compiler_chibi_libraries();
void test_language_package_compiler_chibi_native_arguments_oracle();
void test_language_package_compiler_chibi_native_arguments();

#endif
