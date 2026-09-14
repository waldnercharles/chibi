#ifndef CHIBI_BEHAVIOR_RANDOM_H
#define CHIBI_BEHAVIOR_RANDOM_H

#include <math.h>
#include <stdlib.h>

/* Inputs are finite and ordered. Keep the language's extended-precision
 * interpolation on targets where long double is wider than double. Explicit
 * fusion makes rounding independent of the compiler's contraction flags. */
static inline double behavior_random_range(double min, double max)
{
    double sample = (double)rand() / ((double)RAND_MAX + 1);
    long double fraction = (long double)sample;
    double value = (double)fmal((long double)min, 1 - fraction, (long double)max * fraction);
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

#endif
