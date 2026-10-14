#ifndef CHIBI_PROJECT_MIGRATION_H
#define CHIBI_PROJECT_MIGRATION_H
#include <stdbool.h>
#include <stddef.h>
/* Read-only bounded manifest probe. Does not load assets or migrate. Unsupported
 * versions are returned as versions; the caller decides presentation policy. */
bool project_migration_version(const char *root, int *version, char *error, size_t);
/* Copies source to Parent/Name exclusively, then applies canonical migrations
 * only inside that copy. Refuses external content/scene paths, links, special
 * files, changing reads, >4096 entries, depth>16, >16MiB/file, >64MiB total.
 * Root .git and all .chibi directories are omitted (repository/generated state).
 * Paths<=1024 UTF-8 bytes; retained reports contain SHA256 of sorted JSON
 * [{path,bytes,sha256}] file rows before/after, excluding generated directories.
 * On success owns *result; on failure removes only its own exclusive copy.
 * Existing source and destination bytes are never replaced. */
bool project_migration_copy(
    const char *source,
    const char *parent,
    const char *name,
    char **result,
    char *error,
    size_t
);
#endif
