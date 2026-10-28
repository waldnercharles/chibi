#include "program_internal.h"
#include "types/array.h"
#include "types/record.h"
#include "types/scalar.h"

#include <string.h>

/* Converts a plain number to a type it widens to implicitly, rounding once as
 * an assignment does. */
static bool widen(pin_value value, pin_type type, pin_value *out)
{
    if (value.type_name || !behavior_scalar_number(value.type) ||
        !behavior_scalar_number(type) || !behavior_scalar_implicit(value.type, type)) {
        return false;
    }
    if (value.type == type) {
        *out = value;
        return true;
    }
    if (value.type == PIN_F32) {
        *out = (pin_value){ .type = PIN_F64, .d = value.f };
        return true;
    }
    if (type == PIN_F32) {
        uint64_t bits = behavior_scalar_bits(value);
        float f = behavior_scalar_signed(value.type) ? (float)(int64_t)bits
                                                     : (float)bits;
        *out = (pin_value){ .type = PIN_F32, .f = f };
        return true;
    }
    behavior_scalar_cast(value, type, BEHAVIOR_CAST_EXACT, out);
    return true;
}
static const type_field *record_field(const pin_record *record, const char *name)
{
    for (int i = 0; i < record->type.field_count; i++) {
        if (!strcmp(record->type.fields[i].name, name)) {
            return &record->type.fields[i];
        }
    }
    return NULL;
}
/* Writes each field of to that from also declares into data. Nested record
 * fields have native layouts (pin_record_create), so they copy whole. A field
 * only to declares keeps data's value, which needs defaults: without them, as
 * for an item of an array, a new field cannot be migrated. */
static bool migrate_fields(
    arena *storage,
    const pin_record *from,
    const void *old,
    const pin_record *to,
    void *data,
    bool defaults
)
{
    for (int i = 0; i < to->type.field_count; i++) {
        const type_field *field = &to->type.fields[i];
        const type_field *prior = record_field(from, field->name);
        if (!prior) {
            if (!defaults) {
                return false;
            }
            continue;
        }
        pin_value value;
        if (pin_read_field(storage, old, prior, &value)) {
            return false;
        }
        bool same = prior->type == field->type && prior->size == field->size &&
                    behavior_string_equal(prior->type_name, field->type_name);
        if ((!same && !widen(value, field->type, &value)) ||
            pin_write_field(storage, data, field, value)) {
            return false;
        }
    }
    return true;
}
static bool migrate_record(arena *storage, pin_value old, pin_value shape, pin_value *out)
{
    if (!old.ptr || old.record->id != shape.record->id) {
        return false;
    }
    size_t size = shape.record->type.size;
    void *data = arena_alloc(storage, size);
    if (shape.ptr) {
        memcpy(data, shape.ptr, size);
    } else {
        memset(data, 0, size);
    }
    if (!migrate_fields(storage, old.record, old.ptr, shape.record, data, shape.ptr != NULL)) {
        return false;
    }
    *out = shape;
    out->ptr = data;
    return true;
}
/* Items of a nested container are refused: an item shape carries no
 * container of its own. */
static bool migrate_array(arena *storage, pin_value old, pin_value shape, pin_value *out)
{
    const pin_array *items = old.ptr, *layout = shape.ptr;
    if (!items || !layout) {
        return false;
    }
    pin_array *array = pin_array_snapshot_like(storage, layout, items->count);
    pin_value item = { .type = layout->element_type,
                       .type_name = layout->type_name,
                       .record = layout->record };
    for (int i = 0; i < items->count; i++) {
        pin_value value;
        if (!program_migrate_value(storage, pin_array_get(items, i), item, &value) ||
            pin_array_snapshot_set(storage, array, i, &value)) {
            return false;
        }
    }
    *out = shape;
    out->ptr = array;
    return true;
}
bool program_migrate_value(arena *storage, pin_value old, pin_value shape, pin_value *out)
{
    if (program_same_type(old, shape)) {
        *out = old;
        return true;
    }
    if (old.container != shape.container) {
        return false;
    }
    if (old.container == PIN_CONTAINER_ARRAY) {
        return migrate_array(storage, old, shape, out);
    }
    if (old.container != PIN_CONTAINER_SINGLE) {
        return false;
    }
    if (old.type == PIN_STRUCT && shape.type == PIN_STRUCT && old.record &&
        shape.record) {
        return migrate_record(storage, old, shape, out);
    }
    return widen(old, shape.type, out);
}
