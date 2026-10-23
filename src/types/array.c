#include "types/array.h"

#include "support/arena.h"
#include "support/xalloc.h"
#include "types/array_internal.h"
#include "types/catalog.h"
#include "types/copy_internal.h"
#include "types/map.h"
#include "types/pointer.h"
#include "types/record.h"
#include "types/scalar.h"
#include "types/schema.h"
#include "types/text.h"
#include "types/trace.h"
#include "types/union.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int value_string(pin_type type)
{
    return type == PIN_STR || type == PIN_NAME;
}

static bool packed_layout(const type_desc *type, int depth, type_resolver resolver)
{
    if (!type || !type->size || type->field_count < 0 ||
        (type->field_count && !type->fields) || depth > 16) {
        return false;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->offset > type->size || field->size > type->size - field->offset) {
            return false;
        }
        if (field->type == PIN_STRUCT) {
            const type_desc *nested = type_resolve(resolver, field->type_name);
            if (!packed_layout(nested, depth + 1, resolver) ||
                nested->size != field->size) {
                return false;
            }
        } else if (
            !((field->type >= PIN_BOOL && field->type <= PIN_F64) ||
              field->type == PIN_HANDLE || field->type == PIN_ENUM ||
              field->type == PIN_POINTER || field->type == PIN_QUERY) ||
            field->size != behavior_scalar_size(field->type)
        ) {
            return false;
        }
    }
    return true;
}
static size_t element_stride(pin_type storage, const char *name, const pin_record *record, const pin_schema *schema)
{
    if (name && !strncmp(name, "__collection_", 13)) {
        return 0;
    }
    if ((storage >= PIN_BOOL && storage <= PIN_F64) || storage == PIN_HANDLE ||
        storage == PIN_ENUM || storage == PIN_POINTER || storage == PIN_QUERY) {
        return record ? 0 : behavior_scalar_size(storage);
    }
    if (storage != PIN_STRUCT) {
        return 0;
    }
    pin_value shape = { .type = PIN_STRUCT,
                        .type_name = name,
                        .record = record,
                        .schema = schema };
    const type_desc *type = pin_value_type(&shape);
    type_resolver resolver = schema ? pin_schema_resolver(schema)
                                    : (type_resolver){ 0 };
    return packed_layout(type, 0, resolver) ? type->size : 0;
}
size_t pin_array_element_stride(pin_type storage, const char *name, const pin_record *record)
{
    return element_stride(storage, name, record, NULL);
}
bool pin_array_valid(const pin_array *array)
{
    if (!array || array->count < 0 || array->capacity < array->count) {
        return false;
    }
    if (array->stride) {
        return !array->items &&
               array->stride == element_stride(
                                    array->element_type,
                                    array->type_name,
                                    array->record,
                                    array->schema
                                ) &&
               (!array->count || array->data) &&
               (size_t)array->capacity <= SIZE_MAX / array->stride;
    }
    return !array->data && (!array->count || array->items) &&
           (size_t)array->capacity <= SIZE_MAX / sizeof(pin_value);
}
size_t pin_array_storage_bytes(const pin_array *array)
{
    return array ? (size_t)array->capacity *
                       (array->stride ? array->stride : sizeof(pin_value))
                 : 0;
}
void *pin_array_data(const pin_array *array, int index)
{
    assert(array && index >= 0 && index < array->count);
    if (array->stride) {
        return (char *)array->data + (size_t)index * array->stride;
    }
    pin_value *item = &array->items[index];
    return item->type == PIN_STRUCT && item->container == PIN_CONTAINER_SINGLE
             ? item->ptr
             : &item->d;
}
pin_value pin_array_get(const pin_array *array, int index)
{
    assert(array && index >= 0 && index < array->count);
    if (!array->stride) {
        pin_value value = array->items[index];
        if (value.type == PIN_STRUCT && !value.schema) {
            value.schema = array->schema;
        }
        return value;
    }
    pin_value value = { .type = array->element_type,
                        .type_name = array->type_name,
                        .record = array->record,
                        .schema = array->schema };
    const void *data = pin_array_data(array, index);
    if (value.type == PIN_STRUCT) {
        value.ptr = (void *)data;
    } else {
        memcpy(&value.d, data, array->stride);
    }
    return value;
}
static const pin_schema *element_schema(const pin_array *array)
{
    if (array->schema || array->stride || array->element_type != PIN_STRUCT) {
        return array->schema;
    }
    for (int i = 0; i < array->count; i++) {
        const pin_value *value = &array->items[i];
        if (value->schema && value->type == PIN_STRUCT && value->type_name &&
            array->type_name && !strcmp(value->type_name, array->type_name)) {
            return value->schema;
        }
    }
    return NULL;
}
static bool native_layout_same(
    const pin_schema *left,
    const pin_schema *right,
    const char *name,
    const pin_record *record
)
{
    if (left == right) {
        return true;
    }
    const type_desc *a_type = record ? &record->type
                            : left   ? pin_schema_type(left, name)
                                     : type_catalog_find(name);
    const type_desc *b_type = record ? &record->type
                            : right  ? pin_schema_type(right, name)
                                     : type_catalog_find(name);
    uint64_t a = left ? pin_schema_layout_type(left, a_type)
                      : type_layout_hash(a_type);
    uint64_t b = right ? pin_schema_layout_type(right, b_type)
                       : type_layout_hash(b_type);
    return a && a == b;
}
static bool compatible(const pin_array *array, const pin_value *value)
{
    bool nominal = value->type == PIN_STRUCT || value->type == PIN_ENUM ||
                   value->type == PIN_HANDLE || value->type == PIN_POINTER ||
                   value->type == PIN_QUERY;
    return value->container == PIN_CONTAINER_SINGLE &&
           value->type == array->element_type &&
           pin_record_same(value->record, array->record) &&
           (value->type != PIN_STRUCT || native_layout_same(
                                             array->schema,
                                             value->schema,
                                             array->type_name,
                                             array->record
                                         )) &&
           (!nominal || !strcmp(
                            value->type_name ? value->type_name : "",
                            array->type_name ? array->type_name : ""
                        ));
}
int pin_array_set(pin_array *array, int index, const pin_value *value)
{
    if (!array || !value || index < 0 || index >= array->count) {
        return -1;
    }
    if (array->stride) {
        if (!compatible(array, value)) {
            return -1;
        }
        const void *data = value->type == PIN_STRUCT ? value->ptr : &value->d;
        if (!data) {
            return -1;
        }
        memmove(pin_array_data(array, index), data, array->stride);
    } else {
        array->items[index] = *value;
    }
    array->buckets = NULL;
    array->bucket_mask = 0;
    return 0;
}
void pin_array_swap(pin_array *array, int first, int second)
{
    assert(
        array && first >= 0 && second >= 0 && first < array->count &&
        second < array->count
    );
    if (array->stride) {
        unsigned char *a = pin_array_data(array, first),
                      *b = pin_array_data(array, second);
        for (size_t i = 0; i < array->stride; i++) {
            unsigned char byte = a[i];
            a[i] = b[i];
            b[i] = byte;
        }
    } else {
        pin_value value = array->items[first];
        array->items[first] = array->items[second];
        array->items[second] = value;
    }
    array->buckets = NULL;
    array->bucket_mask = 0;
}

