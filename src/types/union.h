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
/* pin_union_value that keeps the values the `keep` mask names when they are
 * held in the arena, and copies the others: a union built in a caller's
 * storage keeps the locals built there and nothing else of the caller's. */
int pin_union_kept(arena *, uint64_t tag, uint64_t keep, const pin_value *, int count, pin_value *out);
void pin_union_cancelled(pin_value *out);
/* Owned copies for pin_value_copy/destroy; never destroy an arena value. */
int pin_union_copy(pin_value *out, const pin_value *source);
void pin_union_destroy(pin_union *);

#endif
