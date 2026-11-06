#ifndef CHIBI_VALUE_FORMAT_H
#define CHIBI_VALUE_FORMAT_H

#include "types/value.h"

/* Formats *v into a bounded, NUL-terminated thread-local ring (truncated,
 * never overflowed). The pointer survives seven more calls on this thread;
 * other threads do not overwrite it. It cannot outlive the producing thread. */
const char *pin_value_to_string(const pin_value *v);

#endif
