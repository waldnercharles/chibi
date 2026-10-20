#ifndef CHIBI_RECORD_H
#define CHIBI_RECORD_H

#include "types/type.h"

typedef struct arena arena;

/* Immutable flat authored layout. One allocation owns the descriptor, fields
 * and names. Nominal identity is separate from storage compatibility and source
 * defaults; no process-wide registration or current-generation lookup occurs. */
struct pin_record
{
    type_desc type;
    uint64_t id;
    uint64_t layout;
    size_t bytes;
};

/* Fields supply unique nonempty names and fixed scalar/handle/enum/string types,
 * or registered plain nested layouts. Nominal names and units are owned here.
 * Natural target offsets, sizes and the layout hash are computed here. Invalid
 * input returns NULL before allocation. The result belongs to storage. */
const pin_record *pin_record_create(
    arena *storage,
    const char *name,
    uint64_t id,
    const type_field *fields,
    int count
);
/* Initialize an exact-size, suitably aligned caller-owned allocation. The
 * caller budgets storage and validation work; this never allocates. */
const pin_record *pin_record_init(
    void *memory,
    size_t capacity,
    const char *name,
    uint64_t id,
    const type_field *fields,
    int count
);
/* Reuse only a closure already owned by storage; otherwise copy it once. */
const pin_record *pin_record_snapshot(arena *storage, const pin_record *);
bool pin_record_valid(const pin_record *);
bool pin_record_same(const pin_record *, const pin_record *);
/* Authored values resolve their carried layout; native values retain catalog
 * lookup. A conflicting carried/name identity never falls back to the catalog. */
const type_desc *pin_value_type(const pin_value *);

#endif
