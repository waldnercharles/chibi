#ifndef CHIBI_VALUE_SCALAR_H
#define CHIBI_VALUE_SCALAR_H
#include "types/text.h"
#include "types/value.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

/* Physical scalar storage shared by executable and persistent values. */
#define BEHAVIOR_SCALARS(X)                                                    \
    X(PIN_BOOL, bool, b)                                                       \
    X(PIN_I8, int8_t, i8)                                                      \
    X(PIN_U8, uint8_t, u8)                                                     \
    X(PIN_I16, int16_t, i16)                                                   \
    X(PIN_U16, uint16_t, u16)                                                  \
    X(PIN_I32, int32_t, i)                                                     \
    X(PIN_U32, uint32_t, u)                                                    \
    X(PIN_I64, int64_t, i64)                                                   \
    X(PIN_U64, uint64_t, u64)                                                  \
    X(PIN_F32, float, f)                                                       \
    X(PIN_F64, double, d)                                                      \
    X(PIN_ENUM, int32_t, e)                                                    \
    X(PIN_HANDLE, uint64_t, u64)                                               \
    X(PIN_TASK, uint64_t, u64)                                                 \
    X(PIN_QUERY, void *, ptr)                                                  \
    X(PIN_POINTER, void *, ptr)                                                \
    X(PIN_UNION, void *, ptr)                                                  \
    X(PIN_NAME, const char *, str)                                             \
    X(PIN_STR, const pin_string *, text)

/* Numeric bits, independent of host byte order. */
static inline uint64_t behavior_scalar_bits(pin_value value)
{
    switch (value.type) {
        case PIN_BOOL: return value.b;
        case PIN_I8: return (uint64_t)value.i8;
        case PIN_U8: return value.u8;
        case PIN_I16: return (uint64_t)value.i16;
        case PIN_U16: return value.u16;
        case PIN_I32: return (uint64_t)value.i;
        case PIN_U32: return value.u;
        case PIN_I64: return (uint64_t)value.i64;
        case PIN_U64:
        case PIN_HANDLE:
        case PIN_TASK: return value.u64;
        case PIN_ENUM: return (uint64_t)value.e;
        case PIN_QUERY:
        case PIN_POINTER:
        case PIN_UNION: return (uintptr_t)value.ptr;
        case PIN_NAME: return (uintptr_t)value.str;
        case PIN_STR: return (uintptr_t)value.text;
        default: assert(false); return 0;
    }
}
static inline size_t behavior_scalar_size(pin_type type)
{
    switch (type) {
#define SIZE(pin, type, field)                                                 \
    case pin: return sizeof(type);
        BEHAVIOR_SCALARS(SIZE)
#undef SIZE
        default: assert(false); return 0;
    }
}

static inline pin_value behavior_scalar_from_bits(pin_type type, uint64_t bits)
{
    pin_value value = { .type = type };
    switch (type) {
        case PIN_I8: {
            uint8_t x = (uint8_t)bits;
            memcpy(&value.i8, &x, sizeof x);
            break;
        }
        case PIN_U8: value.u8 = (uint8_t)bits; break;
        case PIN_I16: {
            uint16_t x = (uint16_t)bits;
            memcpy(&value.i16, &x, sizeof x);
            break;
        }
        case PIN_U16: value.u16 = (uint16_t)bits; break;
        case PIN_I32: {
            uint32_t x = (uint32_t)bits;
            memcpy(&value.i, &x, sizeof x);
            break;
        }
        case PIN_U32: value.u = (uint32_t)bits; break;
        case PIN_I64: memcpy(&value.i64, &bits, sizeof bits); break;
        case PIN_U64: value.u64 = bits; break;
        default: assert(false); break;
    }
    return value;
}

