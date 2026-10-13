#ifndef CHIBI_TYPE_H
#define CHIBI_TYPE_H

#include "types/value.h"

#include <stddef.h>

/* Maximum reflected struct layers visited by owning range validation. */
#define TYPE_MAX_NESTING 16

typedef struct
{
    const char *name;
    pin_type type;
    size_t offset;
    size_t size; /* byte size of the field (needed to copy PIN_STRUCT fields) */
    const char *type_name; /* C spelling, or a nominal handle type such as "ecs.entity". */
    const char *description; /* First paragraph of the field's comment, or NULL. */
    bool color;              /* Packed RGBA presentation for PIN_U32. */
    const char *unit;        /* Numeric presentation unit, or NULL. */
    double min, max;         /* Numeric range; unbounded when equal. */
    bool range_contract; /* Clamp on owning writes; false means slider hint. */
    double range_default; /* NaN replacement, clamped too; zero when unspecified. */
} type_field;

typedef struct type_desc
{
    const char *name;
    size_t size;
    const type_field *fields;
    int field_count;
    const char *alias_of; /* optional underlying nominal type name */
} type_desc;

/* Payload-free presence marker. Its one-byte backing is canonical zero. */
extern const type_desc TYPE_MARKER;

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

/* Hash the reflected storage layout and numeric write contracts. */
uint64_t type_layout_hash(const type_desc *type);
/* Nested structs resolve only from the supplied descriptors. Missing or cyclic
 * layouts return zero; no process-wide catalog lookup is performed. */
uint64_t type_layout_hash_explicit(const type_desc *type, const type_desc *const *types, int count);
/* Every field is inline and contains no managed string or container. */
bool type_is_plain(const type_desc *type);
bool type_value_is_finite(const type_desc *type, const void *value);
/* Validate range metadata before publishing a descriptor. */
bool type_field_range_valid(const type_field *);
/* Canonicalize a typed scalar without rounding a bounded floating value. */
pin_value pin_clamp_field(const type_field *, pin_value);
/* Decode a floating input into bounded storage: clamp before narrowing or
 * integer conversion, so out-of-storage inputs never overflow a cast. */
pin_value pin_field_number(const type_field *, double);
/* Owning aggregate publication boundary, including nested fields. Direct C
 * pointer writes are unvalidated until their owner calls this or commits them. */
void type_validate_ranges(const type_desc *, void *);

/* Read and write reflected fields. */
void pin_write_field(void *base, const type_field *f, pin_value v);
void pin_read_field(const void *base, const type_field *f, pin_value *out);
const type_field *pin_type_field_path(const type_desc *type, const char *path);
int pin_read_field_path(const void *base, const type_desc *type, const char *path, pin_value *out);

#endif
