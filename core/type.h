#ifndef CBLUEPRINT_TYPE_H
#define CBLUEPRINT_TYPE_H

#include "pin.h"

#include <stddef.h>

typedef struct
{
    const char *name;
    pin_type type;
    size_t offset;
    size_t size; /* byte size of the field (needed to copy PIN_STRUCT fields) */
    const char *type_name; /* C type spelling, e.g. "float", "Vec3", "const char *" */
} type_field;

typedef struct
{
    const char *name;
    size_t size;
    const type_field *fields;
    int field_count;
} type_desc;

/* The only two functions that touch raw struct bytes. */
void pin_write_field(void *base, const type_field *f, pin_value v);
void pin_read_field(const void *base, const type_field *f, pin_value *out);

#endif
