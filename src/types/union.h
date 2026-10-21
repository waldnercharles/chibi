#ifndef CHIBI_TYPES_UNION_H
#define CHIBI_TYPES_UNION_H

#include "support/arena.h"
#include "types/value.h"

/* Immutable tagged values own snapshots of their payload in their arena.
 * Scalars live inline in values; nested managed values retain their own trace.
 * No source/catalog pointer is kept in this runtime representation. */
typedef struct
{
    uint64_t tag;
    int count;
    pin_value values[];
} pin_union;

/* Stable built-in singleton identity, independent of an enclosing union. */
#define PIN_UNION_CANCELLED_TAG UINT64_C(0x63616e63656c6c65)

int pin_union_value(arena *, uint64_t tag, const pin_value *, int count, pin_value *out);
/* pin_union_value for values that stay unchanged while the union does, as a
 * returned union's do until the return copies it: a value held entirely in
 * the arena is kept itself, and any other is copied, so the union reaches
 * no other storage. */
int pin_union_moved(arena *, uint64_t tag, const pin_value *, int count, pin_value *out);
void pin_union_cancelled(pin_value *out);
/* Owned copies for pin_value_copy/destroy; never destroy an arena value. */
int pin_union_copy(pin_value *out, const pin_value *source);
void pin_union_destroy(pin_union *);

#endif
