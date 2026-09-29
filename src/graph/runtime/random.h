#ifndef CHIBI_BEHAVIOR_RANDOM_H
#define CHIBI_BEHAVIOR_RANDOM_H

#include <math.h>
#include <stdint.h>

/* A PCG32 stream. Every behavior random draw (Random Real, Random Range,
 * Random Range Vector, random state selection and expression rand()) reads
 * the selected stream, so a seeded world replays the same draws. */
typedef struct
{
    uint64_t state;
} behavior_random;

#define BEHAVIOR_RAND_MAX 0x7fffffff

void behavior_random_seed(behavior_random *stream, uint64_t seed);
/* Selects the stream later draws read. Worlds select their own before running
 * graphs; NULL selects the process stream that tools and tests share. */
void behavior_random_use(behavior_random *stream);
/* Reseeds the selected stream. */
void behavior_srand(uint64_t seed);
/* The next draw from the selected stream, from 0 to BEHAVIOR_RAND_MAX. */
int behavior_rand();

/* From 0 up to, but not including, 1. */
static inline double behavior_random_unit()
{
    return (double)behavior_rand() / ((double)BEHAVIOR_RAND_MAX + 1);
}

/* Inputs are finite and ordered. Keep the language's extended-precision
 * interpolation on targets where long double is wider than double. Explicit
 * fusion makes rounding independent of the compiler's contraction flags. */
static inline double behavior_random_range(double min, double max)
{
    double sample = behavior_random_unit();
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
