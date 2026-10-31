#ifndef CHIBI_TEST_SCALAR_HOST_H
#define CHIBI_TEST_SCALAR_HOST_H

#include "authoring/source_package_io.h"
#include "support/arena.h"
#include "types/value.h"

#include <stdbool.h>
#include <stddef.h>
#include <yyjson.h>

/* Test-only access to the authored scalar pipeline on the recovered compiler:
 * package_compiler's CollectScalar produces scalar_facts rows, and the consumer
 * packages (scalar_llvm, llvm_native, native_abi and the scalar_cases corpus)
 * run as one closure the compiler compiles behind a small root package.
 * Rows cross as a scalar_facts.Row array of records the caller can read and
 * edit in place. Everything is JSON on the wire; nothing here prepares a
 * package with stage zero. */

/* Loads the compiler from tools/compiler and its seed. The consumer closure is
 * compiled on first use. */
bool scalar_host_open();
void scalar_host_close();

/* The rows CollectScalar gives `source` at `path`, allocated in storage. */
bool scalar_host_collect(const char *path, const char *source, arena *storage, pin_value *rows);
/* The same for CollectScalarPackages: the first function of the first input
 * is the entry and the other inputs are the packages it imports. */
bool scalar_host_collect_packages(const source_package_input *, int count, arena *storage, pin_value *rows);
/* The same with `collection` (such as "core") rooted at `collection_path`; the
 * collection's own files are among the inputs, under that path. */
bool scalar_host_collect_collection(
    const char *collection,
    const char *collection_path,
    const source_package_input *,
    int count,
    arena *storage,
    pin_value *rows
);

/* scalar_llvm.Validate: the empty string when the rows are valid, otherwise
 * the reason. Malloc-owned. */
bool scalar_host_validate(const pin_value *rows, char **message);
/* The LLVM IR scalar_llvm emits for native_abi's target `target` ("LinuxX64",
 * "MacOSArm64" or "WindowsX64"), empty when it rejects the rows. Malloc-owned. */
bool scalar_host_emit(const char *target, const pin_value *rows, char **ir);

/* The scalar_cases corpus: its case count, a case's name or source, its
 * expected value, and the value and verdict of a case's rows. */
bool scalar_host_case_count(int *count);
bool scalar_host_case_text(bool source, int index, char **text);
bool scalar_host_case_expected(int index, int *value);
bool scalar_host_case_value(const pin_value *rows, int64_t *value);
bool scalar_host_case_check(const pin_value *rows, int *verdict);

/* One call of an entry of the consumer root, whose arguments are the JSON
 * array in `arguments`; the result is the parsed array of results. */
bool scalar_host_consumer(const char *entry, yyjson_mut_doc *arguments, yyjson_doc **result);

#endif
