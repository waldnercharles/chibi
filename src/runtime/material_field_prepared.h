#pragma once

#define MATERIAL_FIELDS_PREPARED_VERSION 1
#include "runtime/material_field.h"

#define MATERIAL_FIELDS_PREPARED_MAX_BYTES (64u * 1024u * 1024u)
/* Portable catalog of validated field programs and typed named arguments.
 * Write owns malloc bytes; read validates the complete catalog before returning
 * ownership. No authored parameter strings are interpreted by the reader. */
bool material_fields_prepared_write(
    const material_fields *,
    unsigned char **bytes,
    size_t *length,
    char *error,
    size_t size
);
material_fields *material_fields_prepared_read(const void *bytes, size_t length, char *error, size_t size);
/* Failure leaves the shared retained-storage budget unchanged. */
material_fields *material_fields_prepared_read_budget(
    const void *,
    size_t length,
    size_t *remaining,
    char *error,
    size_t size
);
