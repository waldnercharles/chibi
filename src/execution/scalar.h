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

static inline int behavior_string_equal(const char *a, const char *b)
{
    return a == b || strcmp(a ? a : "", b ? b : "") == 0;
}

static inline bool behavior_scalar_string(pin_type type)
{
    return type == PIN_NAME || type == PIN_STR;
}

static inline bool behavior_scalar_integer(pin_type type)
{
    return (type >= PIN_I8 && type <= PIN_U64) || type == PIN_ENUM;
}
static inline bool behavior_scalar_signed(pin_type type)
{
    return type == PIN_I8 || type == PIN_I16 || type == PIN_I32 || type == PIN_I64;
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
static inline pin_value behavior_scalar_cast(pin_value a, pin_type type)
{
    pin_value out = { .type = type };
    if (a.type == PIN_I32 && (type == PIN_U32 || type == PIN_U64)) {
        if (type == PIN_U32) {
            out.u = (uint32_t)a.i;
        } else {
            out.u64 = (uint64_t)(int64_t)a.i;
        }
        return out;
    }
    if (type == PIN_F64) {
        switch (a.type) {
            case PIN_BOOL: out.d = a.b; break;
            case PIN_I8: out.d = a.i8; break;
            case PIN_U8: out.d = a.u8; break;
            case PIN_I16: out.d = a.i16; break;
            case PIN_U16: out.d = a.u16; break;
            case PIN_I32: out.d = a.i; break;
            case PIN_U32: out.d = a.u; break;
            case PIN_I64: out.d = (double)a.i64; break;
            default: assert(false); break;
        }
        return out;
    }
    if (a.type == PIN_I32 && type == PIN_I64) {
        out.i64 = a.i;
        return out;
    }
    double x = a.type == PIN_F32 ? a.f : a.d;
    int64_t low = type == PIN_U8 ? 0 : type == PIN_I32 ? INT32_MIN : INT64_MIN;
    int64_t high = type == PIN_U8  ? UINT8_MAX
                 : type == PIN_I32 ? INT32_MAX
                                   : INT64_MAX;
    int64_t n = isnan(x)          ? 0
              : x <= (double)low  ? low
              : x >= (double)high ? high
                                  : (int64_t)x;
    if (type == PIN_U8) {
        out.u8 = (uint8_t)n;
    }
    if (type == PIN_I32) {
        out.i = (int32_t)n;
    }
    if (type == PIN_I64) {
        out.i64 = n;
    }
    return out;
}
#endif
