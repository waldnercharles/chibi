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
    bool text_descriptor; /* PIN_STR: authored pin_string pointer, otherwise native C string. */
    const char *unit;    /* Numeric presentation unit, or NULL. */
    double min, max;     /* Numeric range; unbounded when equal. */
    bool range_contract; /* Clamp on owning writes; false means slider hint. */
    double range_default; /* NaN replacement, clamped too; zero when unspecified. */
    const struct type_desc *layout; /* Frozen nested struct or array element layout. */
    pin_container container; /* SINGLE inline value, ARRAY stores a pin_array pointer. */
} type_field;

typedef struct type_desc
{
    const char *name;
    size_t size;
    const type_field *fields;
    int field_count;
    const char *alias_of;          /* optional underlying nominal type name */
    const struct type_desc *alias; /* Frozen alias target, when owned. */
    const pin_record *carrier; /* Internal owned layout carrier, otherwise NULL. */
} type_desc;

/* Payload-free presence marker. Its one-byte backing is canonical zero. */
extern const type_desc TYPE_MARKER;

/* Borrowed for one operation; never retained by values or arena tracers.
 * A null callback selects the global nominal type catalog. */
typedef struct
{
    const type_desc *(*find)(void *user, const char *name);
    void *user;
} type_resolver;
const type_desc *type_resolve(type_resolver, const char *name);
/* Carried layouts take precedence over a potentially newer catalog. */
const type_desc *type_field_type(const type_field *, type_resolver);
const type_desc *type_alias_type(const type_desc *, type_resolver);
/* An alias without fields of its own lays values out as its target does. A
 * native alias declares its target's fields itself. */
bool type_alias_layout(const type_desc *);

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
/* Whether a record reaches collections, unions or pointers through its own
 * fields or nested records. An unresolved layout may reach anything. */
bool type_holds_references(const type_desc *type);
bool type_value_is_finite(const type_desc *type, const void *value);
/* The field's type as the language spells it: a scalar's language name,
 * otherwise its nominal name. */
const char *type_field_type_name(const type_field *);
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
void type_validate_ranges_resolved(const type_desc *, void *, type_resolver);

/* Native string reads and all string writes copy into storage (NULL selects
 * owned heap memory). Authored descriptor and aggregate reads borrow fields.
 * Errors leave output/field unchanged; native C string fields reject embedded
 * NUL, authored fields preserve bytes. */
const char *pin_write_field(arena *, void *base, const type_field *f, pin_value v);
/* Whole native records may supply their owner's descriptor without registering
 * it globally. Destination is published only after every field is valid. */
const char *pin_write_struct(arena *, void *destination, const type_desc *, pin_value);
/* Custom resolution supports ordinary native owners. Collectable owners need
 * persistent trace layouts and reject a custom borrowed resolver. */
const char *pin_write_field_resolved(arena *, void *base, const type_field *, pin_value, type_resolver);
const char *pin_write_struct_resolved(arena *, void *destination, const type_desc *, pin_value, type_resolver);
const char *pin_read_field(arena *, const void *base, const type_field *f, pin_value *out);
const type_field *pin_type_field_path(const type_desc *type, const char *path);
int pin_read_field_path(arena *, const void *base, const type_desc *type, const char *path, pin_value *out);

#endif
