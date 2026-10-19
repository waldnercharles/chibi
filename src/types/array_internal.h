#ifndef CHIBI_ARRAY_INTERNAL_H
#define CHIBI_ARRAY_INTERNAL_H

#include "types/array.h"

/* The canonical native add implementation has one concrete callback identity.
 * Runtime bindings share this implementation. */
pin_value pin_array_add(const pin_value *, int, arena *);
const char *pin_array_add_call(arena *, const pin_value *, int, pin_value *);

#endif
