#ifndef CHIBI_FILE_IO_H
#define CHIBI_FILE_IO_H

/* Read the complete file from disk. The returned buffer is NUL-terminated and
 * owned by the caller. NULL means the file could not be read. */
char *file_read_text(const char *path);
/* The running executable's path with symlinks resolved, owned by the caller.
 * Aborts when the platform cannot report it. */
char *file_executable_path();
/* The running executable's directory with symlinks resolved, owned by the
 * caller. Aborts when the platform cannot report it. */
char *file_executable_dir();

#endif
