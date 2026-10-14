#ifndef CHIBI_SHA256_H
#define CHIBI_SHA256_H
#include <stddef.h>

/* SHA-256 of exactly size bytes; NULL data is valid only for an empty input. */
void sha256(const void *data, size_t size, unsigned char digest[32]);
/* Lowercase hexadecimal digest, including the terminating NUL. */
void sha256_hex(const void *data, size_t size, char hex[65]);
#endif
