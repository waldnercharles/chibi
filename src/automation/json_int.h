#ifndef CHIBI_AUTOMATION_JSON_INT_H
#define CHIBI_AUTOMATION_JSON_INT_H

#include "third_party/yyjson/yyjson.h"

#include <stdbool.h>
#include <stdint.h>

/* True when `value` is a JSON integer from `min` to `max`, stored in `out`;
 * unlike yyjson_get_int, a value outside int's range never wraps into it. */
static inline bool json_int_in_range(yyjson_val *value, int min, int max, int *out)
{
    if (!yyjson_is_int(value) ||
        (yyjson_is_uint(value) && yyjson_get_uint(value) > INT64_MAX)) {
        return false;
    }
    int64_t number = yyjson_get_sint(value);
    if (number < min || number > max) {
        return false;
    }
    *out = (int)number;
    return true;
}

#endif
