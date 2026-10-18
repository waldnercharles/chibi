#ifndef CHIBI_HOST_BUILD_IO_H
#define CHIBI_HOST_BUILD_IO_H

#include <signal.h>
#include <stdbool.h>

/* Private build-host file operations. Root is an explicit absolute directory;
 * absolute paths are also accepted. Returned strings are caller-owned. No
 * operation changes the process working directory. */
char *build_io_path(const char *root, const char *path);
char *build_io_resolve(const char *root, const char *path);
bool build_io_exists(const char *root, const char *path);
bool build_io_mkdir(const char *root, const char *path);

/* Read at most 1 MiB of regular-file text. A missing file returns an owned
 * empty string; all other failures return false with a static capability
 * diagnostic. Deadline uses chibi_process_now(); cancelled may be NULL. */
bool build_io_read(
    const char *root,
    const char *path,
    double deadline,
    const volatile sig_atomic_t *cancelled,
    char **out,
    const char **error
);

/* Atomically replace text through a temporary file in the destination directory.
 * Text is limited to 1 MiB. Failed writes remove the temporary file. */
bool build_io_write(
    const char *root,
    const char *path,
    const char *text,
    double deadline,
    const volatile sig_atomic_t *cancelled
);

#endif
