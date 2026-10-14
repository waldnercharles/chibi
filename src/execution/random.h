#ifndef CHIBI_BEHAVIOR_RANDOM_H
#define CHIBI_BEHAVIOR_RANDOM_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* PCG-XSH-RR32, fixed stream54. All arithmetic is explicitly unsigned.
 * A zero-initialized state lazily uses seed1. State belongs to one serial
 * execution owner; random operations are excluded from parallel row batches. */
typedef struct
{
    uint64_t state;
    bool initialized;
} behavior_random;
#define BEHAVIOR_RANDOM_SERVICE "execution_random"
void behavior_random_seed(behavior_random *, unsigned seed);
uint32_t behavior_random_u32(behavior_random *);
/* Nested scope bridge for contextless native kernels. Enter borrows state;
 * Leave restores exactly the previous pointer, including an unbound scope. */
behavior_random *behavior_random_enter(behavior_random *);
void behavior_random_leave(behavior_random *previous);
struct behavior_function_context;
behavior_random *behavior_random_context(struct behavior_function_context *);
int behavior_random_int();
double behavior_random_unit();
/* Inputs are finite and ordered. Preserve the language's extended precision
 * interpolation where long double is wider; explicit fusion fixes contraction. */
static inline double behavior_random_range(double min, double max)
{
    long double fraction = (long double)behavior_random_unit();
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
