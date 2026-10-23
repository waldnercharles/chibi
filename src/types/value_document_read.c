#include "types/array.h"
#include "types/catalog.h"
#include "types/map.h"
#include "types/record.h"
#include "types/text.h"
#include "types/trace.h"
#include "types/type.h"
#include "types/value_document.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define DOCUMENT_MAX_BYTES (64u * 1024u * 1024u)
#define DOCUMENT_MAX_VALUES 1000000
#define DOCUMENT_MAX_DEPTH 64
#define DOCUMENT_MAX_WORK (64u * 1024u * 1024u)

typedef struct
{
    arena *storage;
    const pin_record *record;
    char *error;
    size_t error_size, bytes, work;
    int values, depth;
    bool failed;
} document_reader;

static void read_error(document_reader *reader, const char *format, ...)
{
    reader->failed = true;
    va_list args;
    va_start(args, format);
    if (reader->error_size) {
        vsnprintf(reader->error, reader->error_size, format, args);
    }
    va_end(args);
}
static void *allocate(document_reader *reader, size_t size)
{
    if (!reader->storage || size > DOCUMENT_MAX_BYTES - reader->bytes) {
        read_error(reader, "document value requires owned storage within byte limit");
        return NULL;
    }
    reader->bytes += size;
    void *block = arena_alloc(reader->storage, size);
    memset(block, 0, size);
    return block;
}
static char *own_text(document_reader *reader, const char *text, size_t size)
{
    if (size >= DOCUMENT_MAX_BYTES) {
        read_error(reader, "document text exceeds byte limit");
        return NULL;
    }
    char *copy = allocate(reader, size + 1);
    if (copy) {
        memcpy(copy, text, size);
    }
    return copy;
}
static size_t field_bytes(const type_field *field)
{
    static const size_t sizes[] = {
        [PIN_FLOW] = 0,
        [PIN_BOOL] = sizeof(bool),
        [PIN_I8] = sizeof(int8_t),
        [PIN_U8] = sizeof(uint8_t),
        [PIN_I16] = sizeof(int16_t),
        [PIN_U16] = sizeof(uint16_t),
        [PIN_I32] = sizeof(int32_t),
        [PIN_U32] = sizeof(uint32_t),
        [PIN_I64] = sizeof(int64_t),
        [PIN_U64] = sizeof(uint64_t),
        [PIN_F32] = sizeof(float),
        [PIN_F64] = sizeof(double),
        [PIN_STR] = sizeof(char *),
        [PIN_NAME] = sizeof(char *),
        [PIN_HANDLE] = sizeof(uint64_t),
        [PIN_ENUM] = sizeof(int32_t),
        [PIN_POINTER] = sizeof(void *),
        [PIN_QUERY] = sizeof(void *),
    };
    if (field->type == PIN_STRUCT) {
        const type_desc *type = type_catalog_find(field->type_name);
        return type && field->size == type->size ? type->size : SIZE_MAX;
    }
    if (field->type == PIN_POINTER || field->type == PIN_QUERY) {
        return sizeof(void *);
    }
    return field->type >= PIN_FLOW && field->type <= PIN_ENUM ? sizes[field->type]
                                                              : SIZE_MAX;
}
static int read_value(pin_type, const char *, const av_value *, pin_value *, document_reader *);
static int read_typed(pin_value, const av_value *, pin_value *, document_reader *);
static int read_map(pin_value, const av_value *, pin_value *, document_reader *);
static int read_nested_array(pin_value, const av_value *, pin_value *, document_reader *);
static int read_value_into(
    pin_type,
    const char *,
    const av_value *,
    pin_value *,
    document_reader *,
    void *,
    bool
);
static int read_single(document_reader *, pin_type, const char *, const av_value *, pin_value *, void *, bool);

static int read_signed(const av_value *v, int64_t *out)
{
    if (av_is_sint(v)) {
        *out = av_get_sint(v);
        return 0;
    }
    if (av_is_uint(v) && av_get_uint(v) <= INT64_MAX) {
        *out = (int64_t)av_get_uint(v);
        return 0;
    }
    return -1;
}

