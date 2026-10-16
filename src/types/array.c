#include "types/array.h"

#include "support/arena.h"
#include "support/xalloc.h"
#include "types/array_internal.h"
#include "types/catalog.h"
#include "types/copy_internal.h"
#include "types/record.h"
#include "types/trace.h"
#include "types/union.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int value_string(pin_type type)
{
    return type == PIN_STR || type == PIN_NAME;
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

int pin_array_reserve(pin_array *array, int capacity)
{
    if (!array || capacity < 0) {
        return -1;
    }
    if (capacity <= array->capacity) {
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
        !pin_record_same(array->record, value->record)) {
        return -1;
    }
    if (array->count == array->capacity) {
        int capacity = array->capacity ? array->capacity * 2 : 4;
        if (pin_array_reserve(array, capacity)) {
            return -1;
        }
    }
    array->buckets = NULL;
    array->bucket_mask = 0;
    pin_value copy = { 0 };
    if (pin_value_copy(&copy, value)) {
        return -1;
    }
    memmove(
        array->items + index + 1,
        array->items + index,
        (size_t)(array->count - index) * sizeof *array->items
    );
    array->items[index] = copy;
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
    pin_array *dst = pin_array_new(src->element_type, src->type_name, src->count);
    dst->record = src->record;
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

static int snapshot_fields(arena *a, const type_desc *type, void *data, int depth)
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
            const type_desc *nested = type_catalog_find(field->type_name);
            if (!nested || !nested->size || nested->size != field->size ||
                snapshot_fields(a, nested, member, depth + 1)) {
                return -1;
            }
        } else if (value_string(field->type)) {
            if (field->size != sizeof(const char *)) {
                return -1;
            }
            const char *text = *(const char **)member;
            *(const char **)member = arena_string(a, text, PIN_COPY_TEXT);
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

int pin_struct_snapshot_fields(arena *a, const type_desc *type, void *data)
{
    int result = snapshot_fields(a, type, data, 0);
    if (!result) {
        pin_arena_trace_struct(a, data, type);
    }
    return result;
}
static int snapshot_record(arena *a, pin_value *dst, const pin_value *src, const type_desc *type)
{
    if (!type || !type->size) {
        return -1;
    }
    if (!src->ptr) {
        return 0;
    }
    dst->ptr = arena_alloc(a, type->size);
    memcpy(dst->ptr, src->ptr, type->size);
    pin_copy_record(PIN_COPY_RECORD, type->size);
    return pin_struct_snapshot_fields(a, type, dst->ptr);
}
static int snapshot_value(arena *a, pin_value *dst, const pin_value *src)
{
    *dst = *src;
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
    if (src->container >= PIN_CONTAINER_ARRAY) {
        const pin_array *array = src->ptr;
        if (array && (!pin_record_same(src->record, array->record) ||
                      (array->record && !pin_record_valid(array->record)))) {
            return -1;
        }
        pin_array source;
        if (array) {
            source = *array;
            source.record = dst->record;
        }
        dst->ptr = pin_array_snapshot_to_arena(a, array ? &source : NULL);
        if (src->container == PIN_CONTAINER_SET) {
            pin_array_index(a, dst->ptr);
        }
        return src->ptr && !dst->ptr ? -1 : 0;
    }
    if (value_string(src->type)) {
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
pin_array *pin_array_snapshot_like(arena *a, const pin_array *src, int count)
{
    if (!src || count < 0 ||
        (size_t)count > (SIZE_MAX - sizeof(pin_array)) / sizeof(pin_value)) {
        return NULL;
    }
    const pin_record *record = pin_record_snapshot(a, src->record);
    if (src->record && (!record || src->element_type != PIN_STRUCT || !src->type_name ||
                        strcmp(src->type_name, record->type.name))) {
        return NULL;
    }
    pin_array *dst = pin_array_snapshot_allocate(a, src->element_type, record ? NULL : src->type_name, count);
    dst->record = record;
    if (record) {
        dst->type_name = record->type.name;
    }
    return dst;
}
/* Destination metadata is already validated/owned by the enclosing snapshot
 * or private construction proof. Still validate every original foreign row. */
static int snapshot_items(arena *a, const pin_record *record, pin_value *out, const pin_value *items, int count)
{
    for (int i = 0; i < count; i++) {
        const pin_value *value = &items[i];
        if ((value->record != record && value->record &&
             !pin_record_valid(value->record)) ||
            !pin_record_same(record, value->record)) {
            return -1;
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
        (record && (!pin_record_valid(record) || !arena_contains(a, record)))) {
        return -1;
    }
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
    if (array &&
        (pin_array_snapshot_items(
             a,
             array->record,
             array->items,
             source->items,
             source->count
         ) ||
         pin_array_snapshot_items(a, array->record, &array->items[source->count], &inputs[1], 1))) {
        array = NULL;
    }
    return (pin_value){ .type = source->element_type,
                        .container = PIN_CONTAINER_ARRAY,
                        .type_name = array ? array->type_name : NULL,
                        .record = array ? array->record : NULL,
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
int pin_array_build_row(arena *a, const pin_value *prefix, const pin_value *row, int capacity, pin_value *out)
{
    const pin_array *source = prefix->ptr;
    if (capacity <= 0 || (size_t)capacity > 65536 / sizeof(pin_value) ||
        prefix->type != PIN_STRUCT || prefix->container != PIN_CONTAINER_ARRAY ||
        !source || source->element_type != PIN_STRUCT || !source->record ||
        source->count < 0 || source->count >= capacity) {
        return -1;
    }
    if (!source->count &&
        (!pin_record_valid(prefix->record) || !pin_record_valid(source->record))) {
        return -1;
    }
    if (!pin_record_same(prefix->record, source->record) || !prefix->type_name ||
        !source->type_name || strcmp(prefix->type_name, source->record->type.name) ||
        strcmp(source->type_name, source->record->type.name)) {
        return -1;
    }
    const pin_record *record = source->record;
    pin_value *items = source->items;
    if (!source->count) {
        record = pin_record_snapshot(a, record);
        if (!record) {
            return -1;
        }
        items = arena_alloc(a, (size_t)capacity * sizeof *items);
    } else if (source->capacity != capacity || !items) {
        return -1;
    }
    pin_value item;
    if (snapshot_items(a, record, &item, row, 1)) {
        return -1;
    }
    pin_array *array = arena_alloc(a, sizeof *array);
    *array = (pin_array){ .element_type = PIN_STRUCT,
                          .type_name = record->type.name,
                          .record = record,
                          .count = source->count + 1,
                          .capacity = capacity,
                          .items = items };
    items[source->count] = item;
    pin_arena_trace_array(a, array);
    *out = (pin_value){ .type = PIN_STRUCT,
                        .container = PIN_CONTAINER_ARRAY,
                        .type_name = record->type.name,
                        .record = record,
                        .ptr = array };
    return 0;
}

pin_array *pin_array_snapshot_to_arena(arena *a, const pin_array *src)
{
    if (!src || src->count < 0 || (src->count && !src->items) ||
        (size_t)src->count > (SIZE_MAX - sizeof(pin_array)) / sizeof(pin_value)) {
        return NULL;
    }
    pin_array *dst = pin_array_snapshot_like(a, src, src->count);
    if (!dst) {
        return NULL;
    }
    if (pin_array_snapshot_items(a, dst->record, dst->items, src->items, src->count)) {
        return NULL;
    }
    return dst;
}

void pin_array_free(pin_array *array)
{
    if (!array) {
        return;
    }
    for (int i = 0; i < array->count; i++) {
        pin_value_destroy(&array->items[i]);
    }
    free(array->items);
    free(array);
}

int pin_value_copy(pin_value *dst, const pin_value *src)
{
    if (!dst || !src) {
        return -1;
    }
    *dst = *src;
    pin_copy_record(PIN_COPY_VALUE_HEADER, 1);
    if (src->container >= PIN_CONTAINER_ARRAY) {
        dst->ptr = pin_array_clone((const pin_array *)src->ptr);
        return !src->ptr || dst->ptr ? 0 : -1;
    }
    if (src->type == PIN_UNION) {
        return pin_union_copy(dst, src);
    }
    if (value_string(src->type) && src->str) {
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
    *dst = *src;
    pin_copy_record(PIN_COPY_VALUE_HEADER, 1);
    if (src->type == PIN_UNION && src->container == PIN_CONTAINER_SINGLE) {
        return pin_value_snapshot_to_arena(a, dst, src);
    }
    if (src->container >= PIN_CONTAINER_ARRAY) {
        dst->ptr = pin_array_clone_to_arena(a, (const pin_array *)src->ptr);
        if (src->container == PIN_CONTAINER_SET) {
            pin_array_index(a, dst->ptr);
        }
        return !src->ptr || dst->ptr ? 0 : -1;
    }
    if (value_string(src->type) && src->str) {
        dst->str = arena_string(a, src->str, PIN_COPY_TEXT);
    }
    return 0;
}

void pin_value_destroy(pin_value *value)
{
    if (!value) {
        return;
    }
    if (value->container >= PIN_CONTAINER_ARRAY) {
        pin_array_free((pin_array *)value->ptr);
    } else if (value_string(value->type)) {
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
    if (a->container >= PIN_CONTAINER_ARRAY) {
        return pin_array_equal((const pin_array *)a->ptr, (const pin_array *)b->ptr);
    }
    if (value_string(a->type)) {
        return strcmp(a->str ? a->str : "", b->str ? b->str : "") == 0;
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
        const type_desc *type = pin_value_type(a);
        if (!type || !type->field_count) {
            return a->ptr == b->ptr;
        }
        for (int i = 0; i < type->field_count; i++) {
            pin_value left, right;
            pin_read_field(a->ptr, &type->fields[i], &left);
            pin_read_field(b->ptr, &type->fields[i], &right);
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
        if (!pin_value_equal(&a->items[i], &b->items[i])) {
            return 0;
        }
    }
    return 1;
}