static char *arena_string(arena *a, const char *s, pin_copy_kind kind)
{
    if (!s) {
        return NULL;
    }
    size_t size = strlen(s) + 1;
    char *copy = (char *)arena_alloc(a, size);
    if (copy) {
        memcpy(copy, s, size);
        pin_copy_record(kind, size);
    }
    return copy;
}

static int copy_text(arena *storage, pin_value *out, const pin_value *source)
{
    pin_string text = pin_text(*source);
    pin_value copy;
    if (pin_value_text(storage, &copy, text.bytes, text.length)) {
        return -1;
    }
    out->text = copy.text;
    return 0;
}

pin_array *pin_array_new(pin_type element_type, const char *type_name, int capacity)
{
    if (capacity < 0) {
        return NULL;
    }
    pin_array *array = (pin_array *)xcalloc(1, sizeof *array);
    array->element_type = element_type;
    array->type_name = type_name;
    if (capacity) {
        array->items = (pin_value *)xcalloc((size_t)capacity, sizeof *array->items);
        array->capacity = capacity;
    }
    return array;
}

int pin_array_reserve_owned(arena *a, pin_array *array, int count)
{
    if (count <= array->capacity) {
        return 0;
    }
    if (array->capacity > INT_MAX / 2) {
        return -1;
    }
    int capacity = array->capacity ? array->capacity * 2 : 8;
    capacity = capacity < count ? count : capacity;
    size_t width = array->stride ? array->stride : sizeof(pin_value);
    if ((size_t)capacity > SIZE_MAX / width) {
        return -1;
    }
    char *items = arena_alloc(a, (size_t)capacity * width);
    if (array->count) {
        memcpy(
            items,
            array->stride ? array->data : (void *)array->items,
            (size_t)array->count * width
        );
    }
    memset(items + (size_t)count * width, 0, (size_t)(capacity - count) * width);
    if (array->stride) {
        array->data = items;
    } else {
        array->items = (pin_value *)items;
    }
    array->capacity = capacity;
    return 0;
}
int pin_array_reserve(pin_array *array, int capacity)
{
    if (!array || capacity < 0) {
        return -1;
    }
    if (capacity <= array->capacity) {
        return 0;
    }
    if (array->stride) {
        if ((size_t)capacity > SIZE_MAX / array->stride) {
            return -1;
        }
        array->data = xrealloc(array->data, (size_t)capacity * array->stride);
        memset(
            (char *)array->data + (size_t)array->capacity * array->stride,
            0,
            (size_t)(capacity - array->capacity) * array->stride
        );
        array->capacity = capacity;
        return 0;
    }
    array->items = (pin_value *)
        xrealloc(array->items, (size_t)capacity * sizeof *array->items);
    memset(
        array->items + array->capacity,
        0,
        (size_t)(capacity - array->capacity) * sizeof *array->items
    );
    array->capacity = capacity;
    return 0;
}

