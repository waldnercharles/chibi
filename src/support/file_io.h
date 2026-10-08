#ifndef CHIBI_FILE_IO_H
#define CHIBI_FILE_IO_H

/* Read the complete file from disk. The returned buffer is NUL-terminated and
 * owned by the caller. NULL means the file could not be read. */
char *file_read_text(const char *path);

#endif
