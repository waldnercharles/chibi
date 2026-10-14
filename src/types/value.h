#ifndef CHIBI_VALUE_H
#define CHIBI_VALUE_H

#include "support/enum.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PIN_TYPE_LIST(X)                                                       \
    X(PIN_FLOW, NULL)                                                          \
    X(PIN_BOOL, "bool")                                                        \
    X(PIN_I8, "int8")                                                          \
    X(PIN_U8, "byte")                                                          \
    X(PIN_I16, "int16")                                                        \
    X(PIN_U16, "uint16")                                                       \
    X(PIN_I32, "int")                                                          \
    X(PIN_U32, "uint")                                                         \
    X(PIN_I64, "int64")                                                        \
    X(PIN_U64, "uint64")                                                       \
    X(PIN_F32, "float")                                                        \
    X(PIN_F64, "double")                                                       \
    X(PIN_NAME, "name")                                                        \
    X(PIN_STR, "const char *")                                                 \
    X(PIN_HANDLE, "handle")                                                    \
    X(PIN_STRUCT, "struct")                                                    \
    X(PIN_ENUM, "enum")                                                        \
    X(PIN_TASK, "task")                                                        \
    X(PIN_UNION, "union")

DEFINE_ENUM(pin_type, PIN_TYPE_LIST)

typedef enum
{
    PIN_CONTAINER_SINGLE = 0,
    PIN_CONTAINER_ARRAY,
    PIN_CONTAINER_SET,
    PIN_CONTAINER_MAP,
} pin_container;

typedef struct
{
    pin_type type;
    union
    {
        bool b;
        int8_t i8;
        uint8_t u8;
        int16_t i16;
        uint16_t u16;
        int32_t i;
        uint32_t u;
        int64_t i64;
        uint64_t u64;
        float f;
        double d;
        const char *str;
        void *ptr;
        int32_t e;
    };
    pin_container container;
    const char *type_name;
} pin_value;

#endif