static int pin_array_insert(pin_array *array, int index, const pin_value *value)
{
    if (!array || !value || index < 0 || index > array->count ||
        value->type != array->element_type ||
        !pin_record_same(array->record, value->record) ||
        (array->count == array->capacity && array->capacity > INT_MAX / 2)) {
        return -1;
    }
    pin_value copy = { 0 };
    void *payload = NULL;
    if (array->stride) {
        if (!compatible(array, value) || (value->type == PIN_STRUCT && !value->ptr)) {
            return -1;
        }
        payload = xmalloc(array->stride);
        memcpy(
            payload,
            value->type == PIN_STRUCT ? value->ptr : &value->d,
            array->stride
        );
    } else if (pin_value_copy(&copy, value)) {
        return -1;
    }
    if (array->count == array->capacity) {
        int capacity = array->capacity ? array->capacity * 2 : 4;
        if (pin_array_reserve(array, capacity)) {
            free(payload);
            if (!array->stride) {
                pin_value_destroy(&copy);
            }
            return -1;
        }
    }
    array->buckets = NULL;
    array->bucket_mask = 0;
    if (array->stride) {
        char *at = (char *)array->data + (size_t)index * array->stride;
        memmove(at + array->stride, at, (size_t)(array->count - index) * array->stride);
        memcpy(at, payload, array->stride);
        free(payload);
    } else {
        memmove(
            array->items + index + 1,
            array->items + index,
            (size_t)(array->count - index) * sizeof *array->items
        );
        array->items[index] = copy;
    }
    pin_copy_record(PIN_COPY_VALUE_HEADER, 1);
    array->count++;
    return index;
}

int pin_array_push(pin_array *array, const pin_value *value)
{
    return pin_array_insert(array, array ? array->count : -1, value);
}

static pin_array *array_clone_heap(const pin_array *src)
{
    if (!src) {
        return NULL;
    }
    if (!pin_array_valid(src)) {
        return NULL;
    }
    if (src->stride) {
        pin_array *dst = xcalloc(1, sizeof *dst);
        *dst = *src;
        dst->capacity = src->count;
        dst->buckets = NULL;
        dst->bucket_mask = 0;
        size_t bytes = (size_t)src->count * src->stride;
        dst->data = bytes ? xmalloc(bytes) : NULL;
        if (bytes) {
            memcpy(dst->data, src->data, bytes);
        }
        return dst;
    }
    pin_array *dst = pin_array_new(src->element_type, src->type_name, src->count);
    dst->record = src->record;
    dst->schema = src->schema;
    dst->count = src->count;
    for (int i = 0; i < src->count; i++) {
        if (pin_value_copy(&dst->items[i], &src->items[i])) {
            pin_array_free(dst);
            return NULL;
        }
    }
    return dst;
}

static pin_array *pin_array_clone(const pin_array *src)
{
    return array_clone_heap(src);
}

static pin_array *array_clone_arena(arena *a, const pin_array *src)
{
    if (!a || !src) {
        return NULL;
    }
    if (src->stride || src->schema) {
        return pin_array_snapshot_to_arena(a, src);
    }
    size_t size = sizeof(pin_array) + (size_t)src->count * sizeof(pin_value);
    pin_array *dst = (pin_array *)arena_alloc(a, size);
    if (!dst) {
        return NULL;
    }
    *dst = (pin_array){ 0 };
    dst->element_type = src->element_type;
    dst->record = src->record;
    dst->type_name = arena_string(a, src->type_name, PIN_COPY_NOMINAL);
    dst->count = src->count;
    dst->capacity = src->count;
    dst->items = (pin_value *)(dst + 1);
    pin_arena_trace_array(a, dst);
    memset(dst->items, 0, (size_t)src->count * sizeof *dst->items);
    for (int i = 0; i < src->count; i++) {
        if (pin_value_copy_to_arena(a, &dst->items[i], &src->items[i])) {
            return NULL;
        }
    }
    return dst;
}

