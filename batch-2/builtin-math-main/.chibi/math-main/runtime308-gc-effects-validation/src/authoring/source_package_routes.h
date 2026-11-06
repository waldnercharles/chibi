#ifndef CHIBI_AUTHORING_SOURCE_PACKAGE_ROUTES_H
#define CHIBI_AUTHORING_SOURCE_PACKAGE_ROUTES_H

#include "authoring/source_package_snapshot.h"
#include "support/arena.h"

typedef struct
{
    const char *path, *directory, *key, *identity;
    int root;
} source_package_routed_file;

/* Private lexical resolver policy. Each function returns a stable error message
 * or NULL on success. Output strings belong to storage; no input is retained.
 * No filesystem, environment, source parsing or graph construction occurs. */

/* Options/count/pointer validation precedes this call in the snapshot owner.
 * roots has collection_count+1 entries. On failure root_count retains the
 * successfully normalized prefix, including the implicit project root when
 * that root was valid. */
const char *source_package_routes_roots(
    arena *storage,
    const char *project_root,
    const source_package_collection *collections,
    int collection_count,
    source_package_collection *roots,
    int *root_count
);
/* roots contains at least the canonical implicit project root. Snapshot callers
 * also perform the path guard before source byte-budget checks; retaining it
 * here gives standalone callers the same invalid-path diagnostic. */
const char *source_package_routes_file(
    arena *storage,
    const source_package_collection *roots,
    int root_count,
    const char *input_path,
    source_package_routed_file *out
);
/* directory is the routed declaring file's canonical directory; file_root
 * indexes roots. literal/size describe the complete quoted JSON token. Decode
 * precedes collection lookup, path checks and containment, in that order.
 * out_directory is reset to NULL on every call, including failures. */
const char *source_package_routes_import(
    arena *storage,
    const source_package_collection *roots,
    int root_count,
    const char *directory,
    int file_root,
    const char *literal,
    size_t size,
    char **out_directory
);
#endif
