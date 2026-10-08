#ifndef CHIBI_VALUE_FORMAT_H
#define CHIBI_VALUE_FORMAT_H

#include "types/value.h"

/* Formats *v into a bounded, NUL-terminated static ring buffer (truncated,
 * never overflowed). The returned pointer is valid until the next few calls. */
const char *pin_value_to_string(const pin_value *v);

#endif
