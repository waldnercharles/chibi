#ifndef CHIBI_SUPPORT_SHA256_H
#define CHIBI_SUPPORT_SHA256_H

#include <stddef.h>
#include <stdint.h>

/* SHA-256 (FIPS 180-4) over bytes added in any number of pieces. */
typedef struct
{
    uint32_t state[8];
    uint64_t length;
    unsigned char block[64];
} sha256;

void sha256_begin(sha256 *);
void sha256_add(sha256 *, const void *, size_t);
/* Writes the digest as 64 lowercase hex digits and a NUL. */
void sha256_end(sha256 *, char out[65]);
void sha256_hex(const void *, size_t, char out[65]);
#endif
