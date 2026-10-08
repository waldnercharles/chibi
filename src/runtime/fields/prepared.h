#pragma once

#define FIELD_PREPARED_VERSION 1
#include "runtime/fields/program.h"

#define FIELD_PREPARED_MAX_BYTES (8u * 1024u * 1024u)
#define FIELD_PREPARED_MAX_NAME FIELD_PARAMETER_NAME_MAX
/* Portable little-endian scalar records. No native pointers, source/compiler,
 * registry or executable machine code is serialized. Write returns malloc
 * bytes; read owns an independently validated immutable program. */
bool field_prepared_write(
    const field_program *,
    unsigned char **bytes,
    size_t *length,
    char *error,
    size_t size
);
field_program *field_prepared_read(const void *bytes, size_t length, char *error, size_t size);
/* Failure leaves the shared retained-storage budget unchanged. */
field_program *field_prepared_read_budget(
    const void *,
    size_t length,
    size_t *remaining,
    char *error,
    size_t size
);
