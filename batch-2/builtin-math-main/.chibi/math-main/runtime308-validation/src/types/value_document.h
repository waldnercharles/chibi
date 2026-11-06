#ifndef CHIBI_VALUE_DOCUMENT_H
#define CHIBI_VALUE_DOCUMENT_H

#include "support/arena.h"
#include "support/asset_value.h"
#include "types/value.h"

/* Values borrow their payloads; the resulting document owns a complete copy.
 * Invalid authored metadata fails the whole projection: write leaves the
 * destination key unchanged and to_document returns NULL (not av_null). */
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
 * Arrays/sets retain element types and one owned authored layout. Maps encode
 * key_type/value_type descriptors once and ordered entries of two payloads.
 * Nested collection arrays carry one element_type descriptor beside items.
 * Authored map values require the declaration's pin_map shape and owned layout;
 * documents never define executable layouts. Every struct field must be
 * present. Traversal is limited to depth 64, one million values, 64 MiB owned
 * payload, and 64 MiB of charged map key comparisons. */
int pin_value_read_document(
    arena *storage,
    pin_value declaration,
    const av_value *,
    pin_value *out,
    char *error,
    size_t error_size
);

#endif
