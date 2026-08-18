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
    const char *type_name;
} pin_value;

typedef enum
{
    PIN_DATA = 0,
    PIN_EXEC = 1,
    PIN_KIND_DATA = PIN_DATA,
    PIN_KIND_EXEC = PIN_EXEC,
} pin_kind;

enum
{
    PIN_FLAG_ADVANCED = 1u << 0,
    PIN_FLAG_WILDCARD = 1u << 1,
    PIN_FLAG_REFERENCE = 1u << 2,
};

/* A descriptor pin is also the shape of an instance pin. Descriptor fields
 * are templates; graph_add_node() copies them and assigns id/default storage. */
typedef struct
{
    const char *name;
    pin_type type;
    const char *type_name;
    pin_kind kind;
    uint64_t id;
    pin_container container;
    pin_value default_value;
    uint64_t split_parent;
    unsigned flags;
    int group;
} pin_spec;

#endif