pin_array *pin_array_clone_to_arena(arena *a, const pin_array *src)
{
    return array_clone_arena(a, src);
}

static int snapshot_fields(arena *a, const type_desc *type, void *data, int depth, type_resolver resolver)
{
    if (!type || !type->size || !data || depth == 16) {
        return -1;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->offset > type->size || field->size > type->size - field->offset) {
            return -1;
        }
        unsigned char *member = (unsigned char *)data + field->offset;
        if (field->type == PIN_STRUCT) {
            const type_desc *nested = type_resolve(resolver, field->type_name);
            if (!nested || !nested->size || nested->size != field->size ||
                snapshot_fields(a, nested, member, depth + 1, resolver)) {
                return -1;
            }
        } else if (value_string(field->type)) {
            if (field->size != sizeof(const char *)) {
                return -1;
            }
            if (field->type == PIN_STR && field->text_descriptor) {
                pin_value source = { .type = PIN_STR,
                                     .text = *(const pin_string **)member },
                          copy;
                if (copy_text(a, &copy, &source)) {
                    return -1;
                }
                *(const pin_string **)member = copy.text;
            } else {
                const char *text = *(const char **)member;
                *(const char **)member = arena_string(a, text, PIN_COPY_TEXT);
            }
        } else if (field->type == PIN_POINTER || field->type == PIN_QUERY) {
            if (field->size != sizeof(void *)) {
                return -1;
            }
        } else if (field->type == PIN_UNION) {
            if (field->size != sizeof(void *)) {
                return -1;
            }
            pin_value source = { .type = PIN_UNION, .ptr = *(void **)member }, copy;
            if (pin_value_snapshot_to_arena(a, &copy, &source)) {
                return -1;
            }
            *(void **)member = copy.ptr;
        }
    }
    return 0;
}

int pin_struct_snapshot_fields_resolved(arena *a, const type_desc *type, void *data, type_resolver resolver)
{
    if (resolver.find && a->collectable) {
        return -1;
    }
    int result = snapshot_fields(a, type, data, 0, resolver);
    if (!result) {
        pin_arena_trace_struct(a, data, type);
    }
    return result;
}
int pin_struct_snapshot_fields(arena *a, const type_desc *type, void *data)
{
    return pin_struct_snapshot_fields_resolved(a, type, data, (type_resolver){ 0 });
}
static int snapshot_record(arena *a, pin_value *dst, const pin_value *src, const type_desc *type)
{
    if (!type || !type->size || (dst->record && !pin_value_record_current(dst))) {
        return -1;
    }
    if (!src->ptr) {
        return 0;
    }
    dst->ptr = arena_alloc(a, type->size);
    memcpy(dst->ptr, src->ptr, type->size);
    pin_copy_record(PIN_COPY_RECORD, type->size);
    if (dst->schema) {
        int status = snapshot_fields(a, type, dst->ptr, 0, pin_schema_resolver(dst->schema));
        if (!status) {
            pin_schema_trace_struct(a, dst->ptr, dst->schema, type);
        }
        return status;
    }
    return pin_struct_snapshot_fields(a, type, dst->ptr);
}
static int snapshot_value(arena *a, pin_value *dst, const pin_value *src)
{
    *dst = *src;
    if (src->schema) {
        dst->schema = pin_schema_snapshot(a, src->schema);
    }
    pin_copy_record(PIN_COPY_VALUE_HEADER, 1);
    dst->type_name = src->record
                       ? NULL
                       : arena_string(a, src->type_name, PIN_COPY_NOMINAL);
    if (src->record) {
        if (!pin_value_type(src)) {
            return -1;
        }
        dst->record = pin_record_snapshot(a, src->record);
        if (!dst->record) {
            return -1;
        }
        dst->type_name = dst->record->type.name;
    }
    if (src->container == PIN_CONTAINER_MAP) {
        dst->ptr = pin_map_snapshot_to_arena(a, src->ptr);
        return src->ptr && !dst->ptr ? -1 : 0;
    }
    if (src->container >= PIN_CONTAINER_ARRAY) {
        const pin_array *array = src->ptr;
        if (array && !pin_record_same(src->record, array->record)) {
            return -1;
        }
        pin_array source;
        if (array) {
            source = *array;
            source.record = dst->record;
            if (dst->schema) {
                source.schema = dst->schema;
            }
        }
        dst->ptr = pin_array_snapshot_to_arena(a, array ? &source : NULL);
        if (dst->ptr) {
            dst->schema = ((const pin_array *)dst->ptr)->schema;
        }
        if (src->container == PIN_CONTAINER_SET) {
            pin_array_index(a, dst->ptr);
        }
        return src->ptr && !dst->ptr ? -1 : 0;
    }
    if (src->type == PIN_STR) {
        return copy_text(a, dst, src);
    }
    if (src->type == PIN_NAME) {
        dst->str = arena_string(a, src->str, PIN_COPY_TEXT);
        return 0;
    }
    if (src->type == PIN_UNION && src->ptr) {
        const pin_union *value = src->ptr;
        const char *name = dst->type_name;
        int result = pin_union_value(a, value->tag, value->values, value->count, dst);
        dst->type_name = name;
        return result;
    }
    if (src->type != PIN_STRUCT || !src->ptr) {
        return 0;
    }
    return snapshot_record(a, dst, src, pin_value_type(dst));
}
int pin_value_snapshot_to_arena(arena *a, pin_value *dst, const pin_value *src)
{
    pin_value copy;
    if (snapshot_value(a, &copy, src)) {
        return -1;
    }
    *dst = copy;
    return 0;
}
pin_array *pin_array_snapshot_allocate(arena *a, pin_type type, const char *name, int count)
{
    assert(count >= 0);
    pin_array *dst = arena_alloc(a, sizeof *dst + (size_t)count * sizeof(pin_value));
    *dst = (pin_array){ .element_type = type,
                        .type_name = arena_string(a, name, PIN_COPY_NOMINAL),
                        .count = count,
                        .capacity = count,
                        .items = (pin_value *)(dst + 1) };
    pin_arena_trace_array(a, dst);
    return dst;
}
pin_array *pin_array_allocate(arena *a, pin_type type, const char *name, int count)
{
    pin_array *dst = pin_array_snapshot_allocate(a, type, name, count);
    for (int i = 0; i < count; i++) {
        dst->items[i] = (pin_value){ .type = type, .type_name = dst->type_name };
    }
    return dst;
}
/* Whether a copy of `array` may hold its elements inline: a packed array's
 * elements are, and a boxed one's must each be a present value of exactly a
 * packable element type. */
