#ifndef CHIBI_UTF8_H
#define CHIBI_UTF8_H
#include <stdbool.h>
#include <stddef.h>
/* Unicode scalar UTF-8, without embedded NUL. No normalization is performed. */
bool utf8_text_valid(const char *, size_t bytes);
#endif
