#ifndef CBLUEPRINT_PIN_H
#define CBLUEPRINT_PIN_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    PIN_FLOW,
    PIN_BOOL,
    PIN_I8,
    PIN_U8,
    PIN_I16,
    PIN_U16,
    PIN_I32,
    PIN_U32,
    PIN_I64,
    PIN_U64,
    PIN_F32,
    PIN_F64,
    PIN_STR,
    PIN_STRUCT,
    PIN_ENUM,
} pin_type;

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
        const char *str; /* PIN_STR */
        void *ptr;       /* PIN_STRUCT, points at the instance */
        int32_t e;       /* PIN_ENUM */
    };
    const char *type_name; /* set for PIN_STRUCT / PIN_ENUM */
} pin_value;

typedef enum
{
    PIN_DATA = 0,
    PIN_EXEC = 1,
    PIN_KIND_DATA = PIN_DATA,
    PIN_KIND_EXEC = PIN_EXEC,
} pin_kind;

typedef struct
{
    const char *name;
    pin_type type;
    const char *type_name; /* C type spelling; required match for PIN_STRUCT / PIN_ENUM */
    pin_kind kind; /* DATA is the compatibility default for old descriptors. */
} pin_spec;

#endif
