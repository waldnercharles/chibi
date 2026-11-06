#ifndef CHIBI_AUTHORING_SOURCE_PACKAGE_SNAPSHOT_H
#define CHIBI_AUTHORING_SOURCE_PACKAGE_SNAPSHOT_H

#include "authoring/source_packages.h"
#include "language/semantic.h"

typedef struct
{
    script_diagnostic diagnostic; /* range.document is the input file, or -1. */
    script_range related;         /* document is -1 when absent. */
    source_package_error graph; /* Retains import indices for graph/cycle errors. */
} source_package_snapshot_error;
typedef struct source_package_snapshot source_package_snapshot;

/* Copies all retained inputs; callers may release them after construction.
 * Normal invalid input produces an inspectable invalid owner, not NULL. Valid
 * owners expose parsed documents and a resolved graph. Invalid owners may expose
 * only the prefix retained before failure; their graph is NULL.
 * Roots are explicit absolute, slash-separated paths. File paths may be absolute
 * or project-relative. Normalization is lexical: this module never reads disk,
 * resolves symlinks, consults the environment or proves physical containment.
 * A filesystem adapter must supply physically canonical roots/files if needed.
 * Imports use relative paths or explicit collection:path forms. Relative imports
 * cannot leave the declaring file's longest matching collection root.
 * Names and normalized root paths are unique, including the implicit project
 * collection. Longest root ownership also determines stable document/package
 * keys, independent of absolute installation paths and input order.
 * Limits match source_catalog: 256 files, 512 KiB per file, 8 MiB total source,
 * 4096 path bytes and sixteen directory levels below a selected root. Up to 255
 * explicit collections and 4096 imports are retained. */
source_package_snapshot *source_package_snapshot_new(const source_package_snapshot_options *);
void source_package_snapshot_free(source_package_snapshot *);
const source_package_snapshot_error *source_package_snapshot_error_at(const source_package_snapshot *);
const source_packages *source_package_snapshot_graph(const source_package_snapshot *);
const script_package_document *source_package_snapshot_documents(
    const source_package_snapshot *,
    int *count
);
const source_package_import *source_package_snapshot_imports(
    const source_package_snapshot *,
    int *count
);
/* Existing graph package indices; root identity is collection:, otherwise
 * collection:relative/directory. Returns NULL for an invalid owner/index. */
const char *source_package_snapshot_identity(const source_package_snapshot *, int package);

#endif
