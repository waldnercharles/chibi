#include "types/array.h"
#include "types/catalog.h"
#include "types/record.h"
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

typedef struct
{
    arena *storage;
    const pin_record *record;
    char *error;
    size_t error_size, bytes;
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
    };
    if (field->type == PIN_STRUCT) {
        const type_desc *type = type_catalog_find(field->type_name);
        return type && field->size == type->size ? type->size : SIZE_MAX;
    }
    return field->type >= PIN_FLOW && field->type <= PIN_ENUM ? sizes[field->type]
                                                              : SIZE_MAX;
}
static int read_value(pin_type, const char *, const av_value *, pin_value *, document_reader *);
static int read_single(document_reader *, pin_type, const char *, const av_value *, pin_value *);

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
    pin_array *array = allocate(reader, sizeof *array + (size_t)count * sizeof(pin_value));
    if (!array) {
        return -1;
    }
    *array = (pin_array){ .element_type = element_type,
                          .type_name = reader->record ? reader->record->type.name
                                     : type_name
                                         ? own_text(reader, type_name, strlen(type_name))
                                         : NULL,
                          .record = reader->record,
                          .count = count,
                          .capacity = count,
                          .items = (pin_value *)(array + 1) };
    for (int i = 0; i < count; i++) {
        if (read_value(
                element_type,
                array->type_name,
                av_arr_get(document, (size_t)i),
                &array->items[i],
                reader
            )) {
            if (element_type != PIN_STRUCT) {
                read_error(reader, "invalid array item %d", i);
            }
            return -1;
        }
    }
    if (array->record) {
        pin_arena_trace_array(reader->storage, array);
    }
    *out = (pin_value){ .type = element_type,
                        .container = PIN_CONTAINER_ARRAY,
                        .type_name = array->type_name,
                        .record = array->record,
                        .ptr = array };
    return 0;
}

static int read_struct(const char *type_name, const av_value *document, pin_value *out, document_reader *reader)
{
    const type_desc *type = reader->record ? &reader->record->type
                                           : type_catalog_find(type_name);
    if (!type || !type->size || !av_is_obj(document)) {
        read_error(reader, "invalid struct default for %s", type_name ? type_name : "unknown type");
        return -1;
    }
    void *block = allocate(reader, type->size);
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
            result = read_value(field->type, field->type_name, value, &member, reader);
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
        pin_write_field(block, field, member);
    }
    *out = (pin_value){ .type = PIN_STRUCT,
                        .type_name = out->type_name,
                        .record = reader->record,
                        .ptr = block };
    if (reader->record) {
        pin_arena_trace_struct(reader->storage, block, type);
    }
    return 0;
}

static int read_value(pin_type t, const char *type_name, const av_value *v, pin_value *out, document_reader *reader)
{
    if (++reader->values > DOCUMENT_MAX_VALUES || reader->depth >= DOCUMENT_MAX_DEPTH) {
        read_error(reader, "document value exceeds traversal limit");
        return -1;
    }
    reader->depth++;
    int result = read_single(reader, t, type_name, v, out);
    reader->depth--;
    return result;
}

static int read_single(
    document_reader *reader,
    pin_type t,
    const char *type_name,
    const av_value *v,
    pin_value *out
)
{
    memset(out, 0, sizeof *out);
    out->type = t;
    if (t == PIN_STRUCT && reader->record) {
        out->record = reader->record;
        out->type_name = reader->record->type.name;
    } else {
        out->type_name = reader->storage && type_name
                           ? own_text(reader, type_name, strlen(type_name))
                           : type_name;
    }
    if (reader->failed) {
        return -1;
    }
    switch (t) {
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
        case PIN_NAME:
        case PIN_STR:
            if (!av_is_str(v) || strlen(av_get_str(v)) != av_get_len(v)) {
                return -1;
            }
            out->str = own_text(reader, av_get_str(v), av_get_len(v));
            if (!out->str) {
                return -1;
            }
            break;
        case PIN_STRUCT:
            return av_is_null(v) ? 0 : read_struct(type_name, v, out, reader);
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
    if (declaration.type < PIN_FLOW || declaration.type > PIN_ENUM) {
        read_error(&reader, "invalid document value type");
        return -1;
    }
    int result;
    switch (declaration.container) {
        case PIN_CONTAINER_SINGLE:
            result = read_value(declaration.type, declaration.type_name, document, &value, &reader);
            break;
        case PIN_CONTAINER_ARRAY:
        case PIN_CONTAINER_SET:
            result = read_array(declaration.type, declaration.type_name, document, &value, &reader);
            value.container = declaration.container;
            break;
        default:
            read_error(&reader, "unsupported document container");
            result = -1;
    }
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