static int read_unsigned(const av_value *v, uint64_t *out)
{
    if (av_is_uint(v)) {
        *out = av_get_uint(v);
        return 0;
    }
    if (av_is_sint(v) && av_get_sint(v) >= 0) {
        *out = (uint64_t)av_get_sint(v);
        return 0;
    }
    return -1;
}

static int read_array(
    pin_type element_type,
    const char *type_name,
    const av_value *document,
    pin_value *out,
    document_reader *reader
)
{
    if (!av_is_arr(document) || av_arr_size(document) > DOCUMENT_MAX_VALUES ||
        av_arr_size(document) >
            (DOCUMENT_MAX_BYTES - sizeof(pin_array)) / sizeof(pin_value)) {
        read_error(reader, "invalid array default");
        return -1;
    }
    int count = (int)av_arr_size(document);
    pin_array *array = allocate(reader, sizeof *array);
    if (!array) {
        return -1;
    }
    *array = (pin_array){
        .element_type = element_type,
        .type_name = reader->record ? reader->record->type.name
                   : type_name ? own_text(reader, type_name, strlen(type_name))
                               : NULL,
        .record = reader->record,
        .count = count,
        .capacity = count,
        .stride = pin_array_element_stride(element_type, type_name, reader->record)
    };
    for (int i = 0; array->stride && element_type == PIN_STRUCT && i < count; i++) {
        if (av_is_null(av_arr_get(document, (size_t)i))) {
            array->stride = 0;
        }
    }
    if (array->stride && (size_t)count > DOCUMENT_MAX_BYTES / array->stride) {
        read_error(reader, "document value requires owned storage within byte limit");
        return -1;
    }
    size_t bytes = (size_t)count * (array->stride ? array->stride : sizeof(pin_value));
    void *payload = count ? allocate(reader, bytes) : NULL;
    if (count && !payload) {
        return -1;
    }
    if (array->stride) {
        array->data = payload;
    } else {
        array->items = payload;
    }
    for (int i = 0; i < count; i++) {
        pin_value item;
        void *destination = array->stride && element_type == PIN_STRUCT
                              ? pin_array_data(array, i)
                              : NULL;
        if (read_value_into(
                element_type,
                array->type_name,
                av_arr_get(document, (size_t)i),
                &item,
                reader,
                destination,
                false
            ) ||
            pin_array_set(array, i, &item)) {
            if (element_type != PIN_STRUCT) {
                read_error(reader, "invalid array item %d", i);
            }
            return -1;
        }
    }
    pin_arena_trace_array(reader->storage, array);
    *out = (pin_value){ .type = element_type,
                        .container = PIN_CONTAINER_ARRAY,
                        .type_name = array->type_name,
                        .record = array->record,
                        .ptr = array };
    return 0;
}

