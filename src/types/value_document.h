#ifndef CHIBI_VALUE_DOCUMENT_H
#define CHIBI_VALUE_DOCUMENT_H

#include "support/arena.h"
#include "support/asset_value.h"
#include "types/value.h"

/* Values borrow their payloads; the resulting document owns a complete copy. */
void pin_value_write_document(
    av_doc *,
    av_value *object,
    const char *key,
    pin_type,
    const char *type_name,
    const pin_value *
);
av_value *pin_value_to_document(av_doc *, const pin_value *);

/* Reads one declared value. Managed payloads and names belong to storage until
 * reset/free; NULL storage permits scalar reads with borrowed declaration names.
 * Output is unchanged on failure; partial allocations belong to the same arena.
 * Arrays/sets retain element types; maps lack a reflected document contract.
 * Traversal is limited to depth 64, one million values, and 64 MiB owned payload. */
int pin_value_read_document(
    arena *storage,
    pin_value declaration,
    const av_value *,
    pin_value *out,
    char *error,
    size_t error_size
);

#endif
