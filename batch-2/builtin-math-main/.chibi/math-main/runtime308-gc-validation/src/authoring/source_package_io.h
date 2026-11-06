#ifndef CHIBI_AUTHORING_SOURCE_PACKAGE_IO_H
#define CHIBI_AUTHORING_SOURCE_PACKAGE_IO_H

#include <stdbool.h>
#include <stddef.h>

enum
{
    SOURCE_PACKAGE_MAX_FILES = 256
};
typedef struct
{
    const char *path, *text;
    size_t size; /* Exact text byte count, excluding an optional trailing NUL. */
} source_package_input;
typedef struct
{
    const char *name, *root;
} source_package_collection;
typedef struct
{
    const char *project_root;
    const source_package_input *files;
    int file_count;
    const source_package_collection *collections;
    int collection_count;
} source_package_snapshot_options;
typedef struct
{
    const char *project_root;
    const source_package_collection *collections;
    int collection_count;
} source_package_disk_options;
typedef struct
{
    char path[4096], message[192];
} source_package_load_error;

/* Collects owned, sorted source bytes and canonical roots, without parsing or
 * resolving imports. Roots must be absolute; root symlinks are canonicalized,
 * descendant symlinks rejected, hidden descendants skipped and overlapping
 * roots scanned once. Limits: 256 files, 512 KiB/file, 8 MiB total, 4096 path
 * bytes/visited entries and sixteen directory levels below the longest root.
 * Inputs must initially be empty. Release a successful result with release. */
bool source_package_files_load(
    const source_package_disk_options *,
    source_package_snapshot_options *inputs,
    source_package_load_error *
);
void source_package_files_release(source_package_snapshot_options *inputs);
/* The same bounded filesystem reader for one root, retaining only files.
 * Stores nothing on failure. Windows returns an unsupported error. */
bool source_package_files_read(const char *root, source_package_input **files, source_package_load_error *);
void source_package_files_free(source_package_input *files);

#endif
