#ifndef CHIBI_VALUE_ARRAY_H
#define CHIBI_VALUE_ARRAY_H

#include "types/type.h"

#include <stddef.h>

typedef struct arena arena;

typedef struct pin_array
{
    pin_type element_type;
    const char *type_name;
    const pin_record *record; /* Shared immutable element layout, including empty arrays. */
    int count;
    int capacity;
    pin_value *items;
    /* Exactly one representation: stride > 0 stores fixed-layout values in
     * data, with no per-element headers; stride == 0 uses items. */
    void *data;
    size_t stride;
    /* Optional index into ordered items. Owned by the same arena as the
     * snapshot; owned runtime sets update it on append. */
    int *buckets;
    unsigned bucket_mask;
    const pin_schema *schema; /* Owned native element layouts, including empty arrays. */
} pin_array;

bool pin_array_valid(const pin_array *);
size_t pin_array_element_stride(pin_type, const char *, const pin_record *);
size_t pin_array_storage_bytes(const pin_array *);
/* Borrowed element/payload. Indices must already be checked by the caller. */
pin_value pin_array_get(const pin_array *, int index);
void *pin_array_data(const pin_array *, int index);
/* Assign an already-owned compatible element. This does not retain pointers. */
int pin_array_set(pin_array *, int index, const pin_value *);
void pin_array_swap(pin_array *, int first, int second);

/* Small and aggregate collections retain flat storage. Indexes never change
 * iteration order or first-match equality semantics. */
void pin_array_index(arena *storage, pin_array *array);
int pin_array_find(const pin_array *array, const pin_value *value);
int pin_set_unique(arena *storage, pin_value *out, const pin_value *items, int count);
/* Mutate an exclusively owned set. Capacity grows geometrically; the arena
 * retains replaced snapshots until its owner resets it. */
int pin_set_add_owned(arena *storage, pin_array **set, const pin_value *item);

/* Arrays own their item storage. These heap/clone helpers borrow both struct
 * payloads and authored layouts; use snapshots for detached owned values. */
pin_array *pin_array_new(pin_type element_type, const char *type_name, int capacity);
pin_array *pin_array_clone_to_arena(arena *a, const pin_array *src);
/* Internal snapshot construction: header/name are owned, items uninitialized.
 * Fill every item before exposing the array; abandon it if any snapshot fails. */
pin_array *pin_array_snapshot_allocate(arena *a, pin_type type, const char *name, int count);
/* Uninitialized items with a copied/shared source element layout. */
pin_array *pin_array_snapshot_like(arena *, const pin_array *, int count);
int pin_array_snapshot_set(arena *, pin_array *, int index, const pin_value *);
int pin_array_snapshot_copy(arena *, pin_array *out, int to, const pin_array *items, int from, int count);
/* Internal array construction. Validate the destination layout's ownership once
 * per range, and that each original source layout matches it. Items already
 * copied on failure must be abandoned with the unpublished destination array. */
int pin_array_snapshot_items(arena *, const pin_record *, pin_value *out, const pin_value *items, int count);
/* As above for an array the arena already owns, whose layout ownership was
 * established when the array was made, so no ownership walk repeats. */
int pin_array_snapshot_owned_items(
    arena *,
    const pin_record *,
    pin_value *out,
    const pin_value *items,
    int count
);
/* Fixed-size typed zero values, owned by the arena. */
pin_array *pin_array_allocate(arena *a, pin_type type, const char *name, int count);
/* Deep snapshot, including registered struct fields and all nominal names. */
pin_array *pin_array_snapshot_to_arena(arena *a, const pin_array *src);
int pin_value_snapshot_to_arena(arena *a, pin_value *dst, const pin_value *src);
/* Retain managed fields in an already copied struct. Nested structs are inline;
 * only their strings need separate storage. Publish data only on success. */
int pin_struct_snapshot_fields(arena *a, const type_desc *type, void *data);
int pin_struct_snapshot_fields_resolved(arena *, const type_desc *, void *, type_resolver);
void pin_array_free(pin_array *array);
int pin_array_reserve(pin_array *array, int capacity);
/* Grows an exclusively owned arena array to hold count items, doubling its
 * capacity like Odin's dynamic arrays so appends cost amortized O(1). Items
 * move without copying their payloads, and a packed array stays packed; the
 * old buffer becomes unreachable. The caller writes the slots from the
 * array's count up to `count`. */
int pin_array_reserve_owned(arena *a, pin_array *array, int count);
int pin_array_push(pin_array *array, const pin_value *value);
int pin_array_equal(const pin_array *a, const pin_array *b);

/* Copy and destruction dispatch. `dst` owns copied array and string storage;
 * struct payloads/layouts and handles remain borrowed. */
int pin_value_copy(pin_value *dst, const pin_value *src);
int pin_value_copy_to_arena(arena *a, pin_value *dst, const pin_value *src);
void pin_value_destroy(pin_value *value);
int pin_value_equal(const pin_value *a, const pin_value *b);

#endif
