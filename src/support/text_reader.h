#ifndef CHIBI_TEXT_READER_H
#define CHIBI_TEXT_READER_H
#include "support/text_file.h"

/* Synchronous owned text reads. An explicit reader is authoritative: NULL
 * means missing/refused, never permission to fall back to the filesystem.
 * Callback/context are borrowed only through the invoking parse operation. */
typedef struct
{
    void *context;
    char *(*read)(void *, const char *path);
} text_reader;
static inline char *text_reader_read(const text_reader *reader, const char *path)
{
    return reader && reader->read ? reader->read(reader->context, path)
                                  : text_file_read(path);
}
#endif
