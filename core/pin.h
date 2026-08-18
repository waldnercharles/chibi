#ifndef CBLUEPRINT_PIN_H
#define CBLUEPRINT_PIN_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    PIN_FLOW, /* reserved, unused in v0.1 */
    PIN_BOOL,
    PIN_I32,
    PIN_U32,
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
        int32_t i;
        uint32_t u;
        float f;
        double d;
        const char *str; /* PIN_STR */
        void *ptr;       /* PIN_STRUCT, points at the instance */
        int32_t e;       /* PIN_ENUM */
    };
    const char *type_name; /* set for PIN_STRUCT / PIN_ENUM */
} pin_value;

typedef struct
{
    const char *name;
    pin_type type;
    const char *type_name; /* C type spelling; required match for PIN_STRUCT / PIN_ENUM */
} pin_spec;

#endif
