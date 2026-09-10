#ifndef CHIBI_PIN_FMT_H
#define CHIBI_PIN_FMT_H

#include "graph/model/pin.h"

/* Formats *v into a bounded, NUL-terminated static ring buffer (truncated,
 * never overflowed). The returned pointer is valid until the next few calls. */
const char *pin_value_to_string(const pin_value *v);

#endif
