#ifndef CHIBI_TYPE_H
#define CHIBI_TYPE_H

#include "graph/model/pin.h"

#include <stddef.h>

typedef struct
{
    const char *name;
    pin_type type;
    size_t offset;
    size_t size; /* byte size of the field (needed to copy PIN_STRUCT fields) */
    const char *type_name; /* C spelling, or a nominal handle type such as "ecs.entity". */
} type_field;

typedef struct
{
    const char *name;
    size_t size;
    const type_field *fields;
    int field_count;
    const char *alias_of; /* optional underlying nominal type name */
} type_desc;

typedef struct
{
    int value;
    const char *name;
    const char *label;
} enum_value_desc;

typedef struct
{
    const char *name;
    const enum_value_desc *values;
    int value_count;
} enum_desc;

/* Hash the reflected storage layout on the current target. */
uint64_t type_layout_hash(const type_desc *type);
/* Every field is inline and contains no managed string or container. */
bool type_is_plain(const type_desc *type);

/* Read and write reflected fields. */
void pin_write_field(void *base, const type_field *f, pin_value v);
void pin_read_field(const void *base, const type_field *f, pin_value *out);
const type_field *pin_type_field_path(const type_desc *type, const char *path);
int pin_read_field_path(const void *base, const type_desc *type, const char *path, pin_value *out);
int pin_write_field_path(void *base, const type_desc *type, const char *path, pin_value value);

#endif
