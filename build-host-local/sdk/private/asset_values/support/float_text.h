#ifndef CHIBI_FLOAT_TEXT_H
#define CHIBI_FLOAT_TEXT_H

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Remove decimal noise only while parsing preserves the source scalar exactly.
   Float callers compare as float; double callers retain every double bit. */
static inline void float_text(char *out, size_t size, double value, bool single)
{
    int digits = single ? FLT_DECIMAL_DIG : DBL_DECIMAL_DIG;
    int precision;
    for (precision = 1; precision <= digits; precision++) {
        snprintf(out, size, "%.*g", precision, value);
        double parsed = single ? (double)strtof(out, NULL) : strtod(out, NULL);
        if (parsed == value && signbit(parsed) == signbit(value)) {
            break;
        }
    }
    const char *scientific = strchr(out, 'e');
    if (!scientific) {
        return;
    }
    int exponent = (int)strtol(scientific + 1, NULL, 10);
    /* Keep ordinary authored quantities readable without expanding extremes. */
    if (exponent >= -4 && exponent < 16) {
        int decimals = precision - exponent - 1;
        snprintf(out, size, "%.*f", decimals > 0 ? decimals : 0, value);
    }
}

#endif