static int read_struct(
    const char *type_name,
    const av_value *document,
    pin_value *out,
    document_reader *reader,
    void *destination
)
{
    const type_desc *type = reader->record ? &reader->record->type
                                           : type_catalog_find(type_name);
    if (!type || !type->size || !av_is_obj(document)) {
        read_error(reader, "invalid struct default for %s", type_name ? type_name : "unknown type");
        return -1;
    }
    void *block = destination ? destination : allocate(reader, type->size);
    if (!block) {
        return -1;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        size_t bytes = field_bytes(field);
        if (!field->name || bytes == SIZE_MAX || field->offset > type->size ||
            bytes > type->size - field->offset) {
            read_error(reader, "invalid reflected field layout");
            return -1;
        }
        const av_value *value = av_obj_get(document, field->name);
        if (!value) {
            read_error(reader, "missing struct field %s.%s", type->name, field->name);
            return -1;
        }
        if (field->type == PIN_STRUCT && !av_is_obj(value)) {
            read_error(reader, "invalid struct field %s.%s", type->name, field->name);
            return -1;
        }
        pin_value member;
        int result;
        if (field->range_contract && av_is_num(value) &&
            (field->type == PIN_F32 || field->type == PIN_F64)) {
            member = pin_field_number(field, av_get_num(value));
            result = 0;
        } else {
            const pin_record *record = reader->record;
            /* Nested fields carry native names, not the enclosing authored
             * record's descriptor. Decode their own reflected layout inline. */
            if (field->type == PIN_STRUCT) {
                reader->record = NULL;
            }
            result = read_value_into(
                field->type,
                field->type_name,
                value,
                &member,
                reader,
                field->type == PIN_STRUCT ? (unsigned char *)block + field->offset : NULL,
                true
            );
            reader->record = record;
        }
        if (result && !reader->failed && field->range_contract &&
            (av_is_sint(value) || av_is_uint(value))) {
            member = pin_field_number(field, av_get_num(value));
            result = 0;
        }
        if (result) {
            if (field->type != PIN_STRUCT) {
                read_error(
                    reader,
                    "invalid struct field %s.%s",
                    type->name,
                    field->name
                );
            }
            return -1;
        }
        if (field->type == PIN_STR) {
            void *destination = (unsigned char *)block + field->offset;
            if (field->text_descriptor) {
                *(const pin_string **)destination = member.text;
            } else {
                const char *bytes;
                const char *error = pin_text_cstring(reader->storage, member, &bytes, NULL);
                if (error) {
                    read_error(reader, "%s", error);
                    return -1;
                }
                *(const char **)destination = bytes;
            }
        } else if (field->type != PIN_STRUCT) {
            const char *error = pin_write_field(reader->storage, block, field, member);
            if (error) {
                read_error(reader, "%s", error);
                return -1;
            }
        }
    }
    *out = (pin_value){ .type = PIN_STRUCT,
                        .type_name = out->type_name,
                        .record = reader->record,
                        .ptr = block };
    if (reader->record && !destination) {
        pin_arena_trace_struct(reader->storage, block, type);
    }
    return 0;
}

static int read_value(pin_type t, const char *type_name, const av_value *v, pin_value *out, document_reader *reader)
{
    return read_value_into(t, type_name, v, out, reader, NULL, true);
}
static int read_value_into(
    pin_type t,
    const char *type_name,
    const av_value *v,
    pin_value *out,
    document_reader *reader,
    void *destination,
    bool own_name
)
{
    if (++reader->values > DOCUMENT_MAX_VALUES || reader->depth >= DOCUMENT_MAX_DEPTH) {
        read_error(reader, "document value exceeds traversal limit");
        return -1;
    }
    reader->depth++;
    int result = read_single(reader, t, type_name, v, out, destination, own_name);
    reader->depth--;
    return result;
}

