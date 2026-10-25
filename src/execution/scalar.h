#ifndef CHIBI_BEHAVIOR_SCALAR_H
#define CHIBI_BEHAVIOR_SCALAR_H
#include "execution/instruction.h"
#include "support/cos_sin.h"
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
/* Keep the shared deterministic float approximation without widening. */
static inline float cos_sin_sinef(float x)
{
    return cos_sin_compute(x).s;
}
static inline float cos_sin_cosinef(float x)
{
    return cos_sin_compute(x).c;
}
static inline float behavior_fractf(float x)
{
    return x - floorf(x);
}
static inline float behavior_signf(float x)
{
    return (x > 0) - (x < 0);
}

/* Integer arithmetic uses declared-width two's-complement bits and never
 * fails. Overflow wraps. x / 0 is 0 and x % 0 is x. The signed minimum / -1
 * wraps to the minimum, with remainder 0. */
static inline void behavior_scalar_integer_op(
    behavior_function_op op,
    pin_value a,
    pin_value b,
    pin_value *out,
    pin_value *other
)
{
    assert(a.type >= PIN_I8 && a.type <= PIN_U64);
    bool sign = behavior_scalar_signed(a.type);
    uint64_t x = behavior_scalar_bits(a),
             y = op == BF_NEG || op == BF_ABS ? 0 : behavior_scalar_bits(b),
             bits = 0;
    int64_t sx = (int64_t)x, sy = (int64_t)y;
    switch (op) {
        case BF_ADD: bits = x + y; break;
        case BF_SUB: bits = x - y; break;
        case BF_MUL: bits = x * y; break;
        case BF_DIV:
        case BF_MOD: {
            /* x / -1 negates, which wraps the minimum to itself. */
            bool negate = sign && sy == -1;
            uint64_t quotient = !y     ? 0
                              : negate ? UINT64_C(0) - x
                              : sign   ? (uint64_t)(sx / sy)
                                       : x / y;
            uint64_t remainder = !y     ? x
                               : negate ? 0
                               : sign   ? (uint64_t)(sx % sy)
                                        : x % y;
            bits = op == BF_DIV ? quotient : remainder;
            if (other) {
                *other = behavior_scalar_from_bits(a.type, op == BF_DIV ? remainder : quotient);
            }
            break;
        }
        case BF_NEG: bits = UINT64_C(0) - x; break;
        case BF_ABS: bits = sign && sx < 0 ? UINT64_C(0) - x : x; break;
        case BF_MIN: bits = (sign ? sx < sy : x < y) ? x : y; break;
        case BF_MAX: bits = (sign ? sx > sy : x > y) ? x : y; break;
        case BF_LT: out->b = sign ? sx < sy : x < y; return;
        case BF_LE: out->b = sign ? sx <= sy : x <= y; return;
        case BF_GT: out->b = sign ? sx > sy : x > y; return;
        case BF_GE: out->b = sign ? sx >= sy : x >= y; return;
        case BF_EQ: out->b = x == y; return;
        case BF_NE: out->b = x != y; return;
        default: assert(false);
    }
    *out = behavior_scalar_from_bits(a.type, bits);
}

/* Integer bitwise operations preserve the declared width. Left shifts discard
 * bits; signed right shifts extend the sign bit, unsigned right shifts insert
 * zeros. A count at or past the width shifts every bit out. The count is
 * unsigned. Compute unsigned bits so no signed shift or overflow depends on C
 * behavior. */
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
    left &= mask;
    switch (op) {
        case BF_BIT_AND: return (left & right) & mask;
        case BF_BIT_OR: return (left | right) & mask;
        case BF_BIT_XOR: return (left ^ right) & mask;
        case BF_BIT_NOT: return ~left & mask;
        case BF_SHL: return right < width ? (left << right) & mask : 0;
        case BF_SHR: {
            bool fill = behavior_scalar_signed(type) &&
                        (left & (UINT64_C(1) << (width - 1)));
            if (right >= width) {
                return fill ? mask : 0;
            }
            uint64_t bits = left >> right;
            return fill && right ? bits | (mask ^ (mask >> right)) : bits;
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
