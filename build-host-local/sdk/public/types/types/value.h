#ifndef CHIBI_VALUE_H
#define CHIBI_VALUE_H

#include "support/enum.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PIN_TYPE_LIST(X)                                                       \
    X(PIN_FLOW, NULL)                                                          \
    X(PIN_BOOL, "bool")                                                        \
    X(PIN_I8, "i8")                                                            \
    X(PIN_U8, "u8")                                                            \
    X(PIN_I16, "i16")                                                          \
    X(PIN_U16, "u16")                                                          \
    X(PIN_I32, "int")                                                          \
    X(PIN_U32, "u32")                                                          \
    X(PIN_I64, "i64")                                                          \
    X(PIN_U64, "u64")                                                          \
    X(PIN_F32, "number")                                                       \
    X(PIN_F64, "f64")                                                          \
    X(PIN_NAME, "name")                                                        \
    X(PIN_STR, "const char *")                                                 \
    X(PIN_HANDLE, "handle")                                                    \
    X(PIN_STRUCT, "struct")                                                    \
    X(PIN_ENUM, "enum")                                                        \
    X(PIN_TASK, "task")                                                        \
    X(PIN_UNION, "union")                                                      \
    X(PIN_POINTER, "pointer")                                                  \
    X(PIN_QUERY, "query")

DEFINE_ENUM(pin_type, PIN_TYPE_LIST)

typedef enum
{
    PIN_CONTAINER_SINGLE = 0,
    PIN_CONTAINER_ARRAY,
    PIN_CONTAINER_SET,
    PIN_CONTAINER_MAP,
    PIN_CONTAINER_VIEW,
} pin_container;

typedef struct pin_record pin_record;
typedef struct pin_schema pin_schema;
typedef struct arena arena;
typedef struct pin_text_owner pin_text_owner;
typedef struct pin_string
{
    const char *bytes;
    uint32_t length;
    /* NULL for borrowed bytes. Owned descriptors retain this immutable buffer. */
    pin_text_owner *owner;
} pin_string;

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
        const pin_string *text;
        void *ptr;
        int32_t e;
    };
    pin_container container;
    const char *type_name;
    const pin_record *record; /* Authored layout; native reflected values use NULL. */
    const pin_schema *schema; /* Owned native layout closure, runtime metadata. */
} pin_value;

#endif