static int read_single(
    document_reader *reader,
    pin_type t,
    const char *type_name,
    const av_value *v,
    pin_value *out,
    void *destination,
    bool own_name
)
{
    memset(out, 0, sizeof *out);
    out->type = t;
    if (t == PIN_STRUCT && reader->record) {
        out->record = reader->record;
        out->type_name = reader->record->type.name;
    } else {
        out->type_name = own_name && reader->storage && type_name
                           ? own_text(reader, type_name, strlen(type_name))
                           : type_name;
    }
    if (reader->failed) {
        return -1;
    }
    switch (t) {
        case PIN_QUERY:
        case PIN_POINTER:
            if (!av_is_null(v)) {
                read_error(reader, t == PIN_QUERY ? "Queries cannot be persisted" : "Managed pointers cannot be persisted");
                return -1;
            }
            out->ptr = NULL;
            break;
        case PIN_BOOL:
            if (!av_is_bool(v)) {
                return -1;
            }
            out->b = av_get_bool(v);
            break;
        case PIN_I8: {
            int64_t value;
            if (read_signed(v, &value) || value < INT8_MIN || value > INT8_MAX) {
                return -1;
            }
            out->i8 = (int8_t)value;
            break;
        }
        case PIN_U8:
            if (!av_is_uint(v) || av_get_uint(v) > UINT8_MAX) {
                return -1;
            }
            out->u8 = (uint8_t)av_get_uint(v);
            break;
        case PIN_I16: {
            int64_t value;
            if (read_signed(v, &value) || value < INT16_MIN || value > INT16_MAX) {
                return -1;
            }
            out->i16 = (int16_t)value;
            break;
        }
        case PIN_U16:
            if (!av_is_uint(v) || av_get_uint(v) > UINT16_MAX) {
                return -1;
            }
            out->u16 = (uint16_t)av_get_uint(v);
            break;
        case PIN_I32: {
            int64_t value;
            if (read_signed(v, &value) || value < INT32_MIN || value > INT32_MAX) {
                return -1;
            }
            out->i = (int32_t)value;
            break;
        }
        case PIN_U32:
            if (!av_is_uint(v) || av_get_uint(v) > UINT32_MAX) {
                return -1;
            }
            out->u = (uint32_t)av_get_uint(v);
            break;
        case PIN_I64:
            if (read_signed(v, &out->i64)) {
                return -1;
            }
            break;
        case PIN_U64:
            if (!av_is_uint(v)) {
                return -1;
            }
            out->u64 = av_get_uint(v);
            break;
        case PIN_F32:
            if (!av_is_num(v)) {
                return -1;
            }
            out->f = (float)av_get_num(v);
            break;
        case PIN_F64:
            if (!av_is_num(v)) {
                return -1;
            }
            out->d = av_get_num(v);
            break;
        case PIN_ENUM: {
            int label_value = 0;
            if (av_is_str(v) &&
                type_catalog_enum_value(type_name, av_get_str(v), &label_value)) {
                out->e = label_value;
            } else {
                int64_t value;
                if (read_signed(v, &value) || value < INT32_MIN || value > INT32_MAX) {
                    return -1;
                }
                out->e = (int32_t)value;
            }
            break;
        }
        case PIN_STR: {
            const av_value *encoded = av_is_obj(v) ? av_obj_get(v, "bytes") : NULL;
            bool binary = encoded && av_obj_size(v) == 1 && av_is_arr(encoded);
            if (!av_is_str(v) && !binary) {
                return -1;
            }
            size_t length = binary ? av_arr_size(encoded) : av_get_len(v);
            if (length > UINT32_MAX ||
                length > DOCUMENT_MAX_BYTES - sizeof(pin_string) - 1 ||
                (binary && length > (size_t)(DOCUMENT_MAX_VALUES - reader->values))) {
                return -1;
            }
            if (binary) {
                reader->values += (int)length;
                for (size_t i = 0; i < length; i++) {
                    const av_value *byte = av_arr_get(encoded, i);
                    if (!av_is_uint(byte) || av_get_uint(byte) > UINT8_MAX) {
                        return -1;
                    }
                }
            }
            pin_string *text = allocate(reader, sizeof *text + length + 1);
            if (!text) {
                return -1;
            }
            char *bytes = (char *)(text + 1);
            for (size_t i = 0; i < length; i++) {
                bytes[i] = binary ? (char)av_get_uint(av_arr_get(encoded, i))
                                  : av_get_str(v)[i];
            }
            *text = (pin_string){ .bytes = bytes, .length = (uint32_t)length };
            out->text = text;
            return 0;
        }
        case PIN_NAME:
            if (!av_is_str(v) || strlen(av_get_str(v)) != av_get_len(v)) {
                return -1;
            }
            out->str = own_text(reader, av_get_str(v), av_get_len(v));
            if (!out->str) {
                return -1;
            }
            break;
        case PIN_STRUCT:
            if (av_is_null(v)) {
                return destination ? -1 : 0;
            }
            return read_struct(type_name, v, out, reader, destination);
        case PIN_FLOW:
            if (!av_is_null(v)) {
                return -1;
            }
            break;
        case PIN_HANDLE:
            if (av_is_null(v)) {
                break;
            }
            if (read_unsigned(v, &out->u64)) {
                return -1;
            }
            break;
        default: return -1;
    }
    return 0;
}