static inline int behavior_text_equal(const pin_string *a, const pin_string *b)
{
    return a == b || pin_text_equal(
                         a ? *a : (pin_string){ .bytes = "", .length = 0 },
                         b ? *b : (pin_string){ .bytes = "", .length = 0 }
                     );
}
static inline int behavior_string_equal(const char *a, const char *b)
{
    return a == b || !strcmp(a ? a : "", b ? b : "");
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

/* BF_CAST keeps its mode in the instruction target. An integer destination
 * applies it to a value outside its range: EXACT fails, WRAP keeps the low
 * bits and CLAMP saturates. Floating sources round toward zero, saturate and
 * map NaN to zero in every mode. */
typedef enum
{
    BEHAVIOR_CAST_EXACT,
    BEHAVIOR_CAST_WRAP,
    BEHAVIOR_CAST_CLAMP,
} behavior_cast_mode;

static inline bool behavior_scalar_number(pin_type type)
{
    return type >= PIN_I8 && type <= PIN_F64;
}
static inline int64_t behavior_scalar_integer_low(pin_type type)
{
    switch (type) {
        case PIN_I8: return INT8_MIN;
        case PIN_I16: return INT16_MIN;
        case PIN_I32: return INT32_MIN;
        case PIN_I64: return INT64_MIN;
        default: return 0;
    }
}
static inline uint64_t behavior_scalar_integer_high(pin_type type)
{
    switch (type) {
        case PIN_I8: return INT8_MAX;
        case PIN_U8: return UINT8_MAX;
        case PIN_I16: return INT16_MAX;
        case PIN_U16: return UINT16_MAX;
        case PIN_I32: return INT32_MAX;
        case PIN_U32: return UINT32_MAX;
        case PIN_I64: return INT64_MAX;
        default: return UINT64_MAX;
    }
}
/* Every value of an integer `from` fits `to`. */
static inline bool behavior_scalar_integer_widens(pin_type from, pin_type to)
{
    return behavior_scalar_integer_low(to) <= behavior_scalar_integer_low(from) &&
           behavior_scalar_integer_high(to) >= behavior_scalar_integer_high(from);
}

/* The operand type that holds both integers exactly: the wider operand, or
 * int or int64 for a signed/unsigned pair, since no backend computes in
 * narrower types. Returns PIN_FLOW for int64 with uint64 or a non-integer. */
static inline pin_type behavior_scalar_common_integer(pin_type x, pin_type y)
{
    if (x < PIN_I8 || x > PIN_U64 || y < PIN_I8 || y > PIN_U64) {
        return PIN_FLOW;
    }
    if (behavior_scalar_integer_widens(x, y)) {
        return y;
    }
    if (behavior_scalar_integer_widens(y, x)) {
        return x;
    }
    if (behavior_scalar_integer_widens(x, PIN_I32) &&
        behavior_scalar_integer_widens(y, PIN_I32)) {
        return PIN_I32;
    }
    if (behavior_scalar_integer_widens(x, PIN_I64) &&
        behavior_scalar_integer_widens(y, PIN_I64)) {
        return PIN_I64;
    }
    return PIN_FLOW;
}

/* The numeric casts every backend implements. Lowering composes integer to
 * F32 through F64 and BF_CONVERT, and F32/F64 changes use BF_CONVERT. */
static inline bool behavior_scalar_cast_supported(pin_type from, pin_type to)
{
    if (to == PIN_F32) {
        return from == PIN_F32;
    }
    if (to == PIN_F64) {
        return from >= PIN_BOOL && from <= PIN_F64 && from != PIN_F32;
    }
    return to >= PIN_I8 && to <= PIN_U64 && behavior_scalar_number(from);
}
/* Whether the value fits: false only when EXACT receives an integer outside
 * the destination range. That value clamps, as CLAMP does, so a cast never
 * fails at run time; compile time rejects a constant that does not fit. */
static inline bool behavior_scalar_cast(pin_value a, pin_type type, behavior_cast_mode mode, pin_value *out)
{
    assert(behavior_scalar_cast_supported(a.type, type));
    if (a.type == type) {
        *out = a;
        return true;
    }
    if (type == PIN_F64) {
        *out = (pin_value){ .type = PIN_F64 };
        uint64_t bits = behavior_scalar_bits(a);
        out->d = behavior_scalar_signed(a.type) ? (double)(int64_t)bits : (double)bits;
        return true;
    }
    int64_t low = behavior_scalar_integer_low(type);
    uint64_t high = behavior_scalar_integer_high(type);
    if (a.type == PIN_F32 || a.type == PIN_F64) {
        double x = a.type == PIN_F32 ? a.f : a.d;
        uint64_t n = isnan(x)          ? 0
                   : x <= (double)low  ? (uint64_t)low
                   : x >= (double)high ? high
                   : x < 0             ? (uint64_t)(int64_t)x
                                       : (uint64_t)x;
        *out = behavior_scalar_from_bits(type, n);
        return true;
    }
    uint64_t bits = behavior_scalar_bits(a);
    bool negative = behavior_scalar_signed(a.type) && (int64_t)bits < 0;
    bool below = negative && (int64_t)bits < low, above = !negative && bits > high;
    if (mode != BEHAVIOR_CAST_WRAP) {
        bits = below ? (uint64_t)low : above ? high : bits;
    }
    *out = behavior_scalar_from_bits(type, bits);
    return mode != BEHAVIOR_CAST_EXACT || (!below && !above);
}
#endif
