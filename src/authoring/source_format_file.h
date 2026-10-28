#ifndef CHIBI_AUTHORING_SOURCE_FORMAT_FILE_H
#define CHIBI_AUTHORING_SOURCE_FORMAT_FILE_H

#include "authoring/source_format.h"

/* Resolve .chibi-format from the source's directory upwards, stopping at the
 * nearest .git boundary or filesystem root. The nearest file replaces (never
 * merges with) parent settings; missing keys use defaults. An explicit config
 * bypasses discovery. No user-global config or environment settings are read.
 * Source may be a new buffer filename, but its parent directory must exist.
 * Windows uses the SDK support shim, including drive/UNC-root discovery. */
bool authoring_format_options_for_file(
    const char *source,
    const char *config,
    authoring_format_options *,
    char *error,
    size_t size
);
/* Format a buffer with the same file-based configuration as disk/CLI callers.
 * Does not read or write the source file. Returns malloc-owned text or NULL. */
char *authoring_format_file_text(
    const char *source,
    const char *text,
    const char *config,
    char *error,
    size_t size
);
/* Format a file or recursively all regular .chibi files below a directory.
 * Files are sorted; all are validated before any write. Descendant symlinks
 * are rejected, .git and .chibi build/cache directories are not traversed.
 * Limits: 8 MiB/file, 256 MiB aggregate, 65536 files, 64 directory levels.
 * Returns the number changed/would change, or -1 on failure. Check never writes.
 * The repository command separately handles generated and exact-byte fixtures. */
int authoring_format_path(const char *path, const char *config, bool check);

#endif
