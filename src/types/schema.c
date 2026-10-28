#include "types/schema.h"

#include "support/array.h"
#include "types/array.h"
#include "types/map.h"
#include "types/record.h"
#include "types/scalar.h"
#include "types/trace.h"
#include "types/union.h"

#include <assert.h>
#include <string.h>

static bool add_bytes(size_t *bytes, size_t count)
{
    if (count > SIZE_MAX - *bytes) {
        return false;
    }
    *bytes += count;
    return true;
}
static bool text_bytes(size_t *bytes, const char *text)
{
    return !text || add_bytes(bytes, strlen(text) + 1);
}
static bool collect_types(const type_desc *type, type_resolver resolver, const type_desc ***types, int depth)
{
    if (!type || !type->name || !type->size || type->field_count < 0 ||
        (type->field_count && !type->fields) || depth >= TYPE_MAX_NESTING) {
        return false;
    }
    for (int i = 0; i < acount(*types); i++) {
        if (!strcmp((*types)[i]->name, type->name)) {
            return true;
        }
    }
    apush(*types, type);
    if (type->alias_of) {
        const type_desc *alias = type_alias_type(type, resolver);
        if (!alias || alias->size != type->size ||
            !collect_types(alias, resolver, types, depth + 1)) {
            return false;
        }
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (!field->name || field->container != PIN_CONTAINER_SINGLE ||
            field->offset > type->size || field->size > type->size - field->offset) {
            return false;
        }
        if (field->type == PIN_STRUCT) {
            const type_desc *nested = type_field_type(field, resolver);
            if (!nested || nested->size != field->size ||
                !collect_types(nested, resolver, types, depth + 1)) {
                return false;
            }
            continue;
        }
        bool scalar = field->type >= PIN_BOOL && field->type <= PIN_ENUM &&
                      field->type != PIN_STRUCT;
        if (!scalar || (field->type == PIN_ENUM
                            ? !field->size || field->size > sizeof(int32_t)
                            : field->size != behavior_scalar_size(field->type))) {
            return false;
        }
    }
    return true;
}
static char *copy_text(char **at, const char *text)
{
    if (!text) {
        return NULL;
    }
    char *copy = *at;
    size_t bytes = strlen(text) + 1;
    memcpy(copy, text, bytes);
    *at += bytes;
    return copy;
}
static const pin_schema *create_schema(arena *storage, const type_desc *root, const type_desc **sources)
{
    bool valid = true;
    size_t bytes = sizeof(pin_schema), fields = 0;
    for (int i = 0; valid && i < acount(sources); i++) {
        const type_desc *type = sources[i];
        valid = add_bytes(&bytes, sizeof(type_desc) + sizeof(type_desc *)) &&
                text_bytes(&bytes, type->name) &&
                text_bytes(&bytes, type->alias_of) &&
                add_bytes(&fields, (size_t)type->field_count);
        for (int j = 0; valid && j < type->field_count; j++) {
            const type_field *field = &type->fields[j];
            valid = text_bytes(&bytes, field->name) &&
                    text_bytes(&bytes, field->type_name) &&
                    text_bytes(&bytes, field->unit) &&
                    text_bytes(&bytes, field->description);
        }
    }
    valid = valid && fields <= SIZE_MAX / sizeof(type_field) &&
            add_bytes(&bytes, fields * sizeof(type_field));
    if (!valid || !type_layout_hash_explicit(root, sources, acount(sources))) {
        afree(sources);
        return NULL;
    }
    pin_schema *schema = arena_alloc(storage, bytes);
    const type_desc **lookup = (const type_desc **)(schema + 1);
    type_desc *types = (type_desc *)(lookup + acount(sources));
    type_field *next = (type_field *)(types + acount(sources));
    char *text = (char *)(next + fields);
    *schema = (pin_schema){ .bytes = bytes,
                            .count = acount(sources),
                            .types = types,
                            .lookup = lookup };
    for (int i = 0; i < schema->count; i++) {
        types[i] = *sources[i];
        lookup[i] = &types[i];
        types[i].name = copy_text(&text, sources[i]->name);
        types[i].alias_of = copy_text(&text, sources[i]->alias_of);
        types[i].fields = next;
        types[i].carrier = NULL;
        types[i].alias = NULL;
        for (int j = 0; j < types[i].field_count; j++) {
            *next = sources[i]->fields[j];
            next->name = copy_text(&text, next->name);
            next->type_name = copy_text(&text, next->type_name);
            next->unit = copy_text(&text, next->unit);
            next->description = copy_text(&text, next->description);
            next->layout = NULL;
            next++;
        }
    }
    for (int i = 0; i < schema->count; i++) {
        if (types[i].alias_of) {
            types[i].alias = pin_schema_type(schema, types[i].alias_of);
        }
        type_field *fields = (type_field *)types[i].fields;
        for (int j = 0; j < types[i].field_count; j++) {
            if (fields[j].type == PIN_STRUCT) {
                fields[j].layout = pin_schema_type(schema, fields[j].type_name);
            }
        }
    }
    assert(text == (char *)schema + bytes);
    afree(sources);
    return schema;
}
const pin_schema *pin_schema_create(arena *storage, const type_desc *root, type_resolver resolver)
{
    const type_desc **sources = NULL;
    if (!collect_types(root, resolver, &sources, 0)) {
        afree(sources);
        return NULL;
    }
    return create_schema(storage, root, sources);
}
const pin_schema *pin_schema_create_record(arena *storage, const pin_record *record)
{
    if (!pin_record_current(record)) {
        return NULL;
    }
    const type_desc **sources = NULL;
    for (int i = 0; i < record->type.field_count; i++) {
        const type_field *field = &record->type.fields[i];
        if (field->type == PIN_STRUCT &&
            !collect_types(type_field_type(field, (type_resolver){ 0 }), (type_resolver){ 0 }, &sources, 1)) {
            afree(sources);
            return NULL;
        }
    }
    if (type_layout_hash_explicit(&record->type, sources, acount(sources)) !=
        record->layout) {
        afree(sources);
        return NULL;
    }
    return create_schema(storage, &record->type, sources);
}
static const void *relocate(const pin_schema *source, pin_schema *copy, const void *value)
{
    return value ? (const char *)copy + ((const char *)value - (const char *)source)
                 : NULL;
}
const pin_schema *pin_schema_snapshot(arena *storage, const pin_schema *source)
{
    if (!source || arena_contains(storage, source)) {
        return source;
    }
    pin_schema *copy = arena_alloc(storage, source->bytes);
    memcpy(copy, source, source->bytes);
    type_desc *types = (type_desc *)relocate(source, copy, source->types);
    copy->types = types;
    const type_desc **lookup = (const type_desc **)relocate(source, copy, source->lookup);
    copy->lookup = lookup;
    for (int i = 0; i < copy->count; i++) {
        lookup[i] = &types[i];
        types[i].name = relocate(source, copy, types[i].name);
        types[i].alias_of = relocate(source, copy, types[i].alias_of);
        types[i].alias = relocate(source, copy, types[i].alias);
        type_field *fields = (type_field *)relocate(source, copy, types[i].fields);
        types[i].fields = fields;
        for (int j = 0; j < types[i].field_count; j++) {
            fields[j].name = relocate(source, copy, fields[j].name);
            fields[j].type_name = relocate(source, copy, fields[j].type_name);
            fields[j].unit = relocate(source, copy, fields[j].unit);
            fields[j].description = relocate(source, copy, fields[j].description);
            fields[j].layout = relocate(source, copy, fields[j].layout);
        }
    }
    return copy;
}
const type_desc *pin_schema_type(const pin_schema *schema, const char *name)
{
    for (int i = 0; schema && name && i < schema->count; i++) {
        if (!strcmp(schema->types[i].name, name)) {
            return &schema->types[i];
        }
    }
    return NULL;
}
static const type_desc *resolve(void *user, const char *name)
{
    return pin_schema_type(user, name);
}
type_resolver pin_schema_resolver(const pin_schema *schema)
{
    return (type_resolver){ .find = resolve, .user = (void *)schema };
}
uint64_t pin_schema_layout(const pin_schema *schema, const char *name)
{
    return pin_schema_layout_type(schema, pin_schema_type(schema, name));
}
uint64_t pin_schema_layout_type(const pin_schema *schema, const type_desc *type)
{
    return schema ? type_layout_hash_explicit(type, schema->lookup, schema->count) : 0;
}
void pin_schema_trace_fields(
    const pin_schema *schema,
    const type_desc *type,
    const void *data,
    arena_marker mark,
    void *user
)
{
    mark(user, schema);
    mark(user, type);
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        const char *member = (const char *)data + field->offset;
        if (field->type == PIN_STRUCT) {
            pin_schema_trace_fields(schema, pin_schema_type(schema, field->type_name), member, mark, user);
        } else if (
            field->type == PIN_STR || field->type == PIN_NAME ||
            field->type == PIN_POINTER || field->type == PIN_QUERY || field->type == PIN_UNION
        ) {
            const void *text;
            memcpy(&text, member, sizeof text);
            mark(user, text);
            if (text && field->type == PIN_STR && field->text_descriptor) {
                mark(user, ((const pin_string *)text)->bytes);
            }
        }
    }
}
typedef struct
{
    const pin_schema *schema;
    const type_desc *type;
} schema_shape;
static void trace_payload(const void *data, const void *shape, arena_marker mark, void *user)
{
    const schema_shape *value = shape;
    mark(user, shape);
    pin_schema_trace_fields(value->schema, value->type, data, mark, user);
}
void pin_schema_trace_struct(arena *storage, void *data, const pin_schema *schema, const type_desc *type)
{
    if (!storage->collectable) {
        return;
    }
    schema_shape *shape = arena_alloc(storage, sizeof *shape);
    *shape = (schema_shape){ schema, type };
    arena_set_trace(storage, data, trace_payload, shape);
}
static int snapshot_owned(arena *, pin_value *, const pin_value *, int);

