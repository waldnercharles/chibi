#ifndef CHIBI_TEXT_FILE_H
#define CHIBI_TEXT_FILE_H

/* Read a whole binary/text file, or its unsaved text (text_overlay.h). The
 * returned buffer is NUL-terminated and owned by the caller. NULL means the
 * file could not be read. */
char *text_file_read(const char *path);

#endif