static int read_typed(pin_value declaration, const av_value *document, pin_value *out, document_reader *reader)
{
    const pin_record *previous = reader->record;
    reader->record = declaration.record;
    int result;
    if (declaration.container == PIN_CONTAINER_SINGLE) {
        result = read_value(declaration.type, declaration.type_name, document, out, reader);
    } else if (++reader->values > DOCUMENT_MAX_VALUES || reader->depth >= DOCUMENT_MAX_DEPTH) {
        read_error(reader, "document value exceeds traversal limit");
        result = -1;
    } else {
        reader->depth++;
        if (declaration.type_name) {
            declaration.type_name = own_text(
                reader,
                declaration.type_name,
                strlen(declaration.type_name)
            );
        }
        if (reader->failed) {
            result = -1;
        } else if (declaration.container == PIN_CONTAINER_MAP) {
            result = read_map(declaration, document, out, reader);
        } else if (declaration.container == PIN_CONTAINER_ARRAY || declaration.container == PIN_CONTAINER_SET) {
            result = av_is_obj(document)
                       ? read_nested_array(declaration, document, out, reader)
                       : read_array(declaration.type, declaration.type_name, document, out, reader);
            out->container = declaration.container;
        } else {
            read_error(reader, "unsupported document container");
            result = -1;
        }
        reader->depth--;
    }
    reader->record = previous;
    return result;
}