static int snapshot_map(arena *storage, pin_value *value, const pin_map *source, int depth)
{
    if (source->count < 0 || (source->count && !source->entries) ||
        (size_t)source->count > (SIZE_MAX - sizeof(pin_map)) / sizeof(pin_map_entry)) {
        return -1;
    }
    pin_map *copy = arena_alloc(
        storage,
        sizeof *copy + (size_t)source->count * sizeof *copy->entries
    );
    *copy = (pin_map){ .count = source->count,
                       .capacity = source->count,
                       .entries = (pin_map_entry *)(copy + 1) };
    memset(copy->entries, 0, (size_t)copy->count * sizeof *copy->entries);
    if (snapshot_owned(storage, &copy->key_type, &source->key_type, depth + 1) ||
        snapshot_owned(storage, &copy->value_type, &source->value_type, depth + 1)) {
        return -1;
    }
    copy->key_type.u64 = copy->value_type.u64 = 0;
    for (int i = 0; i < copy->count; i++) {
        if (snapshot_owned(storage, &copy->entries[i].key, &source->entries[i].key, depth + 1) ||
            snapshot_owned(storage, &copy->entries[i].value, &source->entries[i].value, depth + 1) ||
            !pin_map_entry_valid(copy, &copy->entries[i].key, &copy->entries[i].value)) {
            return -1;
        }
    }
    size_t bytes = pin_map_index_bytes(copy->count);
    if (bytes == SIZE_MAX) {
        return -1;
    }
    if (bytes) {
        pin_map_index(copy, arena_alloc(storage, bytes));
    }
    pin_map_trace_storage(storage, copy);
    value->ptr = copy;
    return 0;
}

