#ifndef CHIBI_VALUE_SCALAR_H
#define CHIBI_VALUE_SCALAR_H
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
    X(PIN_UNION, void *, ptr)                                                  \
    X(PIN_NAME, const char *, str)                                             \
    X(PIN_STR, const char *, str)

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
        case PIN_UNION: return (uintptr_t)value.ptr;
        case PIN_NAME:
        case PIN_STR: return (uintptr_t)value.str;
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

/* The numeric casts every backend implements. Lowering builds any other
 * conversion it needs from these and BF_CONVERT. */
static inline bool behavior_scalar_cast_supported(pin_type from, pin_type to)
{
    if (from == to && (to == PIN_F32 || to == PIN_F64)) {
        return true;
    }
    if (to == PIN_F64) {
        return from >= PIN_BOOL && from <= PIN_I64;
    }
    if (from == PIN_F32 || from == PIN_F64) {
        return to == PIN_U8 || to == PIN_I32 || to == PIN_I64;
    }
    return from == PIN_I32 && (to == PIN_U32 || to == PIN_U64 || to == PIN_I64);
}
static inline pin_value behavior_scalar_cast(pin_value a, pin_type type)
{
    assert(behavior_scalar_cast_supported(a.type, type));
    if (a.type == type) {
        return a;
    }
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
