#ifndef CHIBI_RECORD_H
#define CHIBI_RECORD_H

#include "types/type.h"

typedef struct arena arena;

/* Immutable owned layout. Authored create/init uses a flat natural layout;
 * snapshots close nested layouts and native carriers preserve exact ABI layout.
 * Each closure is one allocation, including names and child descriptors. */
struct pin_record
{
    type_desc type;
    uint64_t id;
    uint64_t layout;
    size_t bytes;
    bool native; /* Exact native identity; never an authored wire schema. */
    bool frozen; /* Carries exact owned child layouts, including authored roots. */
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
/* Reuse only a closure already owned by storage; otherwise copy it once.
 * Descriptors are immutable once created, so callers validate a record where
 * it enters (prepared code, value documents), not on every copy. */
/* Maximum allocation needed for a snapshot; zero means invalid metadata. */
size_t pin_record_snapshot_bytes(const pin_record *);
const pin_record *pin_record_snapshot(arena *storage, const pin_record *);
/* Freeze exact native offsets, sizes and string representation together with
 * all nested value layouts. The closure owns its names and child descriptors. */
const pin_record *pin_record_from_type(arena *, const type_desc *);
/* The resolver and source descriptors are borrowed and must stay unchanged for
 * this call. The closure owns every resolved child, alias and string. */
const pin_record *pin_record_from_type_resolved(arena *, const type_desc *, type_resolver);
/* Resolve only within a frozen closure. The result borrows the record, never
 * consults a catalog, and rejects conflicting layouts with the same name.
 * NULL or non-frozen records resolve no names. */
type_resolver pin_record_resolver(const pin_record *);
/* Portable native restoration requires the same currently registered layout. */
bool pin_record_native_current(const pin_record *);
/* Portable authored schemas still name their nested native layouts. */
bool pin_record_fields_current(const pin_record *);
/* Compare carried authored identity or compatible frozen/current native layout.
 * Callers compare storage type and container separately. */
bool pin_value_layout_same(const pin_value *, const pin_value *);
bool pin_record_valid(const pin_record *);
/* Frozen layouts validate their owned closure after catalog retirement. Flat
 * authored declarations still require their original nested catalog layout. */
bool pin_record_current(const pin_record *);
/* A retained native child closure validates the original authored layout. */
bool pin_value_record_current(const pin_value *);
bool pin_record_same(const pin_record *, const pin_record *);
/* Values resolve a carried authored/native layout before catalog lookup.
 * A conflicting carried/name identity never falls back to the catalog. */
const type_desc *pin_value_type(const pin_value *);

#endif