static int snapshot_union(arena *storage, pin_value *value, const pin_union *source, int depth)
{
    if (!source->tag || source->count < 0 ||
        (size_t)source->count > (SIZE_MAX - sizeof(pin_union)) / sizeof(pin_value)) {
        return -1;
    }
    pin_union *copy = arena_alloc(
        storage,
        sizeof *copy + (size_t)source->count * sizeof *copy->values
    );
    *copy = (pin_union){ .tag = source->tag, .count = source->count };
    memset(copy->values, 0, (size_t)copy->count * sizeof *copy->values);
    for (int i = 0; i < copy->count; i++) {
        if (snapshot_owned(storage, &copy->values[i], &source->values[i], depth + 1)) {
            return -1;
        }
    }
    pin_arena_trace_union(storage, copy);
    value->ptr = copy;
    return 0;
}

static int snapshot_array(arena *storage, pin_value *value, const pin_array *source, int depth)
{
    pin_array shape = *source;
    if (value->schema) {
        shape.schema = value->schema;
    }
    pin_array *copy = pin_array_snapshot_like(storage, &shape, source->count);
    if (!copy) {
        return -1;
    }
    for (int i = 0; i < source->count; i++) {
        pin_value item = pin_array_get(&shape, i), retained;
        if (snapshot_owned(storage, &retained, &item, depth + 1) ||
            pin_array_set(copy, i, &retained)) {
            return -1;
        }
    }
    if (value->container == PIN_CONTAINER_SET) {
        pin_array_index(storage, copy);
    }
    value->schema = copy->schema;
    value->ptr = copy;
    return 0;
}