int pin_value_read_document(
    arena *storage,
    pin_value declaration,
    const av_value *document,
    pin_value *out,
    char *error,
    size_t error_size
)
{
    if (error_size) {
        error[0] = 0;
    }
    document_reader reader = { .storage = storage, .error = error, .error_size = error_size };
    if (declaration.record) {
        if (declaration.type != PIN_STRUCT ||
            (declaration.container != PIN_CONTAINER_SINGLE &&
             declaration.container != PIN_CONTAINER_ARRAY &&
             declaration.container != PIN_CONTAINER_SET) ||
            !pin_record_valid(declaration.record) || !pin_value_type(&declaration)) {
            read_error(&reader, "invalid authored document layout");
            return -1;
        }
        /* Charge the closure once even when storage already owns it. Rows use
         * this validated immutable shape and its name without per-item copies. */
        if (!storage || declaration.record->bytes > DOCUMENT_MAX_BYTES) {
            read_error(&reader, "document layout requires owned storage within byte limit");
            return -1;
        }
        reader.bytes = declaration.record->bytes;
        reader.record = pin_record_snapshot(storage, declaration.record);
        if (!reader.record) {
            read_error(&reader, "invalid authored document layout ownership");
            return -1;
        }
    }
    pin_value value = { 0 };
    if (declaration.type < PIN_FLOW || declaration.type > PIN_QUERY ||
        declaration.type == PIN_TASK ||
        (declaration.type == PIN_UNION && declaration.container == PIN_CONTAINER_SINGLE)) {
        read_error(&reader, "invalid document value type");
        return -1;
    }
    declaration.record = reader.record;
    int result = read_typed(declaration, document, &value, &reader);
    if (result || reader.failed) {
        if (!reader.failed) {
            read_error(
                &reader,
                "invalid document value for declared type %d",
                declaration.type
            );
        }
        return -1;
    }
    *out = value;
    return 0;
}
static int read_shape(
    document_reader *reader,
    const av_value *document,
    const pin_value *schema,
    pin_value *out
)
{
    const av_value *storage = av_obj_get(document, "storage"),
                   *container = av_obj_get(document, "container"),
                   *name = av_obj_get(document, "name"),
                   *id = av_obj_get(document, "record_id"),
                   *layout = av_obj_get(document, "record_layout");
    if (!av_is_obj(document) || !av_is_str(storage) || !av_is_str(container) ||
        strlen(av_get_str(storage)) != av_get_len(storage) ||
        strlen(av_get_str(container)) != av_get_len(container) ||
        (name && (!av_is_str(name) || !av_get_len(name) ||
                  strlen(av_get_str(name)) != av_get_len(name)))) {
        read_error(reader, "invalid collection type descriptor");
        return -1;
    }
    *out = (pin_value){ 0 };
    int kind;
    for (kind = PIN_BOOL; kind <= PIN_QUERY; kind++) {
        if (!strcmp(pin_type_name(kind), av_get_str(storage))) {
            break;
        }
    }
    static const char *containers[] = { "value", "array", "set", "map" };
    int collection;
    for (collection = PIN_CONTAINER_SINGLE; collection <= PIN_CONTAINER_MAP;
         collection++) {
        if (!strcmp(containers[collection], av_get_str(container))) {
            break;
        }
    }
    if (kind > PIN_QUERY || kind == PIN_TASK || collection > PIN_CONTAINER_MAP ||
        (kind == PIN_UNION && collection == PIN_CONTAINER_SINGLE) ||
        av_obj_size(document) != 2u + (name != NULL) + (id != NULL) + (layout != NULL)) {
        read_error(reader, "unsupported collection type descriptor");
        return -1;
    }
    out->type = (pin_type)kind;
    out->container = (pin_container)collection;
    out->ptr = schema && collection != PIN_CONTAINER_SINGLE ? schema->ptr : NULL;
    out->type_name = name ? own_text(reader, av_get_str(name), av_get_len(name)) : NULL;
    if (id || layout || (schema && schema->record)) {
        if (!id || !layout || !av_is_uint(id) || !av_is_uint(layout) ||
            !schema || !pin_record_valid(schema->record) || kind != PIN_STRUCT ||
            !out->type_name || strcmp(out->type_name, schema->record->type.name) ||
            av_get_uint(id) != schema->record->id ||
            av_get_uint(layout) != schema->record->layout ||
            schema->record->bytes > DOCUMENT_MAX_BYTES - reader->bytes) {
            read_error(reader, "authored collection type requires its declaration layout");
            return -1;
        }
        reader->bytes += schema->record->bytes;
        out->record = pin_record_snapshot(reader->storage, schema->record);
        out->type_name = out->record ? out->record->type.name : NULL;
        if (!out->record) {
            read_error(reader, "invalid authored collection layout ownership");
            return -1;
        }
    }
    if (schema &&
        (out->type != schema->type || out->container != schema->container ||
         strcmp(out->type_name ? out->type_name : "", schema->type_name ? schema->type_name : "") ||
         !pin_record_same(out->record, schema->record))) {
        read_error(reader, "collection type differs from its declaration");
        return -1;
    }
    return reader->failed ? -1 : 0;
}

