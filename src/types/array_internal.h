#ifndef CHIBI_ARRAY_INTERNAL_H
#define CHIBI_ARRAY_INTERNAL_H

#include "types/array.h"

/* The canonical native add implementation has one concrete callback identity.
 * Runtime bindings and post-link execution proofs share this implementation. */
pin_value pin_array_add(const pin_value *, int, arena *);
const char *pin_array_add_call(arena *, const pin_value *, int, pin_value *);

/* Canonical readers never change their input. Get retains one owned result;
 * the public snapshot callback retains that result a second time, as before. */
pin_value pin_array_get_result(const pin_value *, int, arena *);
pin_value pin_array_length(const pin_value *, int, arena *);
const char *pin_array_get_owned_call(arena *, const pin_value *, int, pin_value *);
const char *pin_array_get_call(arena *, const pin_value *, int, pin_value *);
const char *pin_array_length_owned_call(arena *, const pin_value *, int, pin_value *);
const char *pin_array_length_call(arena *, const pin_value *, int, pin_value *);

/* Private derived-loop operation. The caller proves that a nonempty prefix
 * was produced by this operation in storage, has no observable aliases, and
 * its initialized items/layout cannot change. Each successful call publishes
 * a new logical header; count, never capacity, bounds collection tracing.
 * Original incoming metadata is checked before owned-layout substitution. */
int pin_array_build_row(arena *, const pin_value *prefix, const pin_value *row, int capacity, pin_value *out);

/* Private scalar tail. The execution proof prevents writes to shared prefixes
 * and snapshots every escape. The latest header must also be a managed root.
 * Clear this cache whenever its arena is reset, even at the same address. */
typedef struct
{
    arena *owner;
    const pin_array *latest;
    void *data;
    int count, capacity;
} pin_array_builder;
const char *pin_array_build_i32(arena *, pin_array_builder *, const pin_value *, pin_value *);

#endif
