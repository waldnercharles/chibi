#ifndef CHIBI_ARRAY_INTERNAL_H
#define CHIBI_ARRAY_INTERNAL_H

#include "types/array.h"

/* The canonical native add implementation has one concrete callback identity.
 * Runtime bindings and post-link execution proofs share this implementation. */
pin_value pin_array_add(const pin_value *, int, arena *);
const char *pin_array_add_call(arena *, const pin_value *, int, pin_value *);

/* Private derived-loop operation. The caller proves that a nonempty prefix
 * was produced by this operation in storage, has no observable aliases, and
 * its initialized items/layout cannot change. Each successful call publishes
 * a new logical header; count, never capacity, bounds collection tracing.
 * Original incoming metadata is checked before owned-layout substitution. */
int pin_array_build_row(arena *, const pin_value *prefix, const pin_value *row, int capacity, pin_value *out);

#endif