static bool packable(const pin_array *array)
{
    if (array->stride) {
        return true;
    }
    pin_array shape = *array;
    shape.schema = element_schema(array);
    if (!element_stride(shape.element_type, shape.type_name, shape.record, shape.schema)) {
        return false;
    }
    for (int i = 0; i < array->count; i++) {
        if (!compatible(&shape, &array->items[i]) ||
            (array->items[i].type == PIN_STRUCT && !array->items[i].ptr)) {
            return false;
        }
    }
    return true;
}
static pin_array *snapshot_like(arena *a, const pin_array *src, int count, bool packed)
{
    if (!src || count < 0 ||
        (size_t)count > (SIZE_MAX - sizeof(pin_array)) / sizeof(pin_value)) {
        return NULL;
    }
    const pin_record *record = pin_record_snapshot(a, src->record);
    const pin_schema *schema = pin_schema_snapshot(a, element_schema(src));
    if (src->record && (!record || src->element_type != PIN_STRUCT || !src->type_name ||
                        strcmp(src->type_name, record->type.name))) {
        return NULL;
    }
    const char *name = record ? record->type.name : src->type_name;
    size_t stride = packed ? element_stride(src->element_type, name, record, schema)
                           : 0;
    if (stride && (size_t)count > SIZE_MAX / stride) {
        return NULL;
    }
    pin_array *dst;
    if (stride) {
        dst = arena_alloc(a, sizeof *dst);
        *dst = (pin_array){
            .element_type = src->element_type,
            .type_name = record ? name : arena_string(a, name, PIN_COPY_NOMINAL),
            .record = record,
            .schema = schema,
            .count = count,
            .capacity = count,
            .stride = stride,
            .data = count ? arena_alloc(a, (size_t)count * stride) : NULL
        };
        if (count) {
            memset(dst->data, 0, (size_t)count * stride);
        }
        pin_arena_trace_array(a, dst);
    } else {
        dst = pin_array_snapshot_allocate(a, src->element_type, record ? NULL : name, count);
        dst->record = record;
        dst->schema = schema;
        if (record) {
            dst->type_name = record->type.name;
        }
        memset(dst->items, 0, (size_t)count * sizeof *dst->items);
    }
    return dst;
}
pin_array *pin_array_snapshot_like(arena *a, const pin_array *src, int count)
{
    return snapshot_like(a, src, count, src && packable(src));
}
int pin_array_snapshot_set(arena *a, pin_array *array, int index, const pin_value *value)
{
    if (!array || !value || index < 0 || index >= array->count) {
        return -1;
    }
    if (array->stride && value->type == PIN_STRUCT && !value->ptr &&
        compatible(array, value)) {
        if ((size_t)array->count > SIZE_MAX / sizeof(pin_value)) {
            return -1;
        }
        /* Inline records have no null representation. Keep existing rows
         * pointing into their owned allocation and box this nullable result. */
        pin_value *items = arena_alloc(a, (size_t)array->count * sizeof *items);
        for (int i = 0; i < array->count; i++) {
            items[i] = pin_array_get(array, i);
        }
        array->items = items;
        array->data = NULL;
        array->stride = 0;
    }
    if (array->stride) {
        if (!compatible(array, value)) {
            return -1;
        }
        if (pin_array_set(array, index, value)) {
            return -1;
        }
        pin_copy_record(
            value->type == PIN_STRUCT ? PIN_COPY_RECORD : PIN_COPY_VALUE_BUFFER,
            array->stride
        );
        return 0;
    }
    if (!array->record) {
        return pin_value_snapshot_to_arena(a, &array->items[index], value);
    }
    return pin_array_snapshot_items(a, array->record, &array->items[index], value, 1);
}
int pin_array_snapshot_copy(arena *a, pin_array *out, int to, const pin_array *items, int from, int count)
{
    if (!out || !items || to < 0 || from < 0 || count < 0 || to > out->count ||
        count > out->count - to || from > items->count || count > items->count - from) {
        return -1;
    }
    if (count && out->stride && out->stride == items->stride) {
        pin_value shape = pin_array_get(items, from);
        if (compatible(out, &shape)) {
            size_t bytes = (size_t)count * out->stride;
            memmove(pin_array_data(out, to), pin_array_data(items, from), bytes);
            out->buckets = NULL;
            out->bucket_mask = 0;
            pin_copy_record(shape.type == PIN_STRUCT ? PIN_COPY_RECORD : PIN_COPY_VALUE_BUFFER, bytes);
            return 0;
        }
    }
    /* Boxed records copy as one run, which matches each distinct record once. */
    if (count && out->record && !out->stride && !items->stride) {
        return pin_array_snapshot_items(a, out->record, out->items + to, items->items + from, count);
    }
    for (int i = 0; i < count; i++) {
        pin_value item = pin_array_get(items, from + i);
        if (pin_array_snapshot_set(a, out, to + i, &item)) {
            return -1;
        }
    }
    return 0;
}
/* Destination metadata is already owned by the enclosing snapshot or private
 * construction proof. Each original row's record must match it. */
