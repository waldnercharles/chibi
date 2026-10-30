#include "types/record.h"

#include "support/arena.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/scalar.h"
#include "types/schema.h"

#include <string.h>

_Static_assert(sizeof(pin_record) % _Alignof(type_field) == 0, "record fields must be aligned");

static size_t field_size_at(const type_field *field, int depth)
{
    if (depth >= TYPE_MAX_NESTING || (field->container != PIN_CONTAINER_SINGLE &&
                                      field->container != PIN_CONTAINER_ARRAY)) {
        return 0;
    }
    pin_type type = field->type;
    if (field->container == PIN_CONTAINER_ARRAY) {
        type_field element = *field;
        element.container = PIN_CONTAINER_SINGLE;
        return field_size_at(&element, depth) ? sizeof(pin_array *) : 0;
    }
    if ((type >= PIN_BOOL && type <= PIN_F64) || type == PIN_STR ||
        type == PIN_NAME || type == PIN_ENUM || type == PIN_HANDLE ||
        type == PIN_POINTER || type == PIN_QUERY || type == PIN_UNION) {
        return behavior_scalar_size(type);
    }
    const type_desc *nested = type == PIN_STRUCT
                                ? type_field_type(field, (type_resolver){ 0 })
                                : NULL;
    if (!nested || !nested->name || !field->type_name ||
        strcmp(nested->name, field->type_name) || !nested->size ||
        nested->field_count <= 0 || !nested->fields ||
        (!field->layout && !type_is_plain(nested))) {
        return 0;
    }
    for (int i = 0; i < nested->field_count; i++) {
        if (!field_size_at(&nested->fields[i], depth + 1)) {
            return 0;
        }
    }
    return nested->size;
}
static size_t field_size(const type_field *field)
{
    return field_size_at(field, 0);
}
static size_t field_align_at(const type_field *field, int depth)
{
    if (depth >= TYPE_MAX_NESTING || !field_size_at(field, depth)) {
        return 0;
    }
    if (field->container == PIN_CONTAINER_ARRAY) {
        return _Alignof(pin_array *);
    }
    if (field->type == PIN_STRUCT) {
        const type_desc *type = type_field_type(field, (type_resolver){ 0 });
        size_t alignment = 1;
        for (int i = 0; type && i < type->field_count; i++) {
            size_t step = field_align_at(&type->fields[i], depth + 1);
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
static size_t field_align(const type_field *field)
{
    return field_align_at(field, 0);
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
static bool layout_work(size_t *used, size_t amount)
{
    if (*used > PIN_RECORD_MAX_LAYOUT_WORK ||
        amount > PIN_RECORD_MAX_LAYOUT_WORK - *used) {
        return false;
    }
    *used += amount;
    return true;
}
static bool layout_bytes(size_t *used, size_t amount)
{
    if (*used > PIN_RECORD_MAX_LAYOUT_BYTES ||
        amount > PIN_RECORD_MAX_LAYOUT_BYTES - *used) {
        return false;
    }
    *used += amount;
    return true;
}
static bool layout_text(size_t *bytes, const char *text)
{
    if (!text) {
        return true;
    }
    if (*bytes >= PIN_RECORD_MAX_LAYOUT_BYTES) {
        return false;
    }
    size_t left = PIN_RECORD_MAX_LAYOUT_BYTES - *bytes;
    size_t length = strnlen(text, left);
    return length < left && layout_bytes(bytes, length + 1);
}
static char *copy_name(char **at, const char *source)
{
    char *name = *at;
    size_t bytes = strlen(source) + 1;
    memcpy(name, source, bytes);
    *at += bytes;
    return name;
}
static bool native_field(const type_field *field)
{
    if (field->container == PIN_CONTAINER_ARRAY) {
        return field->size == sizeof(pin_array *) && !field->range_contract &&
               !field->text_descriptor &&
               ((field->type >= PIN_BOOL && field->type <= PIN_ENUM) ||
                field->type == PIN_POINTER || field->type == PIN_QUERY);
    }
    if (field->container != PIN_CONTAINER_SINGLE ||
        (field->range_contract && !type_field_range_valid(field))) {
        return false;
    }
    if (field->type == PIN_STRUCT) {
        return field->size && field->type_name;
    }
    if (field->type == PIN_ENUM) {
        return field->size && field->size <= sizeof(int32_t);
    }
    if ((field->type >= PIN_BOOL && field->type <= PIN_F64) ||
        field->type == PIN_STR || field->type == PIN_NAME ||
        field->type == PIN_UNION || field->type == PIN_POINTER ||
        field->type == PIN_HANDLE || field->type == PIN_TASK) {
        return field->size == behavior_scalar_size(field->type);
    }
    return false;
}
static bool native_size(const type_desc *type, type_resolver resolver, int depth, size_t *bytes, size_t *work)
{
    if (!type || !type->name || !*type->name || !type->size ||
        type->field_count < 0 || type->field_count > 65536 ||
        (type->field_count && !type->fields) || depth >= TYPE_MAX_NESTING ||
        !layout_work(work, 1 + (size_t)type->field_count) ||
        !align(bytes, _Alignof(pin_record)) ||
        !layout_bytes(bytes, sizeof(pin_record)) ||
        (size_t)type->field_count > SIZE_MAX / sizeof(type_field) ||
        !layout_bytes(bytes, (size_t)type->field_count * sizeof(type_field)) ||
        !layout_text(bytes, type->name) || !layout_text(bytes, type->alias_of)) {
        return false;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (!field->name || !*field->name || !native_field(field) ||
            field->offset > type->size || field->size > type->size - field->offset) {
            return false;
        }
        const char *names[] = { field->name,
                                field->type_name,
                                field->unit,
                                field->description };
        for (size_t n = 0; n < sizeof names / sizeof *names; n++) {
            if (!layout_text(bytes, names[n])) {
                return false;
            }
        }
    }
    if (type->alias_of) {
        const type_desc *alias = type_alias_type(type, resolver);
        if (!alias || alias->size != type->size ||
            !native_size(alias, resolver, depth + 1, bytes, work)) {
            return false;
        }
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->type != PIN_STRUCT) {
            continue;
        }
        const type_desc *nested = type_field_type(field, resolver);
        if (!nested || !field->type_name || !nested->name ||
            strcmp(nested->name, field->type_name) ||
            (field->container == PIN_CONTAINER_SINGLE && nested->size != field->size) ||
            !native_size(nested, resolver, depth + 1, bytes, work)) {
            return false;
        }
    }
    return true;
}
static pin_record *native_initialize(char *memory, size_t *used, const type_desc *type, type_resolver resolver)
{
    align(used, _Alignof(pin_record));
    size_t start = *used;
    pin_record *record = (pin_record *)(memory + start);
    type_field *fields = (type_field *)(record + 1);
    *used += sizeof *record + (size_t)type->field_count * sizeof *fields;
    char *text = memory + *used;
    *record = (pin_record){ .type = *type,
                            .native = !type->carrier || type->carrier->native,
                            .frozen = true };
    record->type.carrier = record;
    record->type.fields = fields;
    record->type.name = copy_name(&text, type->name);
    record->type.alias_of = type->alias_of ? copy_name(&text, type->alias_of) : NULL;
    record->type.alias = NULL;
    for (int i = 0; i < type->field_count; i++) {
        fields[i] = type->fields[i];
        fields[i].name = copy_name(&text, fields[i].name);
        if (fields[i].type_name) {
            fields[i].type_name = copy_name(&text, fields[i].type_name);
        }
        if (fields[i].unit) {
            fields[i].unit = copy_name(&text, fields[i].unit);
        }
        if (fields[i].description) {
            fields[i].description = copy_name(&text, fields[i].description);
        }
        fields[i].layout = NULL;
    }
    *used = (size_t)(text - memory);
    if (type->alias_of) {
        record->type.alias = &native_initialize(memory, used, type_alias_type(type, resolver), resolver)
                                  ->type;
    }
    for (int i = 0; i < type->field_count; i++) {
        if (fields[i].type == PIN_STRUCT) {
            fields[i].layout = &native_initialize(
                                    memory,
                                    used,
                                    type_field_type(&type->fields[i], resolver),
                                    resolver
            )
                                    ->type;
        }
    }
    record->bytes = *used - start;
    record->layout = type_layout_hash(&record->type);
    uint64_t id = UINT64_C(14695981039346656037);
    for (const unsigned char *at = (const unsigned char *)type->name; *at; at++) {
        id = (id ^ *at) * UINT64_C(1099511628211);
    }
    record->id = type->carrier ? type->carrier->id : id ? id : 1;
    return record;
}
const pin_record *pin_record_from_type(arena *storage, const type_desc *type)
{
    return pin_record_from_type_resolved(storage, type, (type_resolver){ 0 });
}
const pin_record *pin_record_from_type_resolved(arena *storage, const type_desc *type, type_resolver resolver)
{
    if (!storage) {
        return NULL;
    }
    if (type && type->carrier && type->carrier->native) {
        return pin_record_snapshot(storage, type->carrier);
    }
    size_t bytes = 0, work = 0;
    if (!native_size(type, resolver, 0, &bytes, &work)) {
        return NULL;
    }
    size_t used = 0;
    return native_initialize(arena_alloc(storage, bytes), &used, type, resolver);
}
static bool native_text(const pin_record *owner, const char *text)
{
    uintptr_t start = (uintptr_t)owner, at = (uintptr_t)text;
    return !text || (at >= start && at - start < owner->bytes &&
                     memchr(text, 0, owner->bytes - (size_t)(at - start)));
}
static bool native_child(const pin_record *owner, const type_desc *type)
{
    uintptr_t start = (uintptr_t)owner, at = (uintptr_t)type;
    return type && at >= start && at - start <= owner->bytes &&
           sizeof(pin_record) <= owner->bytes - (size_t)(at - start) &&
           type->carrier == (const pin_record *)type &&
           type->carrier->bytes <= owner->bytes - (size_t)(at - start);
}
static bool native_valid_at(const pin_record *record, int depth, size_t *work)
{
    if (!record || !record->frozen || !record->id || !record->layout ||
        record->bytes < sizeof *record || record->type.carrier != record ||
        !native_text(record, record->type.name) || !record->type.name ||
        !*record->type.name || !record->type.size || record->type.field_count < 0 ||
        (size_t)record->type.field_count >
            (record->bytes - sizeof *record) / sizeof(type_field) ||
        record->type.fields != (const type_field *)(record + 1) ||
        depth >= TYPE_MAX_NESTING || !native_text(record, record->type.alias_of) ||
        (!record->type.alias_of && record->type.alias) ||
        record->bytes > PIN_RECORD_MAX_LAYOUT_BYTES ||
        !layout_work(work, 1 + (size_t)record->type.field_count)) {
        return false;
    }
    if (record->type.alias_of &&
        (!native_child(record, record->type.alias) ||
         record->type.alias->size != record->type.size ||
         !native_valid_at(record->type.alias->carrier, depth + 1, work))) {
        return false;
    }
    for (int i = 0; i < record->type.field_count; i++) {
        const type_field *f = &record->type.fields[i];
        if (!f->name || !native_text(record, f->name) || !native_field(f) ||
            !native_text(record, f->type_name) || !native_text(record, f->unit) ||
            !native_text(record, f->description) ||
            (f->type != PIN_STRUCT && f->layout) || f->offset > record->type.size ||
            f->size > record->type.size - f->offset) {
            return false;
        }
        if (f->type == PIN_STRUCT &&
            (!native_child(record, f->layout) || !f->type_name ||
             !native_valid_at(f->layout->carrier, depth + 1, work) ||
             strcmp(f->layout->name, f->type_name) ||
             (f->container == PIN_CONTAINER_SINGLE && f->layout->size != f->size))) {
            return false;
        }
    }
    return record->layout == type_layout_hash(&record->type);
}
static bool native_valid(const pin_record *record, int depth)
{
    size_t work = 0;
    return native_valid_at(record, depth, &work);
}
static bool native_same(const type_desc *a, const type_desc *b, int depth)
{
    if (!a || !b || !a->name || !b->name || (a->field_count && !a->fields) ||
        (b->field_count && !b->fields) || depth >= TYPE_MAX_NESTING ||
        a->size != b->size || a->field_count != b->field_count ||
        strcmp(a->name, b->name) || !!a->alias_of != !!b->alias_of ||
        type_layout_hash(a) != type_layout_hash(b)) {
        return false;
    }
    const pin_record *x = a->carrier, *y = b->carrier;
    bool authored_x = x && !x->native, authored_y = y && !y->native;
    /* Child layout hashes describe storage; authored nominal identity remains
     * distinct even when names and physical fields happen to match. */
    if ((authored_x || authored_y) && (!authored_x || !authored_y || x->id != y->id)) {
        return false;
    }
    if (a->alias_of && (strcmp(a->alias_of, b->alias_of) ||
                        !native_same(
                            type_alias_type(a, (type_resolver){ 0 }),
                            type_alias_type(b, (type_resolver){ 0 }),
                            depth + 1
                        ))) {
        return false;
    }
    for (int i = 0; i < a->field_count; i++) {
        if (a->fields[i].type == PIN_STRUCT &&
            !native_same(
                type_field_type(&a->fields[i], (type_resolver){ 0 }),
                type_field_type(&b->fields[i], (type_resolver){ 0 }),
                depth + 1
            )) {
            return false;
        }
    }
    return true;
}
static bool find_owned_type(const type_desc *type, const char *name, const type_desc **found, int depth)
{
    if (!type || depth >= TYPE_MAX_NESTING) {
        return false;
    }
    if (!strcmp(type->name, name)) {
        if (*found && !native_same(*found, type, 0)) {
            return false;
        }
        *found = type;
    }
    if (type->alias && !find_owned_type(type->alias, name, found, depth + 1)) {
        return false;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->layout && !find_owned_type(field->layout, name, found, depth + 1)) {
            return false;
        }
    }
    return true;
}
static const type_desc *resolve_owned_type(void *user, const char *name)
{
    const pin_record *record = user;
    const type_desc *found = NULL;
    return record && record->frozen && name &&
                   find_owned_type(&record->type, name, &found, 0)
             ? found
             : NULL;
}
type_resolver pin_record_resolver(const pin_record *record)
{
    return (type_resolver){ .find = resolve_owned_type, .user = (void *)record };
}
static bool native_preflight(const type_desc *type)
{
    size_t bytes = 0, work = 0;
    return native_size(type, (type_resolver){ 0 }, 0, &bytes, &work);
}
bool pin_record_native_current(const pin_record *record)
{
    const type_desc *current = record ? type_catalog_find(record->type.name) : NULL;
    return record && record->native && native_valid(record, 0) &&
           native_preflight(current) && native_same(&record->type, current, 0);
}
bool pin_record_fields_current(const pin_record *record)
{
    if (!record || !pin_record_valid(record)) {
        return false;
    }
    if (record->native) {
        return pin_record_native_current(record);
    }
    for (int i = 0; i < record->type.field_count; i++) {
        const type_field *field = &record->type.fields[i];
        if (field->type != PIN_STRUCT) {
            continue;
        }
        const type_desc *child = type_field_type(field, (type_resolver){ 0 });
        if (child && child->carrier && !child->carrier->native) {
            if (!pin_record_fields_current(child->carrier)) {
                return false;
            }
            continue;
        }
        const type_desc *current = type_catalog_find(field->type_name);
        if (!native_preflight(current) || !native_same(child, current, 0)) {
            return false;
        }
    }
    return true;
}
bool pin_value_layout_same(const pin_value *a, const pin_value *b)
{
    const pin_record *x = a->record, *y = b->record;
    if ((!x || !x->native) && (!y || !y->native)) {
        if (x || y) {
            return pin_record_same(x, y);
        }
        /* A tagged union carries its own tag and payload, and no layout. */
        if (a->type != PIN_STRUCT && a->type != PIN_POINTER &&
            a->type != PIN_HANDLE && a->type != PIN_ENUM && a->type != PIN_QUERY) {
            return true;
        }
        return (!a->type_name && !b->type_name) ||
               (a->type_name && b->type_name && !strcmp(a->type_name, b->type_name));
    }
    if ((x && !x->native) || (y && !y->native) || !a->type_name ||
        !b->type_name || strcmp(a->type_name, b->type_name)) {
        return false;
    }
    if (x && y) {
        return pin_record_same(x, y);
    }
    return pin_record_native_current(x ? x : y);
}

static bool compositional(const type_field *fields, int count)
{
    for (int i = 0; i < count; i++) {
        if (fields[i].container != PIN_CONTAINER_SINGLE || fields[i].layout) {
            return true;
        }
    }
    return false;
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
    size_t bytes = 0, work = 0;
    if (!layout_work(&work, 1 + (size_t)count) ||
        !layout_bytes(&bytes, sizeof(pin_record) + (size_t)count * sizeof(type_field)) ||
        !layout_text(&bytes, name)) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        const type_field *field = &fields[i];
        if (!field->name || !*field->name || !layout_text(&bytes, field->name) ||
            !layout_text(&bytes, field->type_name) ||
            !layout_text(&bytes, field->unit)) {
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (!layout_work(&work, 1) || !strcmp(field->name, fields[j].name)) {
                return false;
            }
        }
    }
    size_t closed_bytes = bytes;
    for (int i = 0; i < count; i++) {
        if (fields[i].type == PIN_STRUCT &&
            !native_size(
                type_field_type(&fields[i], (type_resolver){ 0 }),
                (type_resolver){ 0 },
                1,
                &closed_bytes,
                &work
            )) {
            return false;
        }
    }
    /* The bounded preflight dominates every recursive size/alignment/hash
     * traversal. Legacy implicit native children close only on snapshot. */
    size_t size = 0, alignment = 1;
    for (int i = 0; i < count; i++) {
        size_t width = field_size(&fields[i]), step = field_align(&fields[i]);
        if (!width || !align(&size, step) || !add(&size, width)) {
            return false;
        }
        if (step > alignment) {
            alignment = step;
        }
    }
    if (!align(&size, alignment)) {
        return false;
    }
    *out_bytes = compositional(fields, count) ? closed_bytes : bytes;
    *out_size = size;
    return true;
}
size_t pin_record_create_bytes(const char *name, uint64_t id, const type_field *fields, int count)
{
    size_t bytes, size;
    return shape(name, id, fields, count, &bytes, &size) ? bytes : 0;
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
                                 .text_descriptor = fields[i].type == PIN_STR &&
                                                    fields[i].container == PIN_CONTAINER_SINGLE,
                                 .offset = offset,
                                 .size = field_size(&fields[i]),
                                 .type_name = type_name,
                                 .unit = unit,
                                 .container = fields[i].container };
        offset += owned[i].size;
    }
    size_t used = (size_t)(text - (char *)memory);
    record->frozen = compositional(fields, count);
    for (int i = 0; record->frozen && i < count; i++) {
        if (fields[i].type == PIN_STRUCT) {
            owned[i].layout = &native_initialize(
                                   memory,
                                   &used,
                                   type_field_type(&fields[i], (type_resolver){ 0 }),
                                   (type_resolver){ 0 }
            )
                                   ->type;
        }
    }
    record->type.carrier = record;
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
static void native_move(const pin_record *source, pin_record *copy, const char *from, char *to)
{
    type_field *fields = (type_field *)moved((const char *)source->type.fields, from, to);
    copy->type.fields = fields;
    copy->type.name = moved(source->type.name, from, to);
    copy->type.alias_of = moved(source->type.alias_of, from, to);
    copy->type.carrier = copy;
    copy->type.alias = (const type_desc *)moved((const char *)source->type.alias, from, to);
    if (source->type.alias) {
        native_move(source->type.alias->carrier, (pin_record *)copy->type.alias, from, to);
    }
    for (int i = 0; i < source->type.field_count; i++) {
        const type_field *field = &source->type.fields[i];
        fields[i].name = moved(field->name, from, to);
        fields[i].type_name = moved(field->type_name, from, to);
        fields[i].unit = moved(field->unit, from, to);
        fields[i].description = moved(field->description, from, to);
        fields[i].layout = (const type_desc *)moved((const char *)field->layout, from, to);
        if (field->layout) {
            native_move(field->layout->carrier, (pin_record *)fields[i].layout, from, to);
        }
    }
}
static const pin_record *copy_record(arena *storage, const pin_record *record)
{
    pin_record *copy = arena_alloc(storage, record->bytes);
    memcpy(copy, record, record->bytes);
    if (record->frozen) {
        native_move(record, copy, (const char *)record, (char *)copy);
        return copy;
    }
    const char *from = (const char *)record;
    char *to = (char *)copy;
    type_field *fields = (type_field *)(copy + 1);
    copy->type.fields = fields;
    copy->type.carrier = copy;
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
size_t pin_record_snapshot_bytes(const pin_record *record)
{
    if (!pin_record_valid(record)) {
        return 0;
    }
    if (!record->frozen) {
        for (int i = 0; i < record->type.field_count; i++) {
            if (record->type.fields[i].type == PIN_STRUCT) {
                size_t bytes = 0, work = 0;
                return native_size(&record->type, (type_resolver){ 0 }, 0, &bytes, &work)
                         ? bytes
                         : 0;
            }
        }
    }
    return record->bytes;
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
    if (copy && !record->frozen && pin_record_same(copy, record) &&
        same_text(copy, record)) {
        return copy;
    }
    bool nested = false;
    for (int i = 0; !record->frozen && i < record->type.field_count; i++) {
        nested |= record->type.fields[i].type == PIN_STRUCT;
    }
    if (!nested && arena_contains(storage, record)) {
        return record;
    }
    if (nested) {
        if (!pin_record_valid(record)) {
            return NULL;
        }
        pin_record *closed = (pin_record *)pin_record_from_type(storage, &record->type);
        if (!closed) {
            return NULL;
        }
        closed->native = false;
        closed->id = record->id;
        closed->layout = record->layout;
        copy = closed;
    } else {
        copy = copy_record(storage, record);
    }
    arena_copy_remember(storage, record, copy);
    return copy;
}
bool pin_record_valid(const pin_record *record)
{
    if (record && record->frozen) {
        return native_valid(record, 0);
    }
    if (!record || !record->id || !record->type.name || !*record->type.name ||
        !record->type.fields || record->type.field_count <= 0 ||
        record->type.alias_of || record->bytes > PIN_RECORD_MAX_LAYOUT_BYTES) {
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
    size_t closed_bytes = record->bytes, work = 1 + (size_t)record->type.field_count;
    for (int i = 0; i < record->type.field_count; i++) {
        if (fields[i].type == PIN_STRUCT &&
            !native_size(
                type_field_type(&fields[i], (type_resolver){ 0 }),
                (type_resolver){ 0 },
                1,
                &closed_bytes,
                &work
            )) {
            return false;
        }
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
    if (record->frozen) {
        return native_valid(record, 0);
    }
    for (int i = 0; i < record->type.field_count; i++) {
        const type_field *field = &record->type.fields[i];
        if (field->type == PIN_STRUCT &&
            (!native_preflight(type_field_type(field, (type_resolver){ 0 })) ||
             field_size(field) != field->size)) {
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
    if (a->native != b->native) {
        return false;
    }
    return a->id == b->id && a->layout == b->layout &&
           !strcmp(a->type.name, b->type.name) &&
           (!(a->frozen || b->frozen) || native_same(&a->type, &b->type, 0));
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
