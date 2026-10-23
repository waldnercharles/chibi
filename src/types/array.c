#include "types/array.h"

#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array_internal.h"
#include "types/catalog.h"
#include "types/copy_internal.h"
#include "types/map.h"
#include "types/pointer.h"
#include "types/record.h"
#include "types/scalar.h"
#include "types/text.h"
#include "types/trace.h"
#include "types/union.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct graph_entry
{
    const void *source;
    pin_type type;
    pin_container container;
    pin_value copy;
} graph_entry;
typedef struct graph_copy
{
    arena *storage;
    graph_entry *entries;
    size_t capacity, count;
    bool historical;
    bool managed;
} graph_copy;
static _Thread_local graph_copy *copying_graph;
static int graph_value(graph_copy *, pin_value *, const pin_value *);
static int manage_owned_graph(arena *, pin_array *);
static const pin_record *snapshot_layout(arena *, pin_type, const char *, const pin_record *);

static int value_string(pin_type type)
{
    return type == PIN_STR || type == PIN_NAME;
}

static bool packed_layout(const type_desc *type, int depth)
{
    if (!type || !type->size || type->field_count < 0 ||
        (type->field_count && !type->fields) || depth > 16) {
        return false;
    }
    if (type->alias_of || type->alias) {
        const type_desc *target = type_alias_type(type, (type_resolver){ 0 });
        return target && target->size == type->size &&
               packed_layout(target, depth + 1);
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->offset > type->size || field->size > type->size - field->offset) {
            return false;
        }
        if (field->type == PIN_STRUCT) {
            const type_desc *nested = type_field_type(field, (type_resolver){ 0 });
            if (!packed_layout(nested, depth + 1) || nested->size != field->size) {
                return false;
            }
        } else if (
            !((field->type >= PIN_BOOL && field->type <= PIN_F64) ||
              field->type == PIN_HANDLE || field->type == PIN_ENUM ||
              field->type == PIN_POINTER) ||
            field->size != behavior_scalar_size(field->type)
        ) {
            return false;
        }
    }
    return true;
}
size_t pin_array_element_stride(pin_type storage, const char *name, const pin_record *record)
{
    if (name && !strncmp(name, "__collection_", 13)) {
        return 0;
    }
    if ((storage >= PIN_BOOL && storage <= PIN_F64) || storage == PIN_HANDLE ||
        storage == PIN_ENUM || storage == PIN_POINTER) {
        return record ? 0 : behavior_scalar_size(storage);
    }
    if (storage != PIN_STRUCT) {
        return 0;
    }
    pin_value shape = { .type = PIN_STRUCT, .type_name = name, .record = record };
    const type_desc *type = pin_value_type(&shape);
    return packed_layout(type, 0) ? type->size : 0;
}
const pin_array *pin_array_owner(const pin_array *array)
{
    return array && array->owner ? array->owner : array;
}
int pin_array_offset(const pin_array *array)
{
    return array && array->owner ? array->offset : 0;
}
bool pin_array_view_valid(const pin_array *array)
{
    if (!array || array->count < 0) {
        return false;
    }
    if (!array->owner) {
        return true;
    }
    const pin_array *owner = array->owner;
    return !owner->owner && array->offset >= 0 && array->offset <= owner->count &&
           array->count <= owner->count - array->offset;
}
bool pin_array_valid(const pin_array *array)
{
    if (!pin_array_view_valid(array) || array->capacity < array->count) {
        return false;
    }
    if (array->owner) {
        return !array->items && !array->data && !array->stride &&
               array->capacity == array->count &&
               array->element_type == array->owner->element_type &&
               pin_record_same(array->record, array->owner->record) &&
               pin_array_valid(array->owner);
    }
    if (array->stride) {
        return !array->items &&
               array->stride == pin_array_element_stride(
                                    array->element_type,
                                    array->type_name,
                                    array->record
                                ) &&
               (!array->count || array->data) &&
               (size_t)array->capacity <= SIZE_MAX / array->stride;
    }
    return !array->data && (!array->count || array->items) &&
           (size_t)array->capacity <= SIZE_MAX / sizeof(pin_value);
}
int pin_array_manage(arena *managed, pin_array *array)
{
    assert(managed && managed->collectable && array && !array->owner);
    assert(arena_contains(managed, array));
    assert(!array->storage || array->storage == managed);
    return manage_owned_graph(managed, array);
}
/* Retained value headers use the object's owned metadata as well as identity. */
static bool share_managed_array(pin_value *out, const pin_value *source)
{
    if ((source->container != PIN_CONTAINER_ARRAY &&
         source->container != PIN_CONTAINER_VIEW) ||
        !source->ptr) {
        return false;
    }
    const pin_array *owner = pin_array_owner(source->ptr);
    if (!owner->storage) {
        return false;
    }
    *out = *source;
    out->type_name = owner->type_name;
    out->record = owner->record;
    pin_copy_record(PIN_COPY_VALUE_HEADER, 1);
    return true;
}
size_t pin_array_storage_bytes(const pin_array *array)
{
    const pin_array *owner = pin_array_owner(array);
    return array ? (size_t)array->capacity *
                       (owner->stride ? owner->stride : sizeof(pin_value))
                 : 0;
}
void *pin_array_data(const pin_array *array, int index)
{
    assert(pin_array_view_valid(array) && index >= 0 && index < array->count);
    int at = index + pin_array_offset(array);
    const pin_array *owner = pin_array_owner(array);
    if (owner->stride) {
        return (char *)owner->data + (size_t)at * owner->stride;
    }
    pin_value *item = &owner->items[at];
    return item->type == PIN_STRUCT && item->container == PIN_CONTAINER_SINGLE
             ? item->ptr
             : &item->d;
}
pin_value pin_array_get(const pin_array *array, int index)
{
    assert(pin_array_view_valid(array) && index >= 0 && index < array->count);
    const pin_array *owner = pin_array_owner(array);
    if (!owner->stride) {
        return owner->items[index + pin_array_offset(array)];
    }
    pin_value value = { .type = owner->element_type,
                        .type_name = owner->type_name,
                        .record = owner->record };
    const void *data = pin_array_data(array, index);
    if (value.type == PIN_STRUCT) {
        value.ptr = (void *)data;
    } else {
        memcpy(&value.d, data, owner->stride);
    }
    return value;
}
static bool compatible(const pin_array *array, const pin_value *value)
{
    bool nominal = value->type == PIN_STRUCT || value->type == PIN_ENUM ||
                   value->type == PIN_HANDLE || value->type == PIN_POINTER;
    pin_value shape = { .type = array->element_type,
                        .type_name = array->type_name,
                        .record = array->record };
    return value->container == PIN_CONTAINER_SINGLE &&
           value->type == array->element_type &&
           pin_value_layout_same(value, &shape) &&
           (!nominal || !strcmp(
                            value->type_name ? value->type_name : "",
                            array->type_name ? array->type_name : ""
                        ));
}
int pin_array_set(pin_array *array, int index, const pin_value *value)
{
    if (!pin_array_view_valid(array) || !value || index < 0 || index >= array->count) {
        return -1;
    }
    if (array->owner) {
        index += array->offset;
        array = array->owner;
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
        pin_array_view_valid(array) && first >= 0 && second >= 0 &&
        first < array->count && second < array->count
    );
    if (array->owner) {
        first += array->offset;
        second += array->offset;
        array = array->owner;
    }
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
    pin_value copy;
    if (pin_value_text_share(storage, &copy, *source)) {
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
    if (!array || array->owner || count < 0) {
        return -1;
    }
    if (array->storage) {
        a = array->storage;
    }
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
    if (!array || array->owner || array->storage || capacity < 0) {
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
    if (array && (array->owner || array->storage)) {
        return -1;
    }
    pin_value shape = { .type = array ? array->element_type : PIN_FLOW,
                        .type_name = array ? array->type_name : NULL,
                        .record = array ? array->record : NULL };
    if (!array || !value || index < 0 || index > array->count ||
        value->type != array->element_type || !pin_value_layout_same(&shape, value) ||
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
    if (!pin_array_valid(src)) {
        return NULL;
    }
    const pin_array *owner = pin_array_owner(src);
    if (owner->stride) {
        pin_array *dst = xcalloc(1, sizeof *dst);
        *dst = (pin_array){ .element_type = src->element_type,
                            .type_name = src->type_name,
                            .record = src->record,
                            .count = src->count,
                            .capacity = src->count,
                            .stride = owner->stride };
        size_t bytes = (size_t)src->count * owner->stride;
        dst->data = bytes ? xmalloc(bytes) : NULL;
        if (bytes) {
            memcpy(dst->data, pin_array_data(src, 0), bytes);
        }
        return dst;
    }
    pin_array *dst = pin_array_new(src->element_type, src->type_name, src->count);
    dst->record = src->record;
    dst->count = src->count;
    for (int i = 0; i < src->count; i++) {
        pin_value item = pin_array_get(src, i);
        if (pin_value_copy(&dst->items[i], &item)) {
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
    if (!a || !pin_array_valid(src)) {
        return NULL;
    }
    pin_array *dst = pin_array_snapshot_like(a, src, src->count);
    if (!dst || pin_array_snapshot_copy(a, dst, 0, src, 0, src->count)) {
        return NULL;
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
    if (type->alias_of || type->alias) {
        const type_desc *target = type_alias_type(type, resolver);
        return target && target->size == type->size
                 ? snapshot_fields(a, target, data, depth + 1, resolver)
                 : -1;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->offset > type->size || field->size > type->size - field->offset) {
            return -1;
        }
        unsigned char *member = (unsigned char *)data + field->offset;
        if (field->type == PIN_STRUCT) {
            const type_desc *nested = type_field_type(field, resolver);
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
        } else if (field->type == PIN_POINTER) {
            if (field->size != sizeof(pin_pointer *)) {
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
    if (!type || !type->size || (dst->record && !pin_record_current(dst->record))) {
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
static const pin_record *snapshot_layout(arena *storage, pin_type type, const char *name, const pin_record *record)
{
    if (record) {
        return pin_record_snapshot(storage, record);
    }
    if (type != PIN_STRUCT || !name || !strncmp(name, "__collection_", 13)) {
        return NULL;
    }
    const type_desc *native = type_catalog_find(name);
    return native ? pin_record_from_type(storage, native) : NULL;
}
static bool layout_failed(pin_type type, const char *name, const pin_record *source, const pin_record *owned)
{
    return !owned && (source || (type == PIN_STRUCT && name &&
                                 strncmp(name, "__collection_", 13) &&
                                 type_catalog_find(name)));
}
static int snapshot_value(arena *a, pin_value *dst, const pin_value *src)
{
    if (share_managed_array(dst, src)) {
        return 0;
    }
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
    } else if (src->type == PIN_STRUCT) {
        dst->record = snapshot_layout(a, src->type, src->type_name, NULL);
        if (layout_failed(src->type, src->type_name, NULL, dst->record)) {
            return -1;
        }
        if (dst->record) {
            dst->type_name = dst->record->type.name;
        }
    }
    if (src->container == PIN_CONTAINER_MAP) {
        dst->ptr = pin_map_snapshot_to_arena(a, src->ptr);
        return src->ptr && !dst->ptr ? -1 : 0;
    }
    if (src->container >= PIN_CONTAINER_ARRAY) {
        const pin_array *array = src->ptr;
        pin_value shape = { .type = array ? array->element_type : src->type,
                            .type_name = array ? array->type_name : src->type_name,
                            .record = array ? array->record : src->record };
        if (array && !pin_value_layout_same(src, &shape)) {
            return -1;
        }
        dst->ptr = pin_array_snapshot_to_arena(a, array);
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
    if (copying_graph && copying_graph->storage == a) {
        return graph_value(copying_graph, dst, src);
    }
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
    const pin_record *record = snapshot_layout(a, type, name, NULL);
    if (layout_failed(type, name, NULL, record)) {
        return NULL;
    }
    pin_array *dst = arena_alloc(a, sizeof *dst);
    *dst = (pin_array){
        .element_type = type,
        .type_name = record ? record->type.name : arena_string(a, name, PIN_COPY_NOMINAL),
        .record = record,
        .count = count,
        .capacity = count,
        .items = count ? arena_alloc(a, (size_t)count * sizeof(pin_value)) : NULL
    };
    pin_arena_trace_array(a, dst);
    return dst;
}
pin_array *pin_array_allocate(arena *a, pin_type type, const char *name, int count)
{
    pin_array *dst = pin_array_snapshot_allocate(a, type, name, count);
    if (!dst) {
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        dst->items[i] = (pin_value){ .type = type,
                                     .type_name = dst->type_name,
                                     .record = dst->record };
    }
    return dst;
}
/* Whether a copy of `array` may hold its elements inline: a packed array's
 * elements are, and a boxed one's must each be a present value of exactly a
 * packable element type. */
static bool packable(const pin_array *array)
{
    if (pin_array_owner(array)->stride) {
        return true;
    }
    if (!pin_array_element_stride(array->element_type, array->type_name, array->record)) {
        return false;
    }
    for (int i = 0; i < array->count; i++) {
        pin_value item = pin_array_get(array, i);
        if (!compatible(array, &item) || (item.type == PIN_STRUCT && !item.ptr)) {
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
    const pin_record *record = snapshot_layout(a, src->element_type, src->type_name, src->record);
    if (layout_failed(src->element_type, src->type_name, src->record, record)) {
        return NULL;
    }
    if (src->record && (!record || src->element_type != PIN_STRUCT || !src->type_name ||
                        strcmp(src->type_name, record->type.name))) {
        return NULL;
    }
    const char *name = record ? record->type.name : src->type_name;
    size_t stride = packed ? pin_array_element_stride(src->element_type, name, record)
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
        if (!dst) {
            return NULL;
        }
        dst->record = record;
        if (record) {
            dst->type_name = record->type.name;
        }
        if (count) {
            memset(dst->items, 0, (size_t)count * sizeof *dst->items);
        }
    }
    return dst;
}
pin_array *pin_array_snapshot_like(arena *a, const pin_array *src, int count)
{
    return snapshot_like(a, src, count, src && packable(src));
}
int pin_array_snapshot_set(arena *a, pin_array *array, int index, const pin_value *value)
{
    if (!pin_array_view_valid(array) || !value || index < 0 || index >= array->count) {
        return -1;
    }
    if (array->owner) {
        index += array->offset;
        array = array->owner;
    }
    if (array->storage) {
        a = array->storage;
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
    if (!pin_array_view_valid(out) || !pin_array_view_valid(items) || to < 0 ||
        from < 0 || count < 0 || to > out->count || count > out->count - to ||
        from > items->count || count > items->count - from) {
        return -1;
    }
    const pin_array *destination = pin_array_owner(out);
    if (destination->storage) {
        a = destination->storage;
    }
    if (count && pin_array_owner(out)->stride &&
        pin_array_owner(out)->stride == pin_array_owner(items)->stride) {
        pin_value shape = pin_array_get(items, from);
        if (compatible(out, &shape)) {
            size_t bytes = (size_t)count * pin_array_owner(out)->stride;
            memmove(pin_array_data(out, to), pin_array_data(items, from), bytes);
            pin_array *owner = (pin_array *)pin_array_owner(out);
            owner->buckets = NULL;
            owner->bucket_mask = 0;
            pin_copy_record(shape.type == PIN_STRUCT ? PIN_COPY_RECORD : PIN_COPY_VALUE_BUFFER, bytes);
            return 0;
        }
    }
    /* Boxed records copy as one run, which matches each distinct record once. */
    if (count && !out->owner && !items->owner && out->record && !out->stride &&
        !items->stride) {
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
        if (!record) {
            if (pin_value_snapshot_to_arena(a, &out[i], value)) {
                return -1;
            }
            continue;
        }
        if (value->record != checked) {
            pin_value shape = { .type = PIN_STRUCT,
                                .type_name = record->type.name,
                                .record = record };
            if (!pin_value_layout_same(&shape, value)) {
                return -1;
            }
            checked = value->record;
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
    if (src && pin_array_owner(src)->storage) {
        return (pin_array *)src;
    }
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

/* Graph copies register a destination before traversing its children. This is
 * both the historical-capture path and the borrowed-array promotion path. */
static size_t graph_slot(const graph_copy *graph, const void *source, pin_type type, pin_container container)
{
    uint64_t hash = (uint64_t)(uintptr_t)source * UINT64_C(0x9e3779b97f4a7c15);
    hash ^= (uint64_t)type * UINT64_C(0xbf58476d1ce4e5b9);
    hash ^= (uint64_t)container * UINT64_C(0x94d049bb133111eb);
    return (size_t)(hash ^ (hash >> 32)) & (graph->capacity - 1);
}
static graph_entry *graph_find(const graph_copy *graph, const pin_value *source)
{
    if (!graph->capacity || !source->ptr) {
        return NULL;
    }
    size_t slot = graph_slot(graph, source->ptr, source->type, source->container);
    while (graph->entries[slot].source) {
        graph_entry *entry = &graph->entries[slot];
        if (entry->source == source->ptr && entry->type == source->type &&
            entry->container == source->container) {
            return entry;
        }
        slot = (slot + 1) & (graph->capacity - 1);
    }
    return NULL;
}
static void graph_remember(graph_copy *graph, const pin_value *source, pin_value copy)
{
    assert(source->ptr && !graph_find(graph, source));
    if (2 * (graph->count + 1) > graph->capacity) {
        graph_entry *old = graph->entries;
        size_t count = graph->capacity;
        graph->capacity = count ? count * 2 : 16;
        graph->entries = xcalloc(graph->capacity, sizeof *graph->entries);
        for (size_t i = 0; i < count; i++) {
            graph_entry entry = old[i];
            if (!entry.source) {
                continue;
            }
            size_t slot = graph_slot(graph, entry.source, entry.type, entry.container);
            while (graph->entries[slot].source) {
                slot = (slot + 1) & (graph->capacity - 1);
            }
            graph->entries[slot] = entry;
        }
        free(old);
    }
    size_t slot = graph_slot(graph, source->ptr, source->type, source->container);
    while (graph->entries[slot].source) {
        slot = (slot + 1) & (graph->capacity - 1);
    }
    graph->entries[slot] = (graph_entry){ source->ptr, source->type, source->container, copy };
    graph->count++;
}

/* Constructor snapshots already own every untagged nested array. Mark that
 * graph in place, without turning maps/sets into shared dynamic objects or
 * following managed pointer targets. Existing managed children keep identity. */
typedef struct
{
    pin_value value;
    pin_value *slot;
} manage_pending;
static int manage_layout(arena *storage, pin_array *array)
{
    if (array->storage) {
        return 0;
    }
    if (array->record || (array->element_type == PIN_STRUCT && array->type_name &&
                          strncmp(array->type_name, "__collection_", 13))) {
        const pin_record *record = snapshot_layout(
            storage,
            array->element_type,
            array->type_name,
            array->record
        );
        if (!record) {
            return -1;
        }
        array->record = record;
        array->type_name = record->type.name;
    }
    return 0;
}
static void manage_header(
    graph_copy *graph,
    const pin_value *value,
    pin_value *slot,
    const pin_record *record,
    const char *name
)
{
    graph_entry *entry = graph_find(graph, value);
    entry->copy.record = record;
    entry->copy.type_name = name;
    if (slot) {
        slot->record = record;
        slot->type_name = name;
    }
}
static int manage_owned_graph(arena *storage, pin_array *array)
{
    graph_copy visited = { 0 };
    manage_pending *pending = NULL;
    apush(
        pending,
        ((manage_pending){ .value = { .type = array->element_type,
                                      .container = PIN_CONTAINER_ARRAY,
                                      .type_name = array->type_name,
                                      .record = array->record,
                                      .ptr = array } })
    );
    int error = 0;
    while (acount(pending) && !error) {
        manage_pending at = apop(pending);
        pin_value value = at.value;
        bool aggregate = value.container != PIN_CONTAINER_SINGLE ||
                         value.type == PIN_UNION || value.type == PIN_STRUCT;
        if (!aggregate || !value.ptr) {
            continue;
        }
        graph_entry *known = graph_find(&visited, &value);
        if (known) {
            if (at.slot) {
                at.slot->record = known->copy.record;
                at.slot->type_name = known->copy.type_name;
            }
            continue;
        }
        graph_remember(&visited, &value, value);
#define MANAGE_SLOT(p) apush(pending, ((manage_pending){ *(p), (p) }))
        if (value.container == PIN_CONTAINER_MAP) {
            pin_map *map = value.ptr;
            for (int i = 0; i < map->count; i++) {
                MANAGE_SLOT(&map->entries[i].key);
                MANAGE_SLOT(&map->entries[i].value);
            }
            continue;
        }
        if (value.container != PIN_CONTAINER_SINGLE) {
            pin_array *items = value.ptr;
            if (items->owner) {
                if (manage_layout(storage, items->owner)) {
                    error = -1;
                    break;
                }
                manage_header(
                    &visited,
                    &value,
                    at.slot,
                    items->owner->record,
                    items->owner->type_name
                );
                value.container = PIN_CONTAINER_ARRAY;
                value.ptr = items->owner;
                apush(pending, ((manage_pending){ .value = value }));
                continue;
            }
            if (items->storage) {
                manage_header(&visited, &value, at.slot, items->record, items->type_name);
                continue;
            }
            if (manage_layout(storage, items)) {
                error = -1;
                break;
            }
            manage_header(&visited, &value, at.slot, items->record, items->type_name);
            if (value.container == PIN_CONTAINER_ARRAY) {
                assert(arena_contains(storage, items));
                items->storage = storage;
                pin_arena_trace_array(storage, items);
            }
            if (items->stride) {
                continue;
            }
            for (int i = 0; i < items->count; i++) {
                if (items->record && items->record->native &&
                    !items->items[i].record && items->items[i].type == PIN_STRUCT &&
                    items->items[i].container == PIN_CONTAINER_SINGLE &&
                    items->items[i].type_name &&
                    !strcmp(items->items[i].type_name, items->type_name)) {
                    items->items[i].record = items->record;
                    items->items[i].type_name = items->type_name;
                }
                MANAGE_SLOT(&items->items[i]);
            }
            continue;
        }
        if (value.type == PIN_UNION) {
            pin_union *sum = value.ptr;
            for (int i = 0; i < sum->count; i++) {
                MANAGE_SLOT(&sum->values[i]);
            }
            continue;
        }
        value.record = snapshot_layout(
            storage,
            value.type,
            value.type_name,
            value.record
        );
        if (!value.record) {
            error = -1;
            break;
        }
        value.type_name = value.record->type.name;
        manage_header(&visited, &value, at.slot, value.record, value.type_name);
        const type_desc *type = pin_value_type(&value);
        if (!type) {
            error = -1;
            break;
        }
        while (type->alias_of || type->alias) {
            type = type_alias_type(type, (type_resolver){ 0 });
            assert(type);
        }
        for (int i = 0; i < type->field_count; i++) {
            const type_field *field = &type->fields[i];
            const void *member = (const char *)value.ptr + field->offset;
            pin_value child = { .type = field->type, .type_name = field->type_name };
            if (field->type == PIN_STRUCT) {
                const type_desc *nested = type_field_type(field, (type_resolver){ 0 });
                child.ptr = (void *)member;
                child.record = nested ? nested->carrier : NULL;
            } else if (field->type == PIN_UNION) {
                memcpy(&child.ptr, member, sizeof child.ptr);
            } else {
                continue;
            }
            apush(pending, ((manage_pending){ .value = child }));
        }
#undef MANAGE_SLOT
    }
    afree(pending);
    free(visited.entries);
    return error;
}
static int graph_header(arena *storage, pin_value *out, const pin_value *source)
{
    *out = *source;
    if (source->record) {
        if (!pin_value_type(source)) {
            return -1;
        }
        out->record = pin_record_snapshot(storage, source->record);
        if (!out->record) {
            return -1;
        }
        out->type_name = out->record->type.name;
    } else {
        out->record = snapshot_layout(storage, source->type, source->type_name, NULL);
        if (layout_failed(source->type, source->type_name, NULL, out->record)) {
            return -1;
        }
        out->type_name = out->record
                           ? out->record->type.name
                           : arena_string(storage, source->type_name, PIN_COPY_NOMINAL);
    }
    pin_copy_record(PIN_COPY_VALUE_HEADER, 1);
    return 0;
}
static void carry_native_row(const pin_array *array, pin_value *value)
{
    if (array->record && array->record->native && !value->record &&
        value->type == PIN_STRUCT && value->container == PIN_CONTAINER_SINGLE &&
        value->type_name && !strcmp(value->type_name, array->type_name)) {
        value->record = array->record;
        value->type_name = array->type_name;
    }
}
static int graph_array(graph_copy *graph, pin_value *out, const pin_value *source)
{
    const pin_array *array = source->ptr;
    pin_value shape = { .type = array->element_type,
                        .type_name = array->type_name,
                        .record = array->record };
    if (!pin_value_layout_same(source, &shape)) {
        return -1;
    }
    if (array->owner) {
        /* Publish the view shell first: its owner may contain this view. */
        pin_array *view = arena_alloc(graph->storage, sizeof *view);
        *view = (pin_array){ .element_type = array->element_type,
                             .type_name = out->type_name,
                             .record = out->record,
                             .count = array->count,
                             .capacity = array->count,
                             .offset = array->offset };
        out->ptr = view;
        graph_remember(graph, source, *out);
        pin_value root = *source, retained;
        root.container = PIN_CONTAINER_ARRAY;
        root.ptr = array->owner;
        if (graph_value(graph, &retained, &root)) {
            return -1;
        }
        view->owner = retained.ptr;
        view->type_name = view->owner->type_name;
        view->record = view->owner->record;
        pin_arena_trace_array(graph->storage, view);
        return 0;
    }
    if (!pin_array_valid(array)) {
        return -1;
    }
    pin_array *copy = snapshot_like(graph->storage, array, array->count, packable(array));
    if (!copy) {
        return -1;
    }
    if (graph->managed && source->container == PIN_CONTAINER_ARRAY) {
        copy->storage = graph->storage;
    }
    out->ptr = copy;
    out->record = copy->record;
    out->type_name = copy->type_name;
    graph_remember(graph, source, *out);
    if (copy->stride && array->stride) {
        size_t bytes = (size_t)array->count * copy->stride;
        if (bytes) {
            memcpy(copy->data, array->data, bytes);
            pin_copy_record(PIN_COPY_VALUE_BUFFER, bytes);
        }
        if (source->container == PIN_CONTAINER_SET) {
            pin_array_index(graph->storage, copy);
        }
        return 0;
    }
    for (int i = 0; i < array->count; i++) {
        pin_value item = pin_array_get(array, i), retained;
        carry_native_row(copy, &item);
        if (copy->stride) {
            if (pin_array_set(copy, i, &item)) {
                return -1;
            }
            continue;
        }
        if (graph_value(graph, &retained, &item) || pin_array_set(copy, i, &retained)) {
            return -1;
        }
    }
    if (source->container == PIN_CONTAINER_SET) {
        pin_array_index(graph->storage, copy);
    }
    return 0;
}
static int graph_map(graph_copy *graph, pin_value *out, const pin_value *source)
{
    const pin_map *map = source->ptr;
    if (map->count < 0 || map->capacity < map->count || (map->count && !map->entries) ||
        (size_t)map->count > (SIZE_MAX - sizeof(pin_map)) / sizeof(pin_map_entry)) {
        return -1;
    }
    size_t bytes = sizeof(pin_map) + (size_t)map->count * sizeof(pin_map_entry);
    pin_map *copy = arena_alloc(graph->storage, bytes);
    memset(copy, 0, bytes);
    copy->entries = (pin_map_entry *)(copy + 1);
    copy->count = copy->capacity = map->count;
    out->ptr = copy;
    graph_remember(graph, source, *out);
    pin_value key_type = map->key_type, value_type = map->value_type;
    key_type.u64 = value_type.u64 = 0;
    if (graph_value(graph, &copy->key_type, &key_type) ||
        graph_value(graph, &copy->value_type, &value_type)) {
        return -1;
    }
    for (int i = 0; i < map->count; i++) {
        if (graph_value(graph, &copy->entries[i].key, &map->entries[i].key) ||
            graph_value(graph, &copy->entries[i].value, &map->entries[i].value)) {
            return -1;
        }
    }
    size_t index_bytes = pin_map_index_bytes(copy->count);
    if (index_bytes == SIZE_MAX) {
        return -1;
    }
    pin_map_index(copy, index_bytes ? arena_alloc(graph->storage, index_bytes) : NULL);
    pin_map_trace_storage(graph->storage, copy);
    return 0;
}
static int graph_value(graph_copy *graph, pin_value *out, const pin_value *source)
{
    if (!graph->historical && share_managed_array(out, source)) {
        return 0;
    }
    if (graph->managed && source->container == PIN_CONTAINER_ARRAY && !source->ptr) {
        pin_array empty = { .element_type = source->type,
                            .type_name = source->type_name,
                            .record = source->record };
        pin_array *array = pin_array_snapshot_like(graph->storage, &empty, 0);
        if (!array) {
            return -1;
        }
        array->storage = graph->storage;
        *out = *source;
        out->ptr = array;
        out->type_name = array->type_name;
        out->record = array->record;
        return 0;
    }
    bool aggregate = source->container != PIN_CONTAINER_SINGLE ||
                     source->type == PIN_STRUCT || source->type == PIN_UNION;
    if (!aggregate || !source->ptr) {
        return snapshot_value(graph->storage, out, source);
    }
    graph_entry *known = graph_find(graph, source);
    if (known) {
        *out = known->copy;
        return 0;
    }
    pin_value copy;
    if (graph_header(graph->storage, &copy, source)) {
        return -1;
    }
    int error;
    if (source->container == PIN_CONTAINER_MAP) {
        error = graph_map(graph, &copy, source);
    } else if (source->container != PIN_CONTAINER_SINGLE) {
        error = graph_array(graph, &copy, source);
    } else if (source->type == PIN_UNION) {
        const pin_union *sum = source->ptr;
        if (sum->count < 0 || (size_t)sum->count > (SIZE_MAX - sizeof(pin_union)) /
                                                       sizeof(pin_value)) {
            return -1;
        }
        size_t bytes = sizeof(pin_union) + (size_t)sum->count * sizeof(pin_value);
        pin_union *retained = arena_alloc(graph->storage, bytes);
        memset(retained, 0, bytes);
        retained->tag = sum->tag;
        retained->count = sum->count;
        copy.ptr = retained;
        graph_remember(graph, source, copy);
        error = 0;
        for (int i = 0; !error && i < sum->count; i++) {
            error = graph_value(graph, &retained->values[i], &sum->values[i]);
        }
        pin_arena_trace_union(graph->storage, retained);
    } else {
        const type_desc *type = pin_value_type(&copy);
        if (!type || !type->size) {
            return -1;
        }
        copy.ptr = arena_alloc(graph->storage, type->size);
        memcpy(copy.ptr, source->ptr, type->size);
        graph_remember(graph, source, copy);
        pin_copy_record(PIN_COPY_RECORD, type->size);
        error = pin_struct_snapshot_fields(graph->storage, type, copy.ptr);
    }
    if (error) {
        return -1;
    }
    *out = copy;
    return 0;
}
static int graph_copy_value(arena *storage, pin_value *out, const pin_value *source, bool historical)
{
    if (!storage || !out || !source) {
        return -1;
    }
    graph_copy graph = { .storage = storage,
                         .historical = historical,
                         .managed = !historical && storage->collectable };
    graph_copy *previous = copying_graph;
    copying_graph = &graph;
    pin_value retained;
    int error = graph_value(&graph, &retained, source);
    copying_graph = previous;
    free(graph.entries);
    if (!error) {
        *out = retained;
    }
    return error;
}
int pin_value_array_promote(arena *managed, pin_value *out, const pin_value *source)
{
    if (!managed || !managed->collectable) {
        return -1;
    }
    return graph_copy_value(managed, out, source, false);
}
int pin_values_array_promote(arena *managed, pin_value *out, const pin_value *source, int count)
{
    if (!managed || !managed->collectable || count < 0 ||
        (count && (!out || !source)) || (size_t)count > SIZE_MAX / sizeof(pin_value)) {
        return -1;
    }
    pin_value *retained = count ? xmalloc((size_t)count * sizeof *retained) : NULL;
    graph_copy graph = { .storage = managed, .managed = true };
    graph_copy *previous = copying_graph;
    copying_graph = &graph;
    int error = 0;
    for (int i = 0; !error && i < count; i++) {
        error = graph_value(&graph, &retained[i], &source[i]);
    }
    copying_graph = previous;
    free(graph.entries);
    if (!error && count) {
        memcpy(out, retained, (size_t)count * sizeof *out);
    }
    free(retained);
    return error;
}
int pin_value_capture_to_arena(arena *storage, pin_value *out, const pin_value *source)
{
    return graph_copy_value(storage, out, source, true);
}
pin_array *pin_array_window_copy(arena *storage, const pin_array *source, int start, int count)
{
    if (!storage || !pin_array_valid(source) || start < 0 || count < 0 ||
        start > source->count || count > source->count - start) {
        return NULL;
    }
    pin_array *copy = snapshot_like(storage, source, count, packable(source));
    if (!copy) {
        return NULL;
    }
    if (storage->collectable) {
        copy->storage = storage;
    }
    if (copy->stride && pin_array_owner(source)->stride) {
        size_t bytes = (size_t)count * copy->stride;
        if (bytes) {
            memcpy(copy->data, pin_array_data(source, start), bytes);
            pin_copy_record(PIN_COPY_VALUE_BUFFER, bytes);
        }
        return copy;
    }
    graph_copy graph = { .storage = storage, .managed = storage->collectable };
    graph_copy *previous = copying_graph;
    copying_graph = &graph;
    int error = 0;
    for (int i = 0; !error && i < count; i++) {
        pin_value item = pin_array_get(source, start + i), retained;
        carry_native_row(copy, &item);
        if (copy->stride) {
            error = pin_array_set(copy, i, &item);
            continue;
        }
        error = graph_value(&graph, &retained, &item);
        if (!error) {
            error = pin_array_set(copy, i, &retained);
        }
    }
    copying_graph = previous;
    free(graph.entries);
    return error ? NULL : copy;
}
pin_array *pin_array_outer_clone(arena *storage, const pin_array *source)
{
    return source ? pin_array_window_copy(storage, source, 0, source->count) : NULL;
}
pin_array *pin_array_window_view(arena *managed, const pin_array *source, int start, int count)
{
    if (!managed || !managed->collectable || !pin_array_valid(source) || start < 0 ||
        count < 0 || start > source->count || count > source->count - start) {
        return NULL;
    }
    pin_value input = { .type = source->element_type,
                        .type_name = source->type_name,
                        .record = source->record,
                        .container = source->owner ? PIN_CONTAINER_VIEW : PIN_CONTAINER_ARRAY,
                        .ptr = (void *)source },
              retained;
    if (pin_value_array_promote(managed, &retained, &input)) {
        return NULL;
    }
    const pin_array *array = retained.ptr;
    pin_array *owner = (pin_array *)pin_array_owner(array);
    pin_array *view = arena_alloc(managed, sizeof *view);
    *view = (pin_array){ .element_type = owner->element_type,
                         .type_name = owner->type_name,
                         .record = owner->record,
                         .count = count,
                         .capacity = count,
                         .owner = owner,
                         .offset = pin_array_offset(array) + start };
    pin_arena_trace_array(managed, view);
    return view;
}

void pin_array_free(pin_array *array)
{
    if (!array || array->owner || array->storage) {
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
    if (share_managed_array(dst, src)) {
        return 0;
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
    if (share_managed_array(dst, src)) {
        return 0;
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
        pin_text_destroy(value->text);
    } else if (value->type == PIN_NAME) {
        free((void *)value->str);
    } else if (value->type == PIN_UNION) {
        pin_union_destroy(value->ptr);
    }
    /* Struct values are borrowed until a type-specific copy callback exists.
     * Their storage may belong to an arena, graph, or host object. */
    memset(value, 0, sizeof *value);
}

static int array_equal(const pin_array *, const pin_array *);
static int value_equal(const pin_value *a, const pin_value *b)
{
    if (!a || !b || a->type != b->type || a->container != b->container) {
        return 0;
    }
    if (!pin_value_layout_same(a, b)) {
        return 0;
    }
    if (a->container == PIN_CONTAINER_MAP) {
        return pin_map_equal(a->ptr, b->ptr);
    }
    if (a->container >= PIN_CONTAINER_ARRAY) {
        return array_equal((const pin_array *)a->ptr, (const pin_array *)b->ptr);
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
        if ((a->record && !pin_record_current(a->record)) ||
            (b->record && !pin_record_current(b->record))) {
            return 0;
        }
        const type_desc *type = pin_value_type(a);
        for (int depth = 0; type && (type->alias_of || type->alias); depth++) {
            if (depth >= TYPE_MAX_NESTING) {
                return 0;
            }
            type = type_alias_type(type, (type_resolver){ 0 });
        }
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

static int array_equal(const pin_array *a, const pin_array *b)
{
    if (!a || !b || !pin_array_view_valid(a) || !pin_array_view_valid(b) ||
        a->element_type != b->element_type || a->count != b->count) {
        return 0;
    }
    pin_value left_shape = { .type = a->element_type,
                             .type_name = a->type_name,
                             .record = a->record };
    pin_value right_shape = { .type = b->element_type,
                              .type_name = b->type_name,
                              .record = b->record };
    if (!pin_value_layout_same(&left_shape, &right_shape)) {
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

/* A repeated pair represents the same comparison already in progress or
 * completed. Coinductive content equality terminates on cyclic containers;
 * distinct aliases alone do not make equal contents unequal. */
typedef struct equal_pair
{
    const void *left, *right;
    pin_type type;
    pin_container container;
} equal_pair;
typedef struct equal_graph
{
    equal_pair *pairs;
    size_t count, capacity;
} equal_graph;
static _Thread_local equal_graph *comparing_graph;
static size_t equal_slot(const equal_graph *graph, equal_pair pair)
{
    uint64_t hash = (uint64_t)(uintptr_t)pair.left * UINT64_C(0x9e3779b97f4a7c15);
    hash ^= (uint64_t)(uintptr_t)pair.right * UINT64_C(0xbf58476d1ce4e5b9);
    hash ^= (uint64_t)pair.type * UINT64_C(0x94d049bb133111eb);
    hash ^= (uint64_t)pair.container;
    return (size_t)(hash ^ (hash >> 32)) & (graph->capacity - 1);
}
static bool equal_seen(equal_graph *graph, equal_pair pair)
{
    if (2 * (graph->count + 1) > graph->capacity) {
        equal_pair *old = graph->pairs;
        size_t capacity = graph->capacity;
        graph->capacity = capacity ? capacity * 2 : 16;
        graph->pairs = xcalloc(graph->capacity, sizeof *graph->pairs);
        for (size_t i = 0; i < capacity; i++) {
            equal_pair entry = old[i];
            if (!entry.left) {
                continue;
            }
            size_t slot = equal_slot(graph, entry);
            while (graph->pairs[slot].left) {
                slot = (slot + 1) & (graph->capacity - 1);
            }
            graph->pairs[slot] = entry;
        }
        free(old);
    }
    size_t slot = equal_slot(graph, pair);
    while (graph->pairs[slot].left) {
        equal_pair known = graph->pairs[slot];
        if (known.left == pair.left && known.right == pair.right &&
            known.type == pair.type && known.container == pair.container) {
            return true;
        }
        slot = (slot + 1) & (graph->capacity - 1);
    }
    graph->pairs[slot] = pair;
    graph->count++;
    return false;
}
int pin_value_equal(const pin_value *a, const pin_value *b)
{
    bool aggregate = a && b && a->type == b->type &&
                     a->container == b->container && a->ptr && b->ptr &&
                     (a->container != PIN_CONTAINER_SINGLE ||
                      a->type == PIN_STRUCT || a->type == PIN_UNION);
    equal_graph local = { 0 };
    bool outermost = !comparing_graph;
    if (outermost) {
        comparing_graph = &local;
    }
    bool seen = aggregate &&
                equal_seen(comparing_graph, (equal_pair){ a->ptr, b->ptr, a->type, a->container });
    int result = seen || value_equal(a, b);
    if (outermost) {
        comparing_graph = NULL;
        free(local.pairs);
    }
    return result;
}
int pin_array_equal(const pin_array *a, const pin_array *b)
{
    if (!a || !b) {
        return 0;
    }
    pin_value left = { .type = a->element_type,
                       .container = PIN_CONTAINER_ARRAY,
                       .record = a->record,
                       .type_name = a->type_name,
                       .ptr = (void *)a };
    pin_value right = { .type = b->element_type,
                        .container = PIN_CONTAINER_ARRAY,
                        .record = b->record,
                        .type_name = b->type_name,
                        .ptr = (void *)b };
    return pin_value_equal(&left, &right);
}