static int snapshot_items(arena *a, const pin_record *record, pin_value *out, const pin_value *items, int count)
{
    const pin_record *checked = record;
    for (int i = 0; i < count; i++) {
        const pin_value *value = &items[i];
        if (value->record != checked) {
            if (!pin_record_same(record, value->record)) {
                return -1;
            }
            checked = value->record;
        }
        if (!record) {
            if (pin_value_snapshot_to_arena(a, &out[i], value)) {
                return -1;
            }
            continue;
        }
        if (value->type != PIN_STRUCT || value->container != PIN_CONTAINER_SINGLE ||
            !value->type_name || strcmp(value->type_name, record->type.name)) {
            return -1;
        }
        pin_value copy = *value;
        copy.record = record;
        copy.schema = pin_schema_snapshot(a, value->schema);
        copy.type_name = record->type.name;
        pin_copy_record(PIN_COPY_VALUE_HEADER, 1);
        if (snapshot_record(a, &copy, value, &record->type)) {
            return -1;
        }
        out[i] = copy;
    }
    return 0;
}

int pin_array_snapshot_items(arena *a, const pin_record *record, pin_value *out, const pin_value *items, int count)
{
    if (count < 0 || (count && (!out || !items)) ||
        (record && !arena_contains(a, record))) {
        return -1;
    }
    return snapshot_items(a, record, out, items, count);
}

int pin_array_snapshot_owned_items(
    arena *a,
    const pin_record *record,
    pin_value *out,
    const pin_value *items,
    int count
)
{
    assert(count >= 0 && (!count || (out && items)));
    return snapshot_items(a, record, out, items, count);
}

