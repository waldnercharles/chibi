#ifndef CHIBI_AUTHORING_SOURCE_PACKAGES_H
#define CHIBI_AUTHORING_SOURCE_PACKAGES_H

/* Resolved package graph, constructed by the owning source_package_snapshot.
 * Direct callers supply complete parsed membership and canonical resolved
 * directories after collection/root validation. This module
 * never reads disk, parses text, resolves paths, or starts authored code.
 * All input arrays and strings remain immutable and alive until free. Returned
 * package indices are snapshot-local; the canonical directory is the identity. */
#include "authoring/source_package_io.h"
#include "language/syntax.h"

typedef struct
{
    const char *path, *directory, *name;
    script_range header;
} source_package_file;
typedef struct
{
    int file;
    const char *directory;
    const char *alias; /* NULL: resolved directory basename; "_": no binding. */
    script_range range;
} source_package_import;
typedef struct
{
    const char *directory, *name;
    const int *files; /* Input file indices, alphabetical filename order. */
    int file_count;
} source_package;
typedef enum
{
    SOURCE_PACKAGE_OK,
    SOURCE_PACKAGE_DUPLICATE_FILE,
    SOURCE_PACKAGE_HEADER_MISMATCH,
    SOURCE_PACKAGE_DUPLICATE_NAME,
    SOURCE_PACKAGE_MISSING,
    SOURCE_PACKAGE_IMPORT_NAME,
    SOURCE_PACKAGE_DUPLICATE_IMPORT,
    SOURCE_PACKAGE_CYCLE,
} source_package_code;
typedef struct
{
    source_package_code code;
    int file, import, related_file, related_import; /* -1 when absent. */
    int cycle[SOURCE_PACKAGE_MAX_FILES]; /* Input import indices, in cycle order. */
    int cycle_count;
} source_package_error;
typedef struct source_packages source_packages;

/* Parsed identifiers and canonical paths are preconditions. File count is
 * bounded by source_catalog; import count/text by its existing byte budget.
 * A failed construction publishes no graph. The error points into the supplied
 * descriptors so the catalog retains both source locations without flattening. */
source_packages *source_packages_new(
    const source_package_file *,
    int file_count,
    const source_package_import *,
    int import_count,
    source_package_error *
);
void source_packages_free(source_packages *);
int source_packages_count(const source_packages *);
const source_package *source_packages_at(const source_packages *, int package);
int source_packages_file(const source_packages *, int file);
/* Dependency-first package indices; canonical directory breaks independent
 * ties. Self-imports remain edges/bindings but are not inter-package ordering
 * constraints. This implements the approved dependency-correct contract, not
 * the pinned Odin priority queue's incidental mixed-self ordering. */
int source_packages_order(const source_packages *, int ordinal);
/* File-local named import lookup. Blank imports never create a binding. */
int source_packages_lookup(const source_packages *, int file, const char *alias);
/* First input import establishing an edge, or -1. Includes direct self edges. */
int source_packages_dependency(const source_packages *, int from, int to);

#endif
