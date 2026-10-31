#ifndef CHIBI_AUTHORING_PACKAGE_COMPILE_H
#define CHIBI_AUTHORING_PACKAGE_COMPILE_H

#include "authoring/source_package_io.h"
#include "execution/backend.h"
#include "runtime/behaviors/package.h"
#include "runtime/native_capabilities.h"

typedef struct
{
    char path[4096]; /* Empty when the failure has no source file. */
    int line, column;
    char message[256];
} package_compile_error;

/* Compiles a root package and the packages it imports on the packaged authored
 * compiler, for native's catalog, and decodes the closure for backend. The
 * root package is that of `first_path`, a virtual file with the text
 * `first_text` that precedes the inputs, or, when both are NULL, of the first
 * input file directly in the project root. The returned packages are named as
 * `project:DIRECTORY` and `core:NAME`; `project:` is the project root's own
 * package. NULL with the reason in `error`. */
package_collection *package_compile(
    const source_package_snapshot_options *inputs,
    const char *first_path,
    const char *first_text,
    const native_capabilities *native,
    behavior_backend backend,
    package_compile_error *error
);

/* Whether the package in `directory`, or a package it imports, declares
 * `foreign import`. Such a package is native only: the VM never runs foreign
 * code and cannot prepare it, so `chibi test` skips it. */
bool package_compile_needs_native(const source_package_snapshot_options *inputs, const char *directory);

/* The path of the source a compiled closure names `label`: `project:REL` is
 * REL below the project root, and `NAME:REL` below the root of the collection
 * NAME. Any other label is copied as it is. `out` holds `size` bytes. */
void package_compile_source_path(
    const char *project_root,
    const source_package_collection *collections,
    int collection_count,
    const char *label,
    char *out,
    size_t size
);

#endif
