#ifndef CHIBI_NATIVE_SCHEMA_H
#define CHIBI_NATIVE_SCHEMA_H

#include "support/arena.h"
#include "types/type.h"

/* An exact immutable closure of native reflected layouts. One allocation owns
 * every descriptor, field and string, retaining native offsets and text ABI. */
typedef struct pin_schema
{
    size_t bytes;
    int count;
    const type_desc *types;
    const type_desc *const *lookup;
} pin_schema;

const pin_schema *pin_schema_create(arena *, const type_desc *, type_resolver);
/* Authored roots already own their layout. Retain only their native children. */
const pin_schema *pin_schema_create_record(arena *, const pin_record *);
const pin_schema *pin_schema_snapshot(arena *, const pin_schema *);
const type_desc *pin_schema_type(const pin_schema *, const char *name);
uint64_t pin_schema_layout(const pin_schema *, const char *name);
uint64_t pin_schema_layout_type(const pin_schema *, const type_desc *);
type_resolver pin_schema_resolver(const pin_schema *);
/* data has already been snapshotted using this owned schema. */
void pin_schema_trace_struct(arena *, void *data, const pin_schema *, const type_desc *);
void pin_schema_trace_fields(const pin_schema *, const type_desc *, const void *, arena_marker, void *);
/* Retain native record schemas at escaping boundaries such as query rows. */
int pin_value_snapshot_owned_to_arena(arena *, pin_value *, const pin_value *);

#endif
