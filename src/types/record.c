#include "types/record.h"

#include "support/arena.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/scalar.h"
#include "types/schema.h"

#include <string.h>

_Static_assert(sizeof(pin_record) % _Alignof(type_field) == 0, "record fields must be aligned");

static size_t field_size(const type_field *field)
{
    pin_type type = field->type;
    if ((type >= PIN_BOOL && type <= PIN_F64) || type == PIN_STR ||
        type == PIN_NAME || type == PIN_ENUM || type == PIN_HANDLE ||
        type == PIN_POINTER || type == PIN_QUERY) {
        return behavior_scalar_size(type);
    }
    const type_desc *nested = type == PIN_STRUCT ? type_catalog_find(field->type_name)
                                                 : NULL;
    if (!type_is_plain(nested)) {
        return 0;
    }
    for (int i = 0; i < nested->field_count; i++) {
        if (!field_size(&nested->fields[i])) {
            return 0;
        }
    }
    return nested->size;
}
static size_t field_align(const type_field *field)
{
    if (!field_size(field)) {
        return 0;
    }
    if (field->type == PIN_STRUCT) {
        const type_desc *type = type_catalog_find(field->type_name);
        size_t alignment = 1;
        for (int i = 0; type && i < type->field_count; i++) {
            size_t step = field_align(&type->fields[i]);
            if (step > alignment) {
                alignment = step;
            }
        }
        return alignment;
    }
    switch (field->type) {
#define ALIGN(pin, type, member)                                               \
    case pin: return _Alignof(type);
        BEHAVIOR_SCALARS(ALIGN)
#undef ALIGN
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
        size_t width = field_size(field), step = field_align(field);
        if (!field->name || !*field->name || !width ||
            !add(&bytes, strlen(field->name) + 1) || !align(&size, step) ||
            !add(&size, width)) {
            return false;
        }
        if ((field->type_name && !add(&bytes, strlen(field->type_name) + 1)) ||
            (field->unit && !add(&bytes, strlen(field->unit) + 1))) {
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
        size_t step = field_align(&fields[i]);
        size_t remainder = offset % step;
        offset += remainder ? step - remainder : 0;
        /* Validation requires the name, type name and unit in this order, so
         * each is copied in its own statement, not within one initializer. */
        const char *name = copy_name(&text, fields[i].name);
        const char *type_name = fields[i].type_name
                                  ? copy_name(&text, fields[i].type_name)
                                  : NULL;
        const char *unit = fields[i].unit ? copy_name(&text, fields[i].unit) : NULL;
        owned[i] = (type_field){ .name = name,
                                 .type = fields[i].type,
                                 .text_descriptor = fields[i].type == PIN_STR,
                                 .offset = offset,
                                 .size = field_size(&fields[i]),
                                 .type_name = type_name,
                                 .unit = unit };
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
/* A valid descriptor is exactly what initialize writes, so its copy is the
 * same bytes with every text pointer moved into the copy. */
static const char *moved(const char *text, const char *from, char *to)
{
    return text ? to + (text - from) : NULL;
}
static const pin_record *copy_record(arena *storage, const pin_record *record)
{
    pin_record *copy = arena_alloc(storage, record->bytes);
    memcpy(copy, record, record->bytes);
    const char *from = (const char *)record;
    char *to = (char *)copy;
    type_field *fields = (type_field *)(copy + 1);
    copy->type.fields = fields;
    copy->type.name = moved(record->type.name, from, to);
    for (int i = 0; i < record->type.field_count; i++) {
        const type_field *field = &record->type.fields[i];
        fields[i].name = moved(field->name, from, to);
        fields[i].type_name = moved(field->type_name, from, to);
        fields[i].unit = moved(field->unit, from, to);
    }
    return copy;
}
/* Whether two descriptors of one identity also hold the same names, type names
 * and units, which follow their fields in that order. */
static bool same_text(const pin_record *a, const pin_record *b)
{
    size_t start = sizeof(pin_record) + (size_t)a->type.field_count * sizeof(type_field);
    return a->bytes == b->bytes && a->type.field_count == b->type.field_count &&
           !memcmp((const char *)a + start, (const char *)b + start, a->bytes - start);
}
const pin_record *pin_record_snapshot(arena *storage, const pin_record *record)
{
    if (!record) {
        return NULL;
    }
    if (!storage) {
        return NULL;
    }
    /* Copies of one record into one arena repeat, so the arena remembers its
     * last copy. */
    const pin_record *copy = arena_copy_find(storage, record);
    if (copy && pin_record_same(copy, record) && same_text(copy, record)) {
        return copy;
    }
    if (arena_contains(storage, record)) {
        return record;
    }
    copy = copy_record(storage, record);
    arena_copy_remember(storage, record, copy);
    return copy;
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
        size_t width = field_size(field), step = field_align(field);
        if (field->text_descriptor != (field->type == PIN_STR) ||
            field->name != text || !*field->name || !width ||
            !align(&size, step) || field->offset != size || field->size != width ||
            !add(&size, width) || !add(&bytes, strlen(field->name) + 1) ||
            field->description || field->color || field->min || field->max ||
            field->range_contract || field->range_default) {
            return false;
        }
        text += strlen(text) + 1;
        if (field->type_name) {
            if (field->type_name != text || !add(&bytes, strlen(text) + 1)) {
                return false;
            }
            text += strlen(text) + 1;
        }
        if (field->unit) {
            if (field->unit != text || !add(&bytes, strlen(text) + 1)) {
                return false;
            }
            text += strlen(text) + 1;
        }
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
bool pin_record_current(const pin_record *record)
{
    for (int i = 0; i < record->type.field_count; i++) {
        const type_field *field = &record->type.fields[i];
        if (field->type == PIN_STRUCT && field_size(field) != field->size) {
            return false;
        }
    }
    return true;
}
bool pin_value_record_current(const pin_value *value)
{
    return value->record && value->record->layout &&
           (value->schema
                ? pin_schema_layout_type(value->schema, &value->record->type) ==
                      value->record->layout
                : pin_record_current(value->record));
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
        const char *name = value->record->type.name;
        return value->type_name &&
                       (value->type_name == name || !strcmp(value->type_name, name))
                 ? &value->record->type
                 : NULL;
    }
    const pin_schema *schema = value->schema;
    if (!schema &&
        (value->container == PIN_CONTAINER_ARRAY || value->container == PIN_CONTAINER_SET) &&
        value->ptr) {
        schema = ((const pin_array *)value->ptr)->schema;
    }
    return schema ? pin_schema_type(schema, value->type_name)
                  : type_catalog_find(value->type_name);
}