pin_value pin_array_add(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    const pin_array *source = inputs && inputs[0].container == PIN_CONTAINER_ARRAY
                                ? inputs[0].ptr
                                : NULL;
    if (!source) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    pin_array *array = pin_array_snapshot_like(a, source, source->count + 1);
    if (array && (pin_array_snapshot_copy(a, array, 0, source, 0, source->count) ||
                  pin_array_snapshot_set(a, array, source->count, &inputs[1]))) {
        array = NULL;
    }
    return (pin_value){ .type = source->element_type,
                        .container = PIN_CONTAINER_ARRAY,
                        .type_name = array ? array->type_name : NULL,
                        .record = array ? array->record : NULL,
                        .schema = array ? array->schema : NULL,
                        .ptr = array };
}
const char *pin_array_add_call(arena *a, const pin_value *inputs, int count, pin_value *out)
{
    pin_value value = pin_array_add(inputs, count, a);
    if (value.type == PIN_FLOW) {
        return "data function produced an invalid result";
    }
    if (!value.ptr) {
        return "arena allocation failed";
    }
    *out = value;
    return NULL;
}
pin_array *pin_array_snapshot_to_arena(arena *a, const pin_array *src)
{
    if (!pin_array_valid(src) ||
        (size_t)src->count > (SIZE_MAX - sizeof(pin_array)) / sizeof(pin_value)) {
        return NULL;
    }
    pin_array *dst = snapshot_like(a, src, src->count, packable(src));
    if (!dst) {
        return NULL;
    }
    if (pin_array_snapshot_copy(a, dst, 0, src, 0, src->count)) {
        return NULL;
    }
    return dst;
}

void pin_array_free(pin_array *array)
{
    if (!array) {
        return;
    }
    for (int i = 0; !array->stride && i < array->count; i++) {
        pin_value_destroy(&array->items[i]);
    }
    free(array->items);
    free(array->data);
    free(array);
}

int pin_value_copy(pin_value *dst, const pin_value *src)
{
    if (!dst || !src) {
        return -1;
    }
    *dst = *src;
    pin_copy_record(PIN_COPY_VALUE_HEADER, 1);
    if (src->container == PIN_CONTAINER_MAP) {
        dst->ptr = pin_map_clone(src->ptr);
        return !src->ptr || dst->ptr ? 0 : -1;
    }
    if (src->container >= PIN_CONTAINER_ARRAY) {
        dst->ptr = pin_array_clone((const pin_array *)src->ptr);
        return !src->ptr || dst->ptr ? 0 : -1;
    }
    if (src->type == PIN_UNION) {
        return pin_union_copy(dst, src);
    }
    if (src->type == PIN_STR) {
        return copy_text(NULL, dst, src);
    }
    if (src->type == PIN_NAME && src->str) {
        dst->str = xstrdup(src->str);
        if (pin_copy_current) {
            pin_copy_record(PIN_COPY_TEXT, strlen(src->str) + 1);
        }
        return 0;
    }
    if (src->type == PIN_STRUCT && src->ptr) {
        return 0;
    }
    return 0;
}

int pin_value_copy_to_arena(arena *a, pin_value *dst, const pin_value *src)
{
    if (!a || !dst || !src) {
        return -1;
    }
    const pin_array *array = src->container == PIN_CONTAINER_ARRAY ||
                                     src->container == PIN_CONTAINER_SET
                               ? src->ptr
                               : NULL;
    if (src->schema || (array && array->schema)) {
        return pin_value_snapshot_to_arena(a, dst, src);
    }
    *dst = *src;
    pin_copy_record(PIN_COPY_VALUE_HEADER, 1);
    if (src->type == PIN_UNION && src->container == PIN_CONTAINER_SINGLE) {
        return pin_value_snapshot_to_arena(a, dst, src);
    }
    if (src->container == PIN_CONTAINER_MAP) {
        dst->ptr = pin_map_copy_to_arena(a, src->ptr);
        return !src->ptr || dst->ptr ? 0 : -1;
    }
    if (src->container >= PIN_CONTAINER_ARRAY) {
        dst->ptr = pin_array_clone_to_arena(a, (const pin_array *)src->ptr);
        if (src->container == PIN_CONTAINER_SET) {
            pin_array_index(a, dst->ptr);
        }
        return !src->ptr || dst->ptr ? 0 : -1;
    }
    if (src->type == PIN_STR) {
        return copy_text(a, dst, src);
    }
    if (src->type == PIN_NAME && src->str) {
        dst->str = arena_string(a, src->str, PIN_COPY_TEXT);
    }
    return 0;
}

void pin_value_destroy(pin_value *value)
{
    if (!value) {
        return;
    }
    if (value->container == PIN_CONTAINER_MAP) {
        pin_map_free(value->ptr);
    } else if (value->container >= PIN_CONTAINER_ARRAY) {
        pin_array_free((pin_array *)value->ptr);
    } else if (value->type == PIN_STR) {
        free((void *)value->text);
    } else if (value->type == PIN_NAME) {
        free((void *)value->str);
    } else if (value->type == PIN_UNION) {
        pin_union_destroy(value->ptr);
    }
    /* Struct values are borrowed until a type-specific copy callback exists.
     * Their storage may belong to an arena, graph, or host object. */
    memset(value, 0, sizeof *value);
}