static int read_map(pin_value declaration, const av_value *document, pin_value *out, document_reader *reader)
{
    if (av_is_null(document)) {
        *out = declaration;
        out->ptr = NULL;
        return 0;
    }
    const av_value *entries = av_obj_get(document, "entries");
    if (!av_is_obj(document) || av_obj_size(document) != 3 ||
        !av_is_arr(entries) || av_arr_size(entries) > DOCUMENT_MAX_VALUES / 2 ||
        av_arr_size(entries) >
            (DOCUMENT_MAX_BYTES - sizeof(pin_map)) / sizeof(pin_map_entry)) {
        read_error(reader, "invalid map document");
        return -1;
    }
    int count = (int)av_arr_size(entries);
    pin_map *map = allocate(reader, sizeof *map + (size_t)count * sizeof(pin_map_entry));
    if (!map) {
        return -1;
    }
    map->capacity = count;
    map->entries = (pin_map_entry *)(map + 1);
    const pin_map *schema = declaration.ptr;
    if (read_shape(reader, av_obj_get(document, "key_type"), schema ? &schema->key_type : NULL, &map->key_type) ||
        read_shape(reader, av_obj_get(document, "value_type"), schema ? &schema->value_type : NULL, &map->value_type) ||
        !pin_map_key_supported(map->key_type)) {
        read_error(reader, "invalid map key or value type");
        return -1;
    }
    pin_map_trace_storage(reader->storage, map);
    pin_value key_declaration = map->key_type, value_declaration = map->value_type;
    map->key_type.ptr = NULL;
    map->value_type.ptr = NULL;
    size_t key_bytes = 0;
    for (int i = 0; i < count; i++) {
        const av_value *pair = av_arr_get(entries, (size_t)i);
        pin_map_entry *entry = &map->entries[i];
        if (!av_is_arr(pair) || av_arr_size(pair) != 2 ||
            read_typed(key_declaration, av_arr_get(pair, 0), &entry->key, reader) ||
            read_typed(value_declaration, av_arr_get(pair, 1), &entry->value, reader) ||
            !pin_map_entry_valid(map, &entry->key, &entry->value)) {
            read_error(reader, "invalid map entry %d", i);
            return -1;
        }
        size_t width = entry->key.type == PIN_STR
                         ? (size_t)pin_text(entry->key).length + 1
                     : entry->key.type == PIN_NAME ? strlen(entry->key.str) + 1
                                                   : 1;
        if (width > DOCUMENT_MAX_WORK || (size_t)i > DOCUMENT_MAX_WORK / width ||
            key_bytes > DOCUMENT_MAX_WORK - (size_t)i * width ||
            key_bytes + (size_t)i * width > DOCUMENT_MAX_WORK - reader->work) {
            read_error(reader, "map document exceeds key comparison budget");
            return -1;
        }
        reader->work += key_bytes + (size_t)i * width;
        if (pin_map_find(map, &entry->key) >= 0) {
            read_error(reader, "duplicate key in map document");
            return -1;
        }
        key_bytes += width;
        map->count++;
    }
    size_t bytes = pin_map_index_bytes(count);
    pin_map_slot *slots = bytes ? allocate(reader, bytes) : NULL;
    if (bytes && !slots) {
        return -1;
    }
    pin_map_index(map, slots);
    *out = declaration;
    out->ptr = map;
    return 0;
}

static int read_nested_array(
    pin_value declaration,
    const av_value *document,
    pin_value *out,
    document_reader *reader
)
{
    const av_value *items = av_obj_get(document, "items");
    if (!av_is_obj(document) || av_obj_size(document) != 2 ||
        !av_is_arr(items) || av_arr_size(items) > DOCUMENT_MAX_VALUES ||
        av_arr_size(items) >
            (DOCUMENT_MAX_BYTES - sizeof(pin_array)) / sizeof(pin_value)) {
        read_error(reader, "invalid nested array document");
        return -1;
    }
    pin_value element;
    const pin_array *schema = declaration.ptr;
    pin_value first = schema && schema->count ? pin_array_get(schema, 0)
                                              : (pin_value){ 0 };
    if (read_shape(
            reader,
            av_obj_get(document, "element_type"),
            schema && schema->count ? &first : NULL,
            &element
        ) ||
        element.type != declaration.type || element.container == PIN_CONTAINER_SINGLE) {
        read_error(reader, "invalid nested array element type");
        return -1;
    }
    int count = (int)av_arr_size(items);
    pin_array *array = allocate(reader, sizeof *array + (size_t)count * sizeof(pin_value));
    if (!array) {
        return -1;
    }
    *array = (pin_array){ .element_type = declaration.type,
                          .type_name = declaration.type_name,
                          .count = count,
                          .capacity = count,
                          .items = (pin_value *)(array + 1) };
    for (int i = 0; i < count; i++) {
        if (read_typed(element, av_arr_get(items, (size_t)i), &array->items[i], reader)) {
            return -1;
        }
    }
    pin_arena_trace_array(reader->storage, array);
    *out = declaration;
    out->ptr = array;
    return 0;
}
