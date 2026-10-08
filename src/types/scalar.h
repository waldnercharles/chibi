#ifndef CHIBI_VALUE_SCALAR_H
#define CHIBI_VALUE_SCALAR_H
#include "types/value.h"

#include <assert.h>
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
        case PIN_HANDLE: return value.u64;
        case PIN_ENUM: return (uint64_t)value.e;
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

#endif