int pin_value_equal(const pin_value *a, const pin_value *b)
{
    if (!a || !b || a->type != b->type || a->container != b->container) {
        return 0;
    }
    if (!pin_record_same(a->record, b->record)) {
        return 0;
    }
    if (a->container == PIN_CONTAINER_MAP) {
        return pin_map_equal(a->ptr, b->ptr);
    }
    if (a->container >= PIN_CONTAINER_ARRAY) {
        return pin_array_equal((const pin_array *)a->ptr, (const pin_array *)b->ptr);
    }
    if (value_string(a->type)) {
        return pin_text_equal(pin_text(*a), pin_text(*b));
    }
    if (a->type == PIN_UNION) {
        const pin_union *left = a->ptr, *right = b->ptr;
        if (!left || !right) {
            return left == right;
        }
        if (left->tag != right->tag || left->count != right->count) {
            return 0;
        }
        for (int i = 0; i < left->count; i++) {
            if (!pin_value_equal(&left->values[i], &right->values[i])) {
                return 0;
            }
        }
        return 1;
    }
    if (a->type == PIN_STRUCT) {
        if (strcmp(a->type_name ? a->type_name : "", b->type_name ? b->type_name : "")) {
            return 0;
        }
        if (!a->ptr || !b->ptr) {
            return a->ptr == b->ptr;
        }
        if (!native_layout_same(a->schema, b->schema, a->type_name, a->record)) {
            return 0;
        }
        if (a->record &&
            (!pin_value_record_current(a) || !pin_value_record_current(b))) {
            return 0;
        }
        const type_desc *type = pin_value_type(a);
        if (!type || !type->field_count) {
            return a->ptr == b->ptr;
        }
        for (int i = 0; i < type->field_count; i++) {
            pin_value left, right;
            const type_field *field = &type->fields[i];
            if (field->type == PIN_STR) {
                const void *x = (const char *)a->ptr + field->offset;
                const void *y = (const char *)b->ptr + field->offset;
                if (field->text_descriptor) {
                    left = (pin_value){ .type = PIN_STR,
                                        .text = *(const pin_string *const *)x };
                    right = (pin_value){ .type = PIN_STR,
                                         .text = *(const pin_string *const *)y };
                } else {
                    left = (pin_value){ .type = PIN_NAME,
                                        .str = *(const char *const *)x };
                    right = (pin_value){ .type = PIN_NAME,
                                         .str = *(const char *const *)y };
                }
            } else {
                if (pin_read_field(NULL, a->ptr, field, &left) ||
                    pin_read_field(NULL, b->ptr, field, &right)) {
                    return 0;
                }
                if (field->type == PIN_STRUCT) {
                    left.schema = a->schema;
                    right.schema = b->schema;
                }
            }
            if (!pin_value_equal(&left, &right)) {
                return 0;
            }
        }
        return 1;
    }
    /* Equality concerns values, not padding, unused union bytes or the address
     * of a type-name string. Numeric rules match the graph's Equal operation. */
    switch (a->type) {
        case PIN_BOOL: return a->b == b->b;
        case PIN_I8: return a->i8 == b->i8;
        case PIN_U8: return a->u8 == b->u8;
        case PIN_I16: return a->i16 == b->i16;
        case PIN_U16: return a->u16 == b->u16;
        case PIN_I32: return a->i == b->i;
        case PIN_U32: return a->u == b->u;
        case PIN_I64: return a->i64 == b->i64;
        case PIN_U64: return a->u64 == b->u64;
        case PIN_TASK: return a->u64 == b->u64;
        case PIN_QUERY:
            return !strcmp(a->type_name ? a->type_name : "", b->type_name ? b->type_name : "") &&
                   a->ptr == b->ptr;
        case PIN_POINTER:
            return !strcmp(a->type_name ? a->type_name : "", b->type_name ? b->type_name : "") &&
                   pin_pointer_equal(a->ptr, b->ptr);
        case PIN_F32: return a->f == b->f;
        case PIN_F64: return a->d == b->d;
        case PIN_ENUM:
        case PIN_HANDLE:
            return (a->type == PIN_ENUM ? a->e == b->e : a->u64 == b->u64) &&
                   strcmp(a->type_name ? a->type_name : "", b->type_name ? b->type_name : "") ==
                       0;
        default: return 0;
    }
}

int pin_array_equal(const pin_array *a, const pin_array *b)
{
    if (!a || !b || a->element_type != b->element_type || a->count != b->count) {
        return 0;
    }
    if (!pin_record_same(a->record, b->record)) {
        return 0;
    }
    for (int i = 0; i < a->count; i++) {
        pin_value left = pin_array_get(a, i), right = pin_array_get(b, i);
        if (!pin_value_equal(&left, &right)) {
            return 0;
        }
    }
    return 1;
}
