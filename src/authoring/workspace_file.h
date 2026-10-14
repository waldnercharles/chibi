#ifndef CHIBI_WORKSPACE_FILE_H
#define CHIBI_WORKSPACE_FILE_H
#include <stdbool.h>
#include <stddef.h>
typedef struct workspace_files workspace_files;
enum
{
    WORKSPACE_FILE_BYTES_MAX = 16 * 1024 * 1024,
    WORKSPACE_FILE_PATH_MAX = 1024
};
/* Retains a root directory descriptor. Paths are relative filesystem
 * names without dot/parent/empty segments. Descendant links and special files
 * are refused. No current-directory dependency or parent-directory creation. */
workspace_files *workspace_files_new(const char *root, char *error, size_t);
void workspace_files_free(workspace_files *);
/* Publishes one fully written new regular file exclusively. Failure removes
 * its private temporary; existing destinations are never replaced. File data
 * are opaque bytes, bounded to 16 MiB. Caller owns data throughout the call. */
bool workspace_file_write_new(
    workspace_files *,
    const char *path,
    const void *data,
    size_t bytes,
    char *error,
    size_t
);
typedef struct
{
    const char *path;
    const void *data;
    size_t bytes;
} workspace_file_entry;
enum
{
    WORKSPACE_BUNDLE_FILES_MAX = 128,
    WORKSPACE_BUNDLE_BYTES_MAX = 64 * 1024 * 1024
};
/* Preflights all entries, then creates an exclusive relative directory under
 * an existing safe parent. Nested entry directories are created inside it.
 * Writes occur in caller order; put the completion manifest last. No existing
 * destination is replaced. On IO failure a partial new directory may remain;
 * false never means complete and retrying the same destination is refused. */
bool workspace_file_bundle_write_new(
    workspace_files *,
    const char *directory,
    const workspace_file_entry *,
    int count,
    char *error,
    size_t
);
/* Flat bundles are fully staged then atomically renamed to a new directory.
 * Refusal never publishes an incomplete destination; nested entries refuse. */
bool workspace_file_bundle_write_atomic(
    workspace_files *,
    const char *directory,
    const workspace_file_entry *,
    int count,
    char *error,
    size_t
);
#endif
