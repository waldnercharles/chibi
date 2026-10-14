#ifndef CHIBI_WORKSPACE_RESOURCES_H
#define CHIBI_WORKSPACE_RESOURCES_H
#include "authoring/reference_index.h"
#include "support/arena.h"
#include "support/asset_value.h"
#include "types/array.h"

typedef struct workspace_resources workspace_resources;
typedef enum
{
    WORKSPACE_RESOURCE_OK,
    WORKSPACE_RESOURCE_INVALID_QUERY,
    WORKSPACE_RESOURCE_IO,
    WORKSPACE_RESOURCE_LIMIT,
    WORKSPACE_RESOURCE_UNSAFE_PATH,
    WORKSPACE_RESOURCE_NOT_FOUND,
    WORKSPACE_RESOURCE_UNAVAILABLE,
} workspace_resource_code;
typedef struct
{
    bool ok;
    int code;
    const char *message;
    const char *status; /* Stable named code for authored error handling. */
    int revision, total;
    bool complete;
} workspace_resource_result;
typedef struct
{
    const char *key, *kind, *name, *path, *status, *message;
    const char *reference; /* Canonical content/... file key, independent of configured folder. */
    double bytes;
    int width, height, vertices, triangles;
    const char *metadata;
    bool unresolved;
} workspace_resource_row;
enum
{
    WORKSPACE_RESOURCE_PAGE_MAX = 256,
    WORKSPACE_RESOURCE_ROWS_MAX = 4096,
    WORKSPACE_RESOURCE_QUERY_MAX = 256,
};
/* Host-owned, thread-confined read-only disk snapshot. Root is canonicalized
 * once; each descendant is opened relative to retained directory descriptors
 * with O_NOFOLLOW. Never opens mutable document buffers or writes project files.
 * New scans respect configured project.kdl content; missing manifest uses content.
 * No GPU calls or resident catalog mutation. Initial scan is explicit.
 * C result messages borrow service storage until refresh/free; script callbacks
 * copy them into activation storage. Query row snapshots own all fields there. */
workspace_resources *workspace_resources_new(const char *root, char *error, size_t size);
void workspace_resources_free(workspace_resources *);
/* Sorted snapshot, at most 4096 visited directory entries, depth 16, 16 MiB/file
 * and 64 MiB total. Source text inspection is limited to 512 KiB/file. Symlinks,
 * nonregular entries and incomplete reads make complete=false. Partial scans
 * remain queryable with explicit typed status. Revision changes only if
 * snapshot identity or scan status changes. Query never performs file IO. */
workspace_resource_result workspace_resources_refresh(workspace_resources *);
workspace_resource_result workspace_resources_query(
    const workspace_resources *,
    int first,
    int count,
    const char *kind,
    const char *query,
    arena *,
    pin_array **rows
);
workspace_resource_result workspace_resources_lookup(
    const workspace_resources *,
    const char *key,
    arena *,
    workspace_resource_row *row
);
/* Optional host-provided authoring annotations. Copies all retained values;
 * callers may release the catalog/index immediately. No hidden disk scan.
 * Resource catalog keys are content/... even for configured content folders.
 * Annotations apply to this snapshot; refresh discards them. */
void workspace_resources_catalog(workspace_resources *, const av_doc *);
void workspace_resources_references(workspace_resources *, const reference_index *);
#endif
