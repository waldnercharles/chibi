#ifndef CHIBI_AUTHORING_SOURCE_LITERAL_H
#define CHIBI_AUTHORING_SOURCE_LITERAL_H

#include "capabilities/catalog.h"

/* Catalog spelling as identifier segments. Spaces and hyphens become `_`,
 * matching catalog name resolution. Other punctuation is rejected. */
char *authoring_source_identifier(const char *, char *error, size_t);

/* Contextual source expression; caller frees the result. Unsupported storage,
 * malformed shapes and bounded-output failures return NULL with an error.
 * Input payloads borrow their declared layout for the duration of the call. */
char *authoring_source_literal(
    const script_catalog *,
    script_type expected,
    const pin_value *,
    char *error,
    size_t
);
#endif
