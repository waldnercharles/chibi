#include "types/record.h"

#include "support/arena.h"
#include "types/catalog.h"

#include <string.h>

_Static_assert(sizeof(pin_record) % _Alignof(type_field) == 0, "record fields must be aligned");

static size_t field_size(pin_type type)
{
    switch (type) {
        case PIN_STR: return sizeof(const char *);
        case PIN_I32: return sizeof(int32_t);
        case PIN_BOOL: return sizeof(bool);
        default: return 0;
    }
}
static size_t field_align(pin_type type)
{
    switch (type) {
        case PIN_STR: return _Alignof(const char *);
        case PIN_I32: return _Alignof(int32_t);
        case PIN_BOOL: return _Alignof(bool);
        default: return 0;
    }
}
static bool add(size_t *total, size_t bytes)
{
    if (bytes > SIZE_MAX - *total) {
        return false;
    }
    *total += bytes;
    return true;
}
static bool align(size_t *size, size_t alignment)
{
    size_t remainder = *size % alignment;
    return !remainder || add(size, alignment - remainder);
}
static char *copy_name(char **at, const char *source)
{
    char *name = *at;
    size_t bytes = strlen(source) + 1;
    memcpy(name, source, bytes);
    *at += bytes;
    return name;
}
static bool shape(
    const char *name,
    uint64_t id,
    const type_field *fields,
    int count,
    size_t *out_bytes,
    size_t *out_size
)
{
    if (!name || !*name || !id || !fields || count <= 0 ||
        (size_t)count > (SIZE_MAX - sizeof(pin_record)) / sizeof(type_field)) {
        return false;
    }
    size_t bytes = sizeof(pin_record) + (size_t)count * sizeof(type_field);
    size_t size = 0, alignment = 1;
    if (!add(&bytes, strlen(name) + 1)) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        const type_field *field = &fields[i];
        size_t width = field_size(field->type), step = field_align(field->type);
        if (!field->name || !*field->name || !width ||
            !add(&bytes, strlen(field->name) + 1) || !align(&size, step) ||
            !add(&size, width)) {
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(field->name, fields[j].name)) {
                return false;
            }
        }
        if (step > alignment) {
            alignment = step;
        }
    }
    if (!align(&size, alignment)) {
        return false;
    }
    *out_bytes = bytes;
    *out_size = size;
    return true;
}
static const pin_record *initialize(
    void *memory,
    size_t bytes,
    size_t size,
    const char *name,
    uint64_t id,
    const type_field *fields,
    int count
)
{
    pin_record *record = memory;
    type_field *owned = (type_field *)(record + 1);
    char *text = (char *)(owned + count);
    *record = (pin_record){ .type = { .name = copy_name(&text, name),
                                      .size = size,
                                      .fields = owned,
                                      .field_count = count },
                            .id = id,
                            .bytes = bytes };
    size_t offset = 0;
    for (int i = 0; i < count; i++) {
        size_t step = field_align(fields[i].type);
        size_t remainder = offset % step;
        offset += remainder ? step - remainder : 0;
        owned[i] = (type_field){ .name = copy_name(&text, fields[i].name),
                                 .type = fields[i].type,
                                 .offset = offset,
                                 .size = field_size(fields[i].type) };
        offset += owned[i].size;
    }
    record->layout = type_layout_hash(&record->type);
    return record;
}
const pin_record *pin_record_init(
    void *memory,
    size_t capacity,
    const char *name,
    uint64_t id,
    const type_field *fields,
    int count
)
{
    size_t bytes, size;
    if (!memory || (uintptr_t)memory % _Alignof(pin_record) ||
        !shape(name, id, fields, count, &bytes, &size) || bytes != capacity) {
        return NULL;
    }
    return initialize(memory, bytes, size, name, id, fields, count);
}
const pin_record *pin_record_create(
    arena *storage,
    const char *name,
    uint64_t id,
    const type_field *fields,
    int count
)
{
    size_t bytes, size;
    if (!storage || !shape(name, id, fields, count, &bytes, &size)) {
        return NULL;
    }
    return initialize(arena_alloc(storage, bytes), bytes, size, name, id, fields, count);
}
const pin_record *pin_record_snapshot(arena *storage, const pin_record *record)
{
    if (!record) {
        return NULL;
    }
    if (!storage || !pin_record_valid(record)) {
        return NULL;
    }
    if (arena_contains(storage, record)) {
        return record;
    }
    return pin_record_create(
        storage,
        record->type.name,
        record->id,
        record->type.fields,
        record->type.field_count
    );
}
bool pin_record_valid(const pin_record *record)
{
    if (!record || !record->id || !record->type.name || !*record->type.name ||
        !record->type.fields || record->type.field_count <= 0 || record->type.alias_of) {
        return false;
    }
    size_t size = 0, alignment = 1;
    if ((size_t)record->type.field_count >
        (SIZE_MAX - sizeof(pin_record)) / sizeof(type_field)) {
        return false;
    }
    size_t bytes = sizeof(pin_record) +
                   (size_t)record->type.field_count * sizeof(type_field);
    const type_field *fields = (const type_field *)(record + 1);
    if (record->type.fields != fields) {
        return false;
    }
    const char *text = (const char *)(fields + record->type.field_count);
    if (record->type.name != text) {
        return false;
    }
    text += strlen(text) + 1;
    if (!add(&bytes, strlen(record->type.name) + 1)) {
        return false;
    }
    for (int i = 0; i < record->type.field_count; i++) {
        const type_field *field = &record->type.fields[i];
        size_t width = field_size(field->type), step = field_align(field->type);
        if (field->name != text || !*field->name || !width || !align(&size, step) ||
            field->offset != size || field->size != width || !add(&size, width) ||
            !add(&bytes, strlen(field->name) + 1) || field->type_name ||
            field->description || field->color || field->unit || field->min ||
            field->max || field->range_contract || field->range_default) {
            return false;
        }
        text += strlen(text) + 1;
        for (int j = 0; j < i; j++) {
            if (!strcmp(field->name, record->type.fields[j].name)) {
                return false;
            }
        }
        if (step > alignment) {
            alignment = step;
        }
    }
    return bytes == record->bytes && align(&size, alignment) &&
           size == record->type.size &&
           record->layout == type_layout_hash(&record->type);
}
bool pin_record_same(const pin_record *a, const pin_record *b)
{
    if (a == b) {
        return true;
    }
    if (!a || !b) {
        return false;
    }
    return a->id == b->id && a->layout == b->layout &&
           !strcmp(a->type.name, b->type.name);
}
const type_desc *pin_value_type(const pin_value *value)
{
    if (value->type != PIN_STRUCT) {
        return NULL;
    }
    if (value->record) {
        return value->type_name && !strcmp(value->type_name, value->record->type.name)
                 ? &value->record->type
                 : NULL;
    }
    return type_catalog_find(value->type_name);
}
