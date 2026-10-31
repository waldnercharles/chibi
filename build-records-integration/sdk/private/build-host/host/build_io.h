#ifndef CHIBI_HOST_BUILD_IO_H
#define CHIBI_HOST_BUILD_IO_H

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>

/* Private build-host file operations. Root is an explicit absolute directory;
 * absolute paths are also accepted. Returned strings are caller-owned. No
 * operation changes the process working directory. */
char *build_io_path(const char *root, const char *path);
char *build_io_resolve(const char *root, const char *path);
bool build_io_exists(const char *root, const char *path);
bool build_io_mkdir(const char *root, const char *path);

/* Reserve one new directory, requiring its parent to exist. Any existing path,
 * including a link, fails. Publish atomically renames a prepared directory and
 * refuses every existing destination. No fallback replaces a destination. */
bool build_io_reserve_directory(const char *root, const char *path);
bool build_io_publish_directory(const char *root, const char *source, const char *destination);

/* Read at most 16 MiB of regular-file bytes. A missing file returns an owned
 * empty string; all other failures return false with a static capability
 * diagnostic. Deadline uses chibi_process_now(); cancelled may be NULL. */
bool build_io_read(
    const char *root,
    const char *path,
    double deadline,
    const volatile sig_atomic_t *cancelled,
    char **out,
    size_t *length,
    const char **error
);

/* Atomically replace bytes through a temporary file in the destination directory.
 * Data is limited to 16 MiB. Failed writes remove the temporary file. */
bool build_io_write(
    const char *root,
    const char *path,
    const char *text,
    size_t size,
    double deadline,
    const volatile sig_atomic_t *cancelled
);

/* Metadata. Kind reads the path itself, not a link's target: "" when nothing
 * is there, else "file", "directory", "link" or "other". A stamp follows
 * links and changes with the file's device, inode, size, mode, modification
 * or change time; it is "" when nothing is there. Its stable device:inode:
 * prefix identifies the followed object independently of mutable metadata.
 * Other failures return a static capability diagnostic. */
#define BUILD_IO_STAMP_SIZE 160
const char *build_io_kind(const char *root, const char *path, const char **kind);
const char *build_io_stamp(const char *root, const char *path, char stamp[BUILD_IO_STAMP_SIZE]);

/* A directory's entry names except . and .., sorted by byte, as an owned
 * support/array.h array of owned strings. A missing directory lists no names.
 * Fails beyond 8,192 names or 16 MiB of names. */
const char *build_io_list(
    const char *root,
    const char *path,
    double deadline,
    const volatile sig_atomic_t *cancelled,
    char ***names
);

/* Copy replaces destination atomically with a regular source file's bytes
 * and permission bits, through a temporary file it removes on failure.
 * Rename is rename(2). Remove deletes a file, link or empty directory and
 * succeeds when nothing is there. */
bool build_io_copy(
    const char *root,
    const char *source,
    const char *destination,
    double deadline,
    const volatile sig_atomic_t *cancelled
);
bool build_io_rename(const char *root, const char *source, const char *destination);
bool build_io_remove(const char *root, const char *path);

/* The SHA-256 of a regular file's bytes as lowercase hex, or "" when nothing
 * is there. */
const char *build_io_hash(
    const char *root,
    const char *path,
    double deadline,
    const volatile sig_atomic_t *cancelled,
    char digest[65]
);

/* A regular file's bytes as `0xHH,` for each, the C initializer CMake's
 * file(READ ... HEX) and "0x\\1," replacement write; *out is xmalloc'd. Fails
 * for an input that is not a regular file or whose list would pass the build
 * text limit. */
const char *build_io_byte_list(
    const char *root,
    const char *path,
    double deadline,
    const volatile sig_atomic_t *cancelled,
    char **out
);
/* Writes a C file defining `const unsigned char SYMBOL[]` with the bytes of a
 * regular input file of at most 64 MiB, and `SYMBOL_size`, in the form
 * cmake/embed_binary.cmake writes. An output that already holds that text is
 * left alone, keeping its stamp; written says whether it changed. */
const char *build_io_embed(
    const char *root,
    const char *input,
    const char *output,
    const char *symbol,
    double deadline,
    const volatile sig_atomic_t *cancelled,
    bool *written
);

#endif
