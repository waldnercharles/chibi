#ifndef CHIBI_BEHAVIOR_SCALAR_H
#define CHIBI_BEHAVIOR_SCALAR_H
#include "execution/instruction.h"
#include "types/scalar.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <string.h>

/* Fract and Sign, shared by their nodes and every backend. */
static inline double behavior_fract(double x)
{
    return x - floor(x);
}
static inline double behavior_sign(double x)
{
    return (x > 0) - (x < 0);
}

/* Integer bitwise operations preserve the declared width. Shift counts use
 * their low log2(width) bits, including negative counts. Left shifts wrap;
 * signed right shifts extend the sign bit, unsigned right shifts insert zeros.
 * Compute unsigned bits so no signed shift or overflow depends on C behavior. */
static inline uint64_t behavior_scalar_bitwise_bits(
    behavior_function_op op,
    pin_type type,
    uint64_t left,
    uint64_t right
)
{
    assert(type >= PIN_I8 && type <= PIN_U64);
    unsigned width = (unsigned)behavior_scalar_size(type) * CHAR_BIT;
    uint64_t mask = UINT64_MAX >> (64 - width);
    unsigned count = (unsigned)(right & (width - 1));
    left &= mask;
    switch (op) {
        case BF_BIT_AND: return (left & right) & mask;
        case BF_BIT_OR: return (left | right) & mask;
        case BF_BIT_XOR: return (left ^ right) & mask;
        case BF_BIT_NOT: return ~left & mask;
        case BF_SHL: return (left << count) & mask;
        case BF_SHR: {
            uint64_t bits = left >> count;
            if (count && behavior_scalar_signed(type) &&
                (left & (UINT64_C(1) << (width - 1)))) {
                bits |= mask ^ (mask >> count);
            }
            return bits;
        }
        default: assert(false); return 0;
    }
}

static inline const char *behavior_scalar_field(pin_type type)
{
    switch (type) {
#define FIELD(pin, type, field)                                                \
    case pin: return #field;
        BEHAVIOR_SCALARS(FIELD)
#undef FIELD
        default: assert(false); return NULL;
    }
}
static inline const char *behavior_scalar_c_type(pin_type type)
{
    switch (type) {
#define TYPE(pin, type, field)                                                 \
    case pin: return #type;
        BEHAVIOR_SCALARS(TYPE)
#undef TYPE
        default: assert(false); return NULL;
    }
}
#endif