/* A header may carry a frozen native record of the type an array describes
 * by its own schema or the catalog. The copy follows the array. */
static bool header_describes(const pin_value *value, const pin_array *array)
{
    if (value->record && value->record->native && !array->record) {
        return value->type_name && array->type_name &&
               !strcmp(value->type_name, array->type_name);
    }
    return pin_record_same(value->record, array->record);
}
/* Compositional authored records already close every child layout. Their
 * container headers and authored child identities belong to that carrier,
 * rather than to the native-only schema representation. */
static bool authored_composition(const pin_record *record)
{
    if (!record || record->native || !record->frozen) {
        return false;
    }
    for (int i = 0; i < record->type.field_count; i++) {
        const type_field *field = &record->type.fields[i];
        if (field->container != PIN_CONTAINER_SINGLE ||
            (field->layout && field->layout->carrier && !field->layout->carrier->native)) {
            return true;
        }
    }
    return false;
}
static int snapshot_owned(arena *storage, pin_value *out, const pin_value *source, int depth)
{
    if (depth >= 64) {
        return -1;
    }
    pin_value value = *source;
    const pin_array *array = value.container == PIN_CONTAINER_ARRAY ||
                                     value.container == PIN_CONTAINER_SET
                               ? value.ptr
                               : NULL;
    if (array && (!pin_array_valid(array) || !header_describes(&value, array))) {
        return -1;
    }
    if (array) {
        value.record = array->record;
    }
    bool nested = array && !array->stride && array->count &&
                  pin_array_get(array, 0).container != PIN_CONTAINER_SINGLE;
    bool typed_array = value.type == PIN_STRUCT &&
                       (value.container == PIN_CONTAINER_ARRAY ||
                        value.container == PIN_CONTAINER_SET) &&
                       !nested &&
                       (!value.type_name ||
                        strncmp(value.type_name, "__collection_", 13));
    if (typed_array && array) {
        if (!value.schema) {
            value.schema = array->schema;
        }
        for (int i = 0; !value.schema && !array->stride && i < array->count; i++) {
            const pin_value *item = &array->items[i];
            if (item->type == PIN_STRUCT && item->container == PIN_CONTAINER_SINGLE &&
                item->schema && item->type_name && value.type_name &&
                !strcmp(item->type_name, value.type_name)) {
                value.schema = item->schema;
            }
        }
    }
    if (value.type == PIN_STRUCT &&
        (value.container == PIN_CONTAINER_SINGLE || typed_array) &&
        !value.schema && !authored_composition(value.record)) {
        bool native = !value.record;
        for (int i = 0; value.record && i < value.record->type.field_count; i++) {
            native |= value.record->type.fields[i].type == PIN_STRUCT;
        }
        if (native) {
            value.schema = value.record
                             ? pin_schema_create_record(storage, value.record)
                             : pin_schema_create(storage, pin_value_type(&value), (type_resolver){ 0 });
            if (!value.schema) {
                return -1;
            }
        }
    }
    bool container = value.container != PIN_CONTAINER_SINGLE || value.type == PIN_UNION;
    pin_value header = value;
    if (container) {
        header.ptr = NULL;
    }
    pin_value retained;
    if (pin_value_snapshot_to_arena(storage, &retained, &header)) {
        return -1;
    }
    int status = 0;
    if (value.ptr && value.container == PIN_CONTAINER_MAP) {
        status = snapshot_map(storage, &retained, value.ptr, depth);
    } else if (array) {
        status = snapshot_array(storage, &retained, array, depth);
    } else if (value.ptr && value.type == PIN_UNION) {
        status = snapshot_union(storage, &retained, value.ptr, depth);
    }
    if (status) {
        return status;
    }
    *out = retained;
    return 0;
}

int pin_value_snapshot_owned_to_arena(arena *storage, pin_value *out, const pin_value *source)
{
    return snapshot_owned(storage, out, source, 0);
}
