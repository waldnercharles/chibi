#include "execution/intrinsics.h"

#include "ecs/ecs.h"
#include "ecs/execution.h"
#include "execution/function.h"
#include "execution/map_intrinsics.h"
#include "execution/scalar.h"
#include "support/arena.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/copy_internal.h"
#include "types/map.h"
#include "types/pointer.h"
#include "types/record.h"
#include "types/schema.h"
#include "types/text.h"
#include "types/trace.h"
#include "types/union.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Managed values escape host-site scratch only through invocation-owned snapshots.
 * These primitives use the same arena/value-copy ABI as compiled native calls. */
static const char *snapshot_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 1);
    (void)count;
    if (out->container == PIN_CONTAINER_SINGLE &&
        args[0].container == PIN_CONTAINER_SINGLE) {
        if (out->type == PIN_STR && args[0].type == PIN_NAME) {
            pin_string text = pin_text(args[0]);
            pin_value value;
            const char *error = pin_value_text(storage, &value, text.bytes, text.length);
            if (error) {
                return error;
            }
            value.type_name = out->type_name;
            *out = value;
            return NULL;
        }
        if (out->type == PIN_NAME && args[0].type == PIN_STR) {
            const char *text;
            const char *error = pin_text_cstring(storage, args[0], &text, NULL);
            if (error) {
                return error;
            }
            pin_value value = *out;
            value.str = text;
            return pin_value_snapshot_to_arena(storage, out, &value)
                     ? "Cannot retain native name conversion"
                     : NULL;
        }
    }
    return pin_value_snapshot_to_arena(storage, out, args)
             ? "Cannot retain language value"
             : NULL;
}
static const char *construct_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count >= 1);
    pin_value result = args[0];
    if (result.container == PIN_CONTAINER_MAP) {
        return "Map construction requires typed key/value pairs";
    }
    if (result.record &&
        (!pin_record_current(result.record) || !pin_value_type(&result))) {
        return "Invalid authored constructor layout";
    }
    if (result.container != PIN_CONTAINER_SINGLE) {
        pin_array source = { .element_type = result.type,
                             .type_name = result.type_name,
                             .record = result.record };
        pin_array *array = pin_array_snapshot_like(storage, &source, count - 1);
        if (!array) {
            return "Cannot retain array element";
        }
        for (int i = 1; i < count; i++) {
            pin_value owned;
            if (pin_value_collection_copy(storage, &owned, &args[i])) {
                return "Cannot copy array element";
            }
            if (pin_array_snapshot_set(storage, array, i - 1, &owned)) {
                pin_value_collection_release(&owned);
                return "Cannot retain array element";
            }
        }
        result.record = array->record;
        result.type_name = array->type_name;
        result.ptr = array;
        *out = result;
        return NULL;
    }
    const type_desc *type = pin_value_type(&result);
    if (!type || result.type != PIN_STRUCT || !(count & 1)) {
        return "Unknown language constructor layout";
    }
    if (result.record && !result.record->native) {
        if (!result.ptr && (count - 1) / 2 != type->field_count) {
            return "Authored record requires every initialized field";
        }
        for (int i = 1; i < count; i += 2) {
            bool found = false;
            for (int j = 0; j < type->field_count; j++) {
                const type_field *field = &type->fields[j];
                pin_value expected = { .type = field->type,
                                       .container = field->container,
                                       .type_name = field->type_name,
                                       .record = field->layout ? field->layout->carrier
                                                               : NULL };
                found |= args[i].type == PIN_U64 &&
                         args[i].container == PIN_CONTAINER_SINGLE &&
                         args[i].u64 == field->offset &&
                         args[i + 1].type == field->type &&
                         args[i + 1].container == field->container &&
                         pin_value_layout_same(&expected, &args[i + 1]);
            }
            for (int j = 1; j < i; j += 2) {
                if (args[j].u64 == args[i].u64) {
                    return "Duplicate authored record constructor field";
                }
            }
            if (!found) {
                return "Authored record constructor field differs from its "
                       "layout";
            }
        }
    }
    if (result.ptr) {
        if (pin_value_collection_copy(storage, &result, &args[0])) {
            return "Cannot retain struct base";
        }
    } else {
        result.record = result.record ? pin_record_snapshot(storage, result.record)
                                      : pin_record_from_type(storage, type);
        if (!result.record) {
            return "Cannot retain constructor layout";
        }
        result.type_name = result.record->type.name;
        type = &result.record->type;
        result.ptr = arena_alloc(storage, type->size);
        memset(result.ptr, 0, type->size);
        pin_arena_trace_struct(storage, result.ptr, type);
    }
    type = pin_value_type(&result);
    for (int i = 1; i + 1 < count; i += 2) {
        size_t offset = (size_t)args[i].u64;
        pin_value value;
        if (pin_value_collection_copy(storage, &value, &args[i + 1])) {
            return "Cannot retain struct field";
        }
        const type_desc *field_type = pin_value_type(&value);
        size_t size = value.container != PIN_CONTAINER_SINGLE ? sizeof(void *)
                    : value.type == PIN_STRUCT ? (field_type ? field_type->size : 0)
                                               : behavior_scalar_size(value.type);
        if (!size || offset > type->size || size > type->size - offset) {
            return "Struct field exceeds declared layout";
        }
        if (value.type == PIN_STRUCT &&
            value.container == PIN_CONTAINER_SINGLE && !value.ptr) {
            return "Struct field has no payload";
        }
        if (value.type == PIN_STR && value.container == PIN_CONTAINER_SINGLE) {
            const type_field *field = NULL;
            for (int n = 0; n < type->field_count; n++) {
                if (type->fields[n].offset == offset && type->fields[n].type == PIN_STR) {
                    field = &type->fields[n];
                    break;
                }
            }
            if (!field) {
                return "Unknown string constructor field";
            }
            const char *error = pin_write_field(storage, result.ptr, field, value);
            if (error) {
                return error;
            }
            continue;
        }
        const void *data = value.type == PIN_STRUCT && value.container == PIN_CONTAINER_SINGLE
                             ? value.ptr
                             : (const void *)&value.d;
        memcpy((char *)result.ptr + offset, data, size);
    }
    type_validate_ranges(type, result.ptr);
    *out = result;
    return NULL;
}
static const char *value_equal(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    if (count != 2) {
        return "Record equality requires two operands";
    }
    if (args[0].type == PIN_POINTER || args[1].type == PIN_POINTER) {
        if (args[0].type != PIN_POINTER || args[1].type != PIN_POINTER ||
            args[0].container != PIN_CONTAINER_SINGLE ||
            args[1].container != PIN_CONTAINER_SINGLE || !args[0].type_name ||
            !args[1].type_name || strcmp(args[0].type_name, args[1].type_name)) {
            return "Pointer equality requires matching pointer types";
        }
        *out = (pin_value){ .type = PIN_BOOL,
                            .b = pin_value_equal(&args[0], &args[1]) };
        return NULL;
    }
    if (args[0].container == PIN_CONTAINER_MAP && args[1].container == PIN_CONTAINER_MAP) {
        *out = (pin_value){ .type = PIN_BOOL,
                            .b = pin_value_equal(&args[0], &args[1]) };
        return NULL;
    }
    for (int i = 0; i < 2; i++) {
        if (args[i].type != PIN_STRUCT ||
            args[i].container != PIN_CONTAINER_SINGLE || !args[i].record ||
            !pin_record_current(args[i].record) || !pin_value_type(&args[i])) {
            return "Invalid authored record equality operand";
        }
    }
    if (!pin_record_same(args[0].record, args[1].record)) {
        return "Record equality operands have different nominal layouts";
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = pin_value_equal(&args[0], &args[1]) };
    return NULL;
}
const node_host_call EXECUTION_value_equal_call = { .value = value_equal };
static bool inline_scalar(pin_type type)
{
    return type >= PIN_BOOL && type <= PIN_F64;
}
/* Whether a packed array keeps its elements inline while it changes in
 * place: plain scalars and authored records whose layout holds no
 * references are their own bytes, so an append grows the array by one
 * element's bytes and a copy of the array copies bytes. Other packed arrays
 * are boxed on their first in-place change, since their items own storage. */
static bool stays_packed(const pin_array *array)
{
    return inline_scalar(array->element_type) ||
           (array->element_type == PIN_STRUCT && array->record &&
            type_is_plain(&array->record->type));
}
/* Whether item fits a packed array as its bytes, needing no copy: a scalar of
 * the array's own type, or a record of the array's own layout, as
 * pin_array_set accepts it. The array's layout is its arena's copy of the
 * one records are built with, so records compare by layout, not address. */
static bool packed_item(const pin_array *array, const pin_value *item)
{
    if (!array->stride || item->type != array->element_type ||
        item->container != PIN_CONTAINER_SINGLE) {
        return false;
    }
    if (item->type != PIN_STRUCT) {
        return inline_scalar(item->type) && !item->record;
    }
    assert(stays_packed(array));
    pin_value shape = { .type = PIN_STRUCT,
                        .type_name = array->type_name,
                        .record = array->record };
    return item->ptr && item->schema == array->schema && item->type_name &&
           array->type_name && !strcmp(item->type_name, array->type_name) &&
           pin_value_layout_same(item, &shape);
}
/* Boxes a packed array's elements in place; false when they cannot fit. */
static bool box(arena *storage, pin_array *array)
{
    return pin_array_box_owned(storage, array) == 0;
}
/* Promote a borrowed local once; managed aliases keep the same object. */
static pin_array *owned_array(arena *storage, pin_value *header)
{
    assert(
        header && (header->container == PIN_CONTAINER_ARRAY ||
                   header->container == PIN_CONTAINER_VIEW)
    );
    assert(storage && storage->collectable);
    pin_array *array = header->ptr;
    /* A managed array is already its own retained value: promotion would
     * only read its element names back into the header. */
    if (array && !array->pointer && array->storage) {
        header->type_name = array->type_name;
        header->record = array->record;
    } else {
        pin_value retained;
        if (pin_value_array_promote(storage, &retained, header)) {
            return NULL;
        }
        *header = retained;
        array = header->ptr;
    }
    if (!array || !pin_array_view_valid(array)) {
        return NULL;
    }
    if (array->pointer) {
        return array;
    }
    if (array->stride && !stays_packed(array) && !box(array->storage, array)) {
        return NULL;
    }
    array->buckets = NULL;
    array->bucket_mask = 0;
    return array;
}
static bool same_storage(pin_value a, pin_value b);
static bool array_item_matches(const pin_array *array, pin_value item)
{
    const pin_array *root = pin_array_owner(array);
    if (!root) {
        return false;
    }
    /* Collection IDs erase the logical element container and optional schema.
     * Their typed reads/reference writes validate independent expected headers.
     * A physical UNION alone likewise does not prove a logical union element. */
    if (root->element_type == PIN_UNION ||
        (root->type_name && !strncmp(root->type_name, "__collection_", 13))) {
        return true;
    }
    pin_value expected = { .type = root->element_type,
                           .type_name = root->type_name,
                           .record = root->record };
    return same_storage(expected, item);
}
static void array_drop_range(pin_array *, int first, int end);
/* Appends item to an owned array: inline when it is packed and the item fits,
 * otherwise as a boxed copy. Its capacity must already hold it. */
static const char *append_item(arena *storage, pin_array *array, const pin_value *item)
{
    assert(!array->pointer && array->count < array->capacity);
    if (packed_item(array, item)) {
        memcpy(
            (char *)array->data + (size_t)array->count * array->stride,
            item->type == PIN_STRUCT ? item->ptr : &item->d,
            array->stride
        );
        array->count++;
        array->buckets = NULL;
        array->bucket_mask = 0;
        pin_array_mutation_end(array);
        return NULL;
    }
    if (!array_item_matches(array, *item)) {
        return "Array item differs from its element type";
    }
    pin_value retained;
    if (pin_value_collection_copy(storage, &retained, item)) {
        return "Cannot retain array item";
    }
    if (array->stride) {
        array->count++;
        if (!pin_array_set(array, array->count - 1, &retained)) {
            return NULL;
        }
        array->count--;
        if (!box(storage, array)) {
            pin_value_collection_release(&retained);
            return "Array exceeds its length limit";
        }
    }
    array->items[array->count] = (pin_value){ 0 };
    array->count++;
    if (pin_array_set(array, array->count - 1, &retained)) {
        array->count--;
        pin_value_collection_release(&retained);
        return "Array item differs from its element type";
    }
    pin_array_mutation_end(array);
    return NULL;
}
static const char *array_push(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 2);
    (void)count;
    pin_array *array = owned_array(storage, (pin_value *)(uintptr_t)args[0].u64);
    if (!array || array->pointer || array->count == INT_MAX ||
        pin_array_reserve_owned(storage, array, array->count + 1)) {
        return "Array exceeds its length limit";
    }
    const char *error = append_item(array->storage, array, &args[1]);
    *out = (pin_value){ .type = PIN_BOOL, .b = true };
    return error;
}
static const char *array_store(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 3);
    (void)count;
    pin_value *header = (pin_value *)(uintptr_t)args[0].u64;
    const pin_array *current = header->ptr;
    int index = args[1].i;
    if (!current || !pin_array_view_valid(current) || index < 0 ||
        index >= current->count) {
        return "Array index out of range";
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = true };
    pin_array *array = owned_array(storage, header);
    if (!array) {
        return "Array exceeds its length limit";
    }
    if (packed_item(array, &args[2])) {
        return pin_array_set(array, index, &args[2])
                 ? "Cannot retain array item"
                 : NULL;
    }
    if (!array_item_matches(array, args[2])) {
        return "Array item differs from its element type";
    }
    return pin_array_snapshot_set(storage, array, index, &args[2])
             ? "Cannot retain array item"
             : NULL;
}
static const char *array_extend(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 2);
    (void)count;
    pin_array *array = owned_array(storage, (pin_value *)(uintptr_t)args[0].u64);
    const pin_array *items = args[1].ptr;
    int added = items ? items->count : 0;
    if (!array || array->pointer || (items && !pin_array_view_valid(items)) ||
        added > INT_MAX - array->count ||
        pin_array_reserve_owned(storage, array, array->count + added)) {
        return "Array exceeds its length limit";
    }
    for (int i = 0; i < added; i++) {
        if (!array_item_matches(array, pin_array_get(items, i))) {
            return "Array extension differs from its element type";
        }
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = true };
    /* Packed items of the array's own layout, which every item matched, copy
     * as bytes; `xs.extend(xs)` reads the items it had before. */
    if (added && array->stride && items->stride == array->stride &&
        items->element_type == array->element_type) {
        memmove(
            (char *)array->data + (size_t)array->count * array->stride,
            items->data,
            (size_t)added * array->stride
        );
        array->count += added;
        pin_array_mutation_end(array);
        return NULL;
    }
    int before = array->count;
    for (int i = 0; i < added; i++) {
        pin_value item = pin_array_get(items, i);
        const char *error = append_item(array->storage, array, &item);
        if (error) {
            array_drop_range(array, before, array->count);
            array->count = before;
            pin_array_mutation_end(array);
            return error;
        }
    }
    return NULL;
}
const node_host_call EXECUTION_array_push_call = { .value = array_push };
const node_host_call EXECUTION_array_extend_call = { .value = array_extend };
const node_host_call EXECUTION_array_store_call = { .value = array_store };
static bool sequence_array(pin_value value)
{
    return value.container == PIN_CONTAINER_ARRAY ||
           value.container == PIN_CONTAINER_VIEW;
}
static pin_value array_header(const pin_array *array, pin_container container)
{
    return (pin_value){ .type = array->element_type,
                        .type_name = array->type_name,
                        .record = array->record,
                        .container = container,
                        .ptr = (void *)array };
}
static const char *sequence_range(arena *storage, const pin_value *args, int count, pin_value *out, bool view)
{
    assert(count == 4);
    (void)count;
    pin_value source = args[0], value;
    int start = args[1].i, end = args[2].i;
    *out = (pin_value){ .type = PIN_UNION };
    if (source.type == PIN_STR && source.container == PIN_CONTAINER_SINGLE) {
        pin_string text = pin_text(source);
        if (start < 0 || end < start || (size_t)end > text.length) {
            return NULL;
        }
        const char *
            error = view
                      ? pin_value_text_slice(storage, &value, source, (size_t)start, (size_t)(end - start))
                      : pin_value_text(storage, &value, text.bytes + start, (size_t)(end - start));
        if (error) {
            return error;
        }
    } else if (view && source.type == PIN_POINTER && source.container == PIN_CONTAINER_SINGLE) {
        pin_value current;
        if (!source.ptr || pin_pointer_read_value(source.ptr, &current) ||
            current.container != PIN_CONTAINER_ARRAY) {
            return "Array view requires a pointer to an array";
        }
        const pin_array *array = current.ptr;
        int length = array ? array->count : 0;
        if (start < 0 || end < start || end > length) {
            return NULL;
        }
        pin_array *result = pin_array_pointer_view(storage, source.ptr, start, end - start);
        if (!result) {
            return "Cannot retain array pointer view";
        }
        value = array_header(result, PIN_CONTAINER_VIEW);
    } else if (sequence_array(source)) {
        if (view) {
            return "Array view requires a pointer to an array";
        }
        const pin_array *array = source.ptr;
        int length = array ? array->count : 0;
        if ((array && !pin_array_view_valid(array)) || start < 0 ||
            end < start || end > length) {
            return NULL;
        }
        pin_array empty = { .element_type = source.type,
                            .type_name = source.type_name,
                            .record = source.record };
        pin_array *result = pin_array_window_copy(storage, array ? array : &empty, start, end - start);
        if (!result) {
            return "Cannot retain sequence range";
        }
        value = array_header(result, view ? PIN_CONTAINER_VIEW : PIN_CONTAINER_ARRAY);
    } else {
        return "Range requires a string or array";
    }
    return pin_union_value(storage, args[3].u64, &value, 1, out)
             ? "Cannot retain sequence range result"
             : NULL;
}
static const char *sequence_slice(arena *storage, const pin_value *args, int count, pin_value *out)
{
    return sequence_range(storage, args, count, out, false);
}
static const char *sequence_view(arena *storage, const pin_value *args, int count, pin_value *out)
{
    return sequence_range(storage, args, count, out, true);
}
static const char *sequence_clone(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 1);
    (void)count;
    if (args[0].type == PIN_STR && args[0].container == PIN_CONTAINER_SINGLE) {
        return pin_value_text_clone(storage, out, args[0]);
    }
    if (!sequence_array(args[0])) {
        return "Clone requires a string or array";
    }
    const pin_array *source = args[0].ptr;
    if (source && !pin_array_view_valid(source)) {
        return "Array view is out of range";
    }
    pin_array empty = { .element_type = args[0].type,
                        .type_name = args[0].type_name,
                        .record = args[0].record };
    pin_array *copy = pin_array_outer_clone(storage, source ? source : &empty);
    if (!copy) {
        return "Cannot clone array";
    }
    *out = array_header(copy, PIN_CONTAINER_ARRAY);
    return NULL;
}
/* An element as its own value, or, borrowed, the element in place: valid
 * only while the array it reads stays unchanged. */
static const char *sequence_element(arena *storage, const pin_value *args, int count, pin_value *out, bool borrowed)
{
    assert(count == 2);
    (void)count;
    int index = args[1].i;
    if (args[0].type == PIN_STR && args[0].container == PIN_CONTAINER_SINGLE) {
        pin_string text = pin_text(args[0]);
        if (index < 0 || (size_t)index >= text.length) {
            return "String index out of range";
        }
        *out = (pin_value){ .type = PIN_I32,
                            .type_name = "int",
                            .i = (unsigned char)text.bytes[index] };
        return NULL;
    }
    if (!sequence_array(args[0])) {
        return "Index requires a string or array";
    }
    const pin_array *array = args[0].ptr;
    if (!array || !pin_array_valid(array) || index < 0 || index >= array->count) {
        return "array index out of range";
    }
    pin_value item = pin_array_get(array, index);
    if (!same_storage(*out, item)) {
        return "Array element does not match result type";
    }
    if (borrowed) {
        *out = item;
        return NULL;
    }
    return pin_value_snapshot_to_arena(storage, out, &item)
             ? "Cannot retain array element"
             : NULL;
}
static const char *sequence_get(arena *storage, const pin_value *args, int count, pin_value *out)
{
    return sequence_element(storage, args, count, out, false);
}
static const char *sequence_borrow(arena *storage, const pin_value *args, int count, pin_value *out)
{
    return sequence_element(storage, args, count, out, true);
}
static const char *sequence_length(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 1);
    (void)count;
    int length;
    if (args[0].type == PIN_STR && args[0].container == PIN_CONTAINER_SINGLE) {
        pin_string text = pin_text(args[0]);
        if (text.length > INT_MAX) {
            return "Text byte length exceeds int range";
        }
        length = (int)text.length;
    } else if (sequence_array(args[0])) {
        const pin_array *array = args[0].ptr;
        if (array && !pin_array_view_valid(array)) {
            return "Array view is out of range";
        }
        length = array ? array->count : 0;
    } else {
        return "Length requires a string or array";
    }
    *out = (pin_value){ .type = PIN_I32, .type_name = "int", .i = length };
    return NULL;
}
const node_host_call EXECUTION_sequence_get_call = { .value = sequence_get };
const node_host_call EXECUTION_sequence_borrow_call = { .value = sequence_borrow };
const node_host_call EXECUTION_sequence_length_call = { .value = sequence_length };
static void array_drop_range(pin_array *array, int first, int end)
{
    for (int i = first; i < end; i++) {
        pin_value value = pin_array_get(array, i);
        pin_value_collection_release(&value);
    }
    if (array->stride) {
        memset(
            (char *)array->data + (size_t)first * array->stride,
            0,
            (size_t)(end - first) * array->stride
        );
    } else {
        memset(array->items + first, 0, (size_t)(end - first) * sizeof *array->items);
    }
}
static const char *array_mutate_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    if (count < 2 || !sequence_array(args[0]) || !args[0].ptr) {
        return "Array mutation requires a managed array";
    }
    pin_array *array = args[0].ptr;
    pin_array *root = (pin_array *)pin_array_owner(array);
    if (!root->storage || !pin_array_view_valid(array)) {
        return "Array mutation requires a live managed array";
    }
    execution_array_mutation operation = (execution_array_mutation)args[1].i;
    if (array->pointer && operation != EXECUTION_ARRAY_SET &&
        operation != EXECUTION_ARRAY_REVERSE && operation != EXECUTION_ARRAY_SWAP) {
        return "Array views cannot change length";
    }
    int expected = 0;
    switch (operation) {
        case EXECUTION_ARRAY_APPEND:
        case EXECUTION_ARRAY_ADD_UNIQUE:
        case EXECUTION_ARRAY_EXTEND:
        case EXECUTION_ARRAY_REMOVE_INDEX:
        case EXECUTION_ARRAY_REMOVE_ITEM:
        case EXECUTION_ARRAY_RESIZE: expected = 3; break;
        case EXECUTION_ARRAY_SET:
        case EXECUTION_ARRAY_INSERT:
        case EXECUTION_ARRAY_SWAP: expected = 4; break;
        case EXECUTION_ARRAY_CLEAR:
        case EXECUTION_ARRAY_REVERSE: expected = 2; break;
        default: return "Unknown array mutation";
    }
    if (count != expected) {
        return "Invalid array mutation arguments";
    }
    if ((operation == EXECUTION_ARRAY_APPEND || operation == EXECUTION_ARRAY_ADD_UNIQUE) &&
        !array_item_matches(array, args[2])) {
        return "Array item differs from its element type";
    }
    if ((operation == EXECUTION_ARRAY_SET || operation == EXECUTION_ARRAY_INSERT) &&
        !array_item_matches(array, args[3])) {
        return "Array item differs from its element type";
    }
    if (operation == EXECUTION_ARRAY_EXTEND) {
        if (!sequence_array(args[2])) {
            return "Array extension requires an array";
        }
        const pin_array *items = args[2].ptr;
        if (items && !pin_array_view_valid(items)) {
            return "Array extension view is out of range";
        }
    }
    arena *owner = root->storage;
    pin_value inputs[3] = { args[0] };
    for (int i = 2; i < count; i++) {
        inputs[i - 1] = args[i];
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = true };
    if (operation == EXECUTION_ARRAY_APPEND || operation == EXECUTION_ARRAY_ADD_UNIQUE) {
        if (operation == EXECUTION_ARRAY_ADD_UNIQUE &&
            pin_array_find(array, &inputs[1]) >= 0) {
            return NULL;
        }
        if (array->count == INT_MAX ||
            pin_array_reserve_owned(owner, array, array->count + 1)) {
            return "Array exceeds its length limit";
        }
        return append_item(owner, array, &inputs[1]);
    }
    if (operation == EXECUTION_ARRAY_EXTEND) {
        pin_value header = inputs[0];
        pin_value extension[] = { { .u64 = (uintptr_t)&header }, inputs[1] };
        return array_extend(owner, extension, 2, out);
    }
    if (operation == EXECUTION_ARRAY_SET) {
        int index = inputs[1].i;
        if (index < 0 || index >= array->count) {
            return NULL;
        }
        return pin_array_snapshot_set(owner, array, index, &inputs[2])
                 ? "Cannot retain array item"
                 : NULL;
    }
    if (operation == EXECUTION_ARRAY_CLEAR) {
        array_drop_range(array, 0, array->count);
        array->count = 0;
        array->buckets = NULL;
        array->bucket_mask = 0;
        return NULL;
    }
    if (operation == EXECUTION_ARRAY_REVERSE) {
        for (int i = 0; i < array->count / 2; i++) {
            pin_array_swap(array, i, array->count - i - 1);
        }
        return NULL;
    }
    if (operation == EXECUTION_ARRAY_SWAP) {
        int first = inputs[1].i, second = inputs[2].i;
        if (first >= 0 && second >= 0 && first < array->count && second < array->count) {
            pin_array_swap(array, first, second);
        }
        return NULL;
    }
    if (operation == EXECUTION_ARRAY_INSERT) {
        if (array->count == INT_MAX) {
            return "Array exceeds its length limit";
        }
        int index = inputs[1].i < 0            ? 0
                  : inputs[1].i > array->count ? array->count
                                               : inputs[1].i;
        /* Retain before shifting: the supplied item may be one of these rows. */
        pin_value inserted;
        if (pin_value_collection_copy(owner, &inserted, &inputs[2])) {
            return "Cannot copy inserted array item";
        }
        pin_array *held = pin_array_snapshot_like(owner, array, 1);
        if (!held || pin_array_snapshot_set(owner, held, 0, &inserted)) {
            pin_value_collection_release(&inserted);
            return "Cannot retain inserted array item";
        }
        pin_value item = pin_array_get(held, 0);
        if (pin_array_reserve_owned(owner, array, array->count + 1)) {
            pin_value_collection_release(&inserted);
            return "Array exceeds its length limit";
        }
        if (array->stride) {
            memmove(
                (char *)array->data + (size_t)(index + 1) * array->stride,
                (char *)array->data + (size_t)index * array->stride,
                (size_t)(array->count - index) * array->stride
            );
        } else {
            memmove(
                array->items + index + 1,
                array->items + index,
                (size_t)(array->count - index) * sizeof *array->items
            );
        }
        if (array->stride) {
            memset((char *)array->data + (size_t)index * array->stride, 0, array->stride);
        } else {
            array->items[index] = (pin_value){ 0 };
        }
        array->count++;
        int error = pin_array_set(array, index, &item);
        assert(!error);
        (void)error;
    } else if (operation == EXECUTION_ARRAY_REMOVE_INDEX || operation == EXECUTION_ARRAY_REMOVE_ITEM) {
        int index = operation == EXECUTION_ARRAY_REMOVE_INDEX
                      ? inputs[1].i
                      : pin_array_find(array, &inputs[1]);
        if (index < 0 || index >= array->count) {
            return NULL;
        }
        pin_value removed = pin_array_get(array, index);
        pin_value_collection_release(&removed);
        if (array->stride) {
            memmove(
                (char *)array->data + (size_t)index * array->stride,
                (char *)array->data + (size_t)(index + 1) * array->stride,
                (size_t)(array->count - index - 1) * array->stride
            );
        } else {
            memmove(
                array->items + index,
                array->items + index + 1,
                (size_t)(array->count - index - 1) * sizeof *array->items
            );
        }
        array->count--;
        if (array->stride) {
            memset(
                (char *)array->data + (size_t)array->count * array->stride,
                0,
                array->stride
            );
        } else {
            array->items[array->count] = (pin_value){ 0 };
        }
    } else if (operation == EXECUTION_ARRAY_RESIZE) {
        int size = inputs[1].i < 0 ? 0 : inputs[1].i;
        if (size < array->count) {
            array_drop_range(array, size, array->count);
        }
        if (size > array->count) {
            if (array->record && !array->record->native) {
                return "Cannot grow an authored record array without "
                       "initialized rows";
            }
            if (pin_array_reserve_owned(owner, array, size)) {
                return "Array exceeds its length limit";
            }
            if (array->stride) {
                memset(
                    (char *)array->data + (size_t)array->count * array->stride,
                    0,
                    (size_t)(size - array->count) * array->stride
                );
            } else {
                for (int i = array->count; i < size; i++) {
                    array->items[i] = (pin_value){ .type = array->element_type,
                                                   .type_name = array->type_name,
                                                   .record = array->record };
                }
            }
        }
        array->count = size;
    } else {
        return "Unknown array mutation";
    }
    array->buckets = NULL;
    array->bucket_mask = 0;
    *out = (pin_value){ .type = PIN_BOOL, .b = true };
    return NULL;
}
static const char *array_mutate(arena *storage, const pin_value *args, int count, pin_value *out)
{
    if (count < 2 || !sequence_array(args[0]) || !args[0].ptr) {
        return "Array mutation requires an array value";
    }
    pin_array *array = args[0].ptr;
    pin_array *root = (pin_array *)pin_array_owner(array);
    if (!root || !root->storage || pin_array_make_unique(root->storage, root)) {
        return "Cannot detach array value";
    }
    const char *error = array_mutate_value(storage, args, count, out);
    pin_array_mutation_end(root);
    return error;
}
const node_host_call EXECUTION_sequence_slice_call = { .value = sequence_slice };
const node_host_call EXECUTION_sequence_view_call = { .value = sequence_view };
const node_host_call EXECUTION_sequence_clone_call = { .value = sequence_clone };
/* Reads mutable sequence storage and creates distinct array objects. No pure
 * reuse or unproven parallel access is valid for this shared dispatch. */
const node_access EXECUTION_sequence_read_access = { .flags = NODE_EFFECT_WORLD_READ };
const node_access EXECUTION_assignment_access = { .flags = NODE_EFFECT_HOST_SINK };
const node_host_call EXECUTION_snapshot_call = { .value = snapshot_value };
const node_host_call EXECUTION_construct_call = { .value = construct_value };
const node_access EXECUTION_value_access = { .flags = NODE_EFFECT_PURE };
static int union_value_copy(arena *storage, uint64_t tag, const pin_value *values, int count, pin_value *out)
{
    pin_value *copies = count ? xmalloc((size_t)count * sizeof *copies) : NULL;
    int copied = 0, error = 0;
    for (; copied < count; copied++) {
        if (pin_value_collection_copy(storage, &copies[copied], &values[copied])) {
            error = -1;
            break;
        }
    }
    if (!error) {
        error = pin_union_value(storage, tag, copies, count, out);
    }
    if (error) {
        for (int i = 0; i < copied; i++) {
            pin_value_collection_release(&copies[i]);
        }
    }
    free(copies);
    return error;
}
static const char *union_make(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count >= 1);
    if (!args[0].u64) {
        return "Union constructor requires a nonzero tag";
    }
    if (args[0].u64 == PIN_UNION_CANCELLED_TAG && count == 1) {
        pin_union_cancelled(out);
        return NULL;
    }
    return union_value_copy(storage, args[0].u64, args + 1, count - 1, out)
             ? "Cannot retain union payload"
             : NULL;
}
static const char *union_keep(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count >= 2);
    if (!args[0].u64) {
        return "Union constructor requires a nonzero tag";
    }
    return union_value_copy(storage, args[0].u64, args + 2, count - 2, out)
             ? "Cannot retain union payload"
             : NULL;
}
static const char *union_test(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count >= 2);
    const pin_union *value = args[0].ptr;
    bool match = false;
    for (int i = 1; value && i < count; i++) {
        match |= value->tag == args[i].u64;
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = match };
    return NULL;
}
/* A value's nominal type name; "" for a plain storage. A native port spells a
 * plain storage by its C name, such as "const char *" for a string, and a
 * collection copy carries that spelling from the array it shares. */
static const char *nominal_name(const pin_value *value)
{
    const char *plain = pin_type_name(value->type);
    if (!value->type_name || (plain && !strcmp(value->type_name, plain))) {
        return "";
    }
    return value->type_name;
}
static const char *union_get(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count >= 4);
    const pin_union *value = args[0].ptr;
    bool match = value && (!args[1].u64 || value->tag == args[1].u64);
    for (int i = 4; value && i < count; i++) {
        match |= value->tag == args[i].u64;
    }
    if (!match) {
        out->u64 = 0;
        return NULL;
    }
    if (args[3].b) {
        if (out->type != PIN_UNION || out->container != PIN_CONTAINER_SINGLE) {
            return "Union payload differs from the extraction type";
        }
        *out = args[0];
        return NULL;
    }
    int field = args[2].i;
    if (field < 0 || field >= value->count) {
        return "Union payload index exceeds its case layout";
    }
    const pin_value *payload = &value->values[field];
    if (payload->type != out->type || payload->container != out->container ||
        strcmp(nominal_name(payload), nominal_name(out))) {
        return "Union payload differs from the extraction type";
    }
    *out = *payload;
    return NULL;
}
const node_host_call EXECUTION_union_make_call = { .value = union_make };
const node_host_call EXECUTION_union_keep_call = { .value = union_keep };
const node_host_call EXECUTION_union_test_call = { .value = union_test };
const node_host_call EXECUTION_union_get_call = { .value = union_get };
static const char *value_copy(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 1);
    (void)count;
    return pin_value_collection_copy(storage, out, args)
             ? "Cannot copy collection value"
             : NULL;
}
static const char *value_drop(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 1);
    (void)count;
    (void)args; /* The dispatcher releases only the current context owner. */
    *out = (pin_value){ .type = PIN_BOOL, .b = true };
    return NULL;
}
const node_host_call EXECUTION_value_copy_call = { .value = value_copy };
const node_host_call EXECUTION_value_drop_call = { .value = value_drop };
typedef struct language_reference_path
{
    const struct language_reference_path *parent;
    pin_value type;
    size_t offset;
    int index;
    bool element;
    bool map;
    bool union_payload;
    uint64_t tag;
    pin_value key;
} language_reference_path;
typedef struct
{
    language_reference_kind kind;
    behavior_function_context *owner;
    pin_value *value;
    pin_value root, type;
    uint64_t entity, component, layout;
    size_t stride;
    const language_reference_path *path;
    int variable;
    execution_box *box;
    pin_pointer *managed;
} language_reference;
static void reference_trace(const void *value, const void *shape, arena_marker mark, void *user)
{
    (void)shape;
    const language_reference *reference = value;
    pin_value_trace(&reference->root, mark, user);
    pin_value_trace(&reference->type, mark, user);
    mark(user, reference->path);
    mark(user, reference->managed);
    if (reference->kind == LANGUAGE_REFERENCE_STATE) {
        mark(user, reference->owner);
    }
    if (reference->kind == LANGUAGE_REFERENCE_VALUE && !reference->box) {
        mark(user, reference->value);
        pin_value_trace(reference->value, mark, user);
    }
}
static void reference_path_trace(const void *value, const void *shape, arena_marker mark, void *user)
{
    (void)shape;
    const language_reference_path *path = value;
    mark(user, path->parent);
    pin_value_trace(&path->type, mark, user);
    pin_value_trace(&path->key, mark, user);
}
static behavior_function_context *value_context(pin_value value)
{
    return (behavior_function_context *)(uintptr_t)behavior_scalar_bits(value);
}
static const char *make_reference(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 9);
    (void)count;
    if (args[2].i < LANGUAGE_REFERENCE_VALUE || args[2].i > LANGUAGE_REFERENCE_COMPONENT) {
        return "Invalid reference storage kind";
    }
    language_reference *ref = arena_alloc(storage, sizeof *ref);
    *ref = (language_reference){ .owner = value_context(args[0]),
                                 .type = args[1],
                                 .root = args[1],
                                 .kind = args[2].i,
                                 .variable = args[3].i,
                                 .value = (pin_value *)(uintptr_t)args[4].u64,
                                 .entity = args[5].u64,
                                 .component = args[6].u64,
                                 .layout = args[7].u64,
                                 .stride = args[8].u64 };
    arena_set_trace(storage, ref, reference_trace, NULL);
    *out = (pin_value){ .type = BF_POINTER_TYPE, .u64 = (uintptr_t)ref };
    return NULL;
}
pin_value execution_reference_value(
    arena *storage,
    behavior_function_context *owner,
    pin_value *value,
    pin_value type
)
{
    language_reference *ref = arena_alloc(storage, sizeof *ref);
    *ref = (language_reference){ .kind = LANGUAGE_REFERENCE_VALUE,
                                 .owner = owner,
                                 .value = value,
                                 .root = type,
                                 .type = type,
                                 .variable = -1 };
    arena_set_trace(storage, ref, reference_trace, NULL);
    return (pin_value){ .type = BF_POINTER_TYPE, .u64 = (uintptr_t)ref };
}
static const language_reference_path *capture_path(arena *storage, const language_reference_path *path)
{
    if (!path) {
        return NULL;
    }
    language_reference_path *copy = arena_alloc(storage, sizeof *copy);
    *copy = *path;
    if (path->map && pin_value_snapshot_to_arena(storage, &copy->key, &path->key)) {
        return NULL;
    }
    copy->parent = capture_path(storage, path->parent);
    arena_set_trace(storage, copy, reference_path_trace, NULL);
    return copy;
}
bool execution_reference_local(
    pin_value value,
    behavior_function_context **owner,
    pin_value **slot,
    execution_box **box
)
{
    const language_reference *ref = (const language_reference *)(uintptr_t)value.u64;
    *owner = ref->owner;
    *slot = ref->value;
    *box = ref->box;
    return ref->kind == LANGUAGE_REFERENCE_VALUE;
}
void execution_reference_capture(
    arena *storage,
    behavior_function_context *owner,
    pin_value *value,
    execution_box *box
)
{
    const language_reference *base = (const language_reference *)(uintptr_t)
                                         value->u64;
    language_reference *ref = arena_alloc(storage, sizeof *ref);
    *ref = *base;
    ref->owner = owner;
    /* State stays with the instance that owns it, which a library task's
     * context does not; keep a copy of the originating context. */
    if (base->kind == LANGUAGE_REFERENCE_STATE) {
        ref->owner = arena_alloc(storage, sizeof *ref->owner);
        *ref->owner = *base->owner;
    }
    ref->path = capture_path(storage, base->path);
    if (box) {
        ref->box = box;
        ref->value = NULL;
    }
    arena_set_trace(storage, ref, reference_trace, NULL);
    value->u64 = (uintptr_t)ref;
}
/* A boxed reference reaches the box's current frame and slot. */
static language_reference *unbox(language_reference *ref, language_reference *view)
{
    if (!ref->box) {
        return ref;
    }
    *view = *ref;
    view->owner = ref->box->owner;
    view->value = ref->box->value;
    return view;
}
static const char *field_reference(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 4);
    (void)count;
    language_reference *base = (language_reference *)(uintptr_t)args[0].u64;
    language_reference *ref = arena_alloc(storage, sizeof *ref);
    *ref = *base;
    ref->type = args[1];
    language_reference_path *path = arena_alloc(storage, sizeof *path);
    *path = (language_reference_path){ .parent = base->path,
                                       .type = args[1],
                                       .offset = args[2].u64,
                                       .index = args[2].i,
                                       .element = args[3].b };
    ref->path = path;
    arena_set_trace(storage, path, reference_path_trace, NULL);
    arena_set_trace(storage, ref, reference_trace, NULL);
    *out = (pin_value){ .type = BF_POINTER_TYPE, .u64 = (uintptr_t)ref };
    return NULL;
}
static const char *key_reference(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 3);
    (void)count;
    if (!pin_map_key_supported(args[2])) {
        return "Invalid or nonfinite Map reference key";
    }
    language_reference *base = (language_reference *)(uintptr_t)args[0].u64;
    language_reference *ref = arena_alloc(storage, sizeof *ref);
    language_reference_path *path = arena_alloc(storage, sizeof *path);
    *ref = *base;
    *path = (language_reference_path){ .parent = base->path, .type = args[1], .map = true };
    if (pin_value_snapshot_to_arena(storage, &path->key, &args[2])) {
        return "Cannot retain Map reference key";
    }
    ref->path = path;
    ref->type = args[1];
    arena_set_trace(storage, path, reference_path_trace, NULL);
    arena_set_trace(storage, ref, reference_trace, NULL);
    *out = (pin_value){ .type = BF_POINTER_TYPE, .u64 = (uintptr_t)ref };
    return NULL;
}
const node_host_call EXECUTION_reference_key_call = { .value = key_reference };

static const char *union_reference(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 4);
    (void)count;
    language_reference *base = (language_reference *)(uintptr_t)args[0].u64;
    language_reference *ref = arena_alloc(storage, sizeof *ref);
    *ref = *base;
    ref->type = args[3];
    language_reference_path *path = arena_alloc(storage, sizeof *path);
    *path = (language_reference_path){ .parent = base->path,
                                       .type = args[3],
                                       .index = args[2].i,
                                       .tag = args[1].u64,
                                       .union_payload = true };
    ref->path = path;
    arena_set_trace(storage, path, reference_path_trace, NULL);
    arena_set_trace(storage, ref, reference_trace, NULL);
    *out = (pin_value){ .type = BF_POINTER_TYPE, .u64 = (uintptr_t)ref };
    return NULL;
}
const node_host_call EXECUTION_reference_union_call = { .value = union_reference };
static pin_value reference_root(language_reference *ref)
{
    if (ref->kind == LANGUAGE_REFERENCE_COMPONENT) {
        pin_value root = ref->root;
        root.ptr = NULL;
        const ecs_execution_view *world = ref->owner->world;
        if (!world) {
            return root;
        }
        uint32_t row = ecs_execution_entity(world, ref->entity);
        const ecs_execution_column *column = ecs_execution_component(world, row, ref->component);
        if (column && column->layout == ref->layout && column->stride == ref->stride) {
            root.ptr = (char *)column->data + (size_t)row * column->stride;
        }
        return root;
    }
    if (ref->kind == LANGUAGE_REFERENCE_MANAGED) {
        if (ref->managed->component) {
            pin_value value = ecs_component_cell_value(ref->managed->component);
            value.record = ref->managed->value.record;
            if (value.record) {
                value.schema = NULL;
            }
            return value;
        }
        return ref->managed->value;
    }
    return ref->kind == LANGUAGE_REFERENCE_STATE
             ? ref->owner->variables[ref->variable]
             : *ref->value;
}
static bool invalid_component_target(const language_reference *ref, pin_value root)
{
    return ref->kind == LANGUAGE_REFERENCE_MANAGED && ref->managed->component &&
           !root.ptr;
}
static const char invalid_component_target_error
    [] = "Component reference target is no longer available";
static const type_desc *reference_root_layout(const language_reference *ref)
{
    if (ref->kind == LANGUAGE_REFERENCE_MANAGED && ref->managed->component) {
        return ecs_component_cell_type(ref->managed->component);
    }
    if (ref->kind != LANGUAGE_REFERENCE_COMPONENT) {
        return NULL;
    }
    const ecs_execution_column *column = ecs_execution_find_column(ref->owner->world, ref->component);
    return column && column->layout == ref->layout && column->stride == ref->stride
             ? column->type
             : NULL;
}
static type_resolver reference_resolver(const language_reference *ref)
{
    return ref->kind == LANGUAGE_REFERENCE_MANAGED && ref->managed->component
             ? ecs_component_cell_resolver(ref->managed->component)
             : (type_resolver){ 0 };
}
/* Whether layout has a field of the claimed type at offset: a field path
 * copies by its claimed type, which must be the field's own. Names of scalar
 * types do not change their storage. */
static bool field_at(const type_desc *layout, size_t offset, pin_value type)
{
    for (int i = 0; i < layout->field_count; i++) {
        const type_field *field = &layout->fields[i];
        bool named = (type.type != PIN_STRUCT && type.type != PIN_POINTER) ||
                     (field->type_name && type.type_name &&
                      !strcmp(field->type_name, type.type_name));
        if (field->layout && type.type == PIN_STRUCT) {
            pin_value expected = { .type = field->type,
                                   .container = field->container,
                                   .type_name = field->type_name,
                                   .record = field->layout->carrier };
            named &= pin_value_layout_same(&expected, &type);
        }
        if (field->offset == offset && field->type == type.type &&
            type.container == field->container && named) {
            return true;
        }
    }
    return false;
}
static void *reference_path_data_mode(
    arena *storage,
    bool writable,
    const language_reference_path *path,
    pin_value *type,
    void *data,
    const type_desc *root_layout,
    type_resolver resolver
)
{
    if (!path || !data) {
        return data;
    }
    data = reference_path_data_mode(storage, writable, path->parent, type, data, root_layout, resolver);
    if (!data) {
        return NULL;
    }
    if (path->union_payload) {
        if (type->type != PIN_UNION || type->container != PIN_CONTAINER_SINGLE) {
            return NULL;
        }
        pin_union *sum;
        memcpy(&sum, data, sizeof sum);
        if (!sum || (path->tag && sum->tag != path->tag) || path->index < 0 ||
            path->index >= sum->count ||
            !same_storage(path->type, sum->values[path->index])) {
            return NULL;
        }
        pin_value *item = &sum->values[path->index];
        *type = *item;
        return item->type == PIN_STRUCT && item->container == PIN_CONTAINER_SINGLE
                 ? item->ptr
                 : &item->d;
    }
    if (path->map) {
        if (type->container != PIN_CONTAINER_MAP) {
            return NULL;
        }
        pin_map *map;
        memcpy(&map, data, sizeof map);
        if (writable && (!map || pin_map_make_unique(storage, map))) {
            return NULL;
        }
        int index = pin_map_find(map, &path->key);
        if (index < 0) {
            return NULL;
        }
        pin_value *item = &map->entries[index].value;
        pin_map expected = { .key_type = map->key_type, .value_type = path->type };
        if (!pin_map_entry_valid(&expected, &path->key, item)) {
            return NULL;
        }
        *type = *item;
        return item->type == PIN_STRUCT && item->container == PIN_CONTAINER_SINGLE
                 ? item->ptr
                 : &item->d;
    }
    if (path->element) {
        if (type->container != PIN_CONTAINER_ARRAY &&
            type->container != PIN_CONTAINER_VIEW) {
            return NULL;
        }
        pin_array *array;
        memcpy(&array, data, sizeof array);
        if (!array || !pin_array_view_valid(array) || path->index < 0 ||
            path->index >= array->count) {
            return NULL;
        }
        /* A collected array may share its backing, so a write makes it
         * unique first. Headers can share an arena array, so a write gives
         * the path a copy of its own in the owner's storage. */
        pin_array *root = (pin_array *)pin_array_owner(array);
        if (writable &&
            (!root || (root->storage && pin_array_make_unique(root->storage, root)))) {
            return NULL;
        }
        if (writable && !root->storage && root == array) {
            array = pin_array_snapshot_to_arena(storage, array);
            if (!array) {
                return NULL;
            }
            memcpy(data, &array, sizeof array);
        }
        *type = pin_array_get(array, path->index);
        return pin_array_data(array, path->index);
    }
    const type_desc *layout = !path->parent && root_layout ? root_layout
                            : type->type == PIN_STRUCT && type->container == PIN_CONTAINER_SINGLE
                                ? (resolver.find ? type_resolve(resolver, type->type_name)
                                                 : pin_value_type(type))
                                : NULL;
    if (!layout || !field_at(layout, path->offset, path->type)) {
        return NULL;
    }
    *type = path->type;
    return (char *)data + path->offset;
}
static void *reference_path_data(
    const language_reference_path *path,
    pin_value *type,
    void *data,
    const type_desc *root_layout,
    type_resolver resolver
)
{
    return reference_path_data_mode(NULL, false, path, type, data, root_layout, resolver);
}
static void *reference_data(language_reference *ref, pin_value *root)
{
    *root = reference_root(ref);
    pin_value type = *root;
    void *data = root->type == PIN_STRUCT && root->container == PIN_CONTAINER_SINGLE
                   ? root->ptr
                   : &root->d;
    return reference_path_data(ref->path, &type, data, reference_root_layout(ref), reference_resolver(ref));
}
static const type_field *reference_member_field(const language_reference *ref, pin_value root)
{
    const language_reference_path *path = ref->path;
    if (!path || path->element || path->map) {
        return NULL;
    }
    pin_value type = root;
    void *data = root.type == PIN_STRUCT && root.container == PIN_CONTAINER_SINGLE
                   ? root.ptr
                   : &root.d;
    const type_desc *root_layout = reference_root_layout(ref);
    type_resolver resolver = reference_resolver(ref);
    data = reference_path_data(path->parent, &type, data, root_layout, resolver);
    const type_desc *layout = !path->parent && root_layout ? root_layout
                            : resolver.find ? type_resolve(resolver, type.type_name)
                                            : pin_value_type(&type);
    for (int n = 0; data && layout && n < layout->field_count; n++) {
        const type_field *field = &layout->fields[n];
        if (field->offset == path->offset && field->type == path->type.type &&
            field->container == path->type.container) {
            return field;
        }
    }
    return NULL;
}
static const type_field *reference_text_field(const language_reference *ref, pin_value root)
{
    const type_field *field = reference_member_field(ref, root);
    return field && field->type == PIN_STR ? field : NULL;
}
static pin_array *reference_array(const language_reference *ref, pin_value *root)
{
    const language_reference_path *path = ref->path;
    assert(path && path->element);
    pin_value type = *root;
    void *data = root->type == PIN_STRUCT && root->container == PIN_CONTAINER_SINGLE
                   ? root->ptr
                   : &root->d;
    data = reference_path_data(path->parent, &type, data, reference_root_layout(ref), reference_resolver(ref));
    if (!data || (type.container != PIN_CONTAINER_ARRAY &&
                  type.container != PIN_CONTAINER_VIEW)) {
        return NULL;
    }
    pin_array *array;
    memcpy(&array, data, sizeof array);
    return array && pin_array_view_valid(array) && path->index >= 0 &&
                   path->index < array->count
             ? array
             : NULL;
}
size_t execution_value_size(pin_value type)
{
    if (type.container != PIN_CONTAINER_SINGLE) {
        return sizeof(void *);
    }
    if (type.type == PIN_STRUCT) {
        const type_desc *layout = pin_value_type(&type);
        return layout ? layout->size : 0;
    }
    return behavior_scalar_size(type.type);
}
static int snapshot_reference_root(
    arena *storage,
    pin_value *out,
    const pin_value *source,
    const type_desc *layout,
    type_resolver resolver
)
{
    if (!layout || source->record || source->schema) {
        return pin_value_snapshot_to_arena(storage, out, source);
    }
    assert(source->type == PIN_STRUCT && source->container == PIN_CONTAINER_SINGLE);
    *out = *source;
    pin_copy_record(PIN_COPY_VALUE_HEADER, 1);
    out->record = pin_record_from_type_resolved(storage, layout, resolver);
    if (!out->record) {
        return -1;
    }
    out->type_name = out->record->type.name;
    const type_desc *owned = &out->record->type;
    out->ptr = arena_alloc(storage, owned->size);
    memcpy(out->ptr, source->ptr, owned->size);
    pin_copy_record(PIN_COPY_RECORD, owned->size);
    return pin_struct_snapshot_fields(storage, owned, out->ptr);
}
/* Whether two headers describe the same storage: the same type and
 * container and, for records and unions, the same declaration. Names of
 * scalar types do not change their storage. Union headers name their
 * declaration only when the lowering knows it; two unnamed unions match. */
static bool same_storage(pin_value a, pin_value b)
{
    if (a.type != b.type || a.container != b.container) {
        return false;
    }
    if (a.type != PIN_STRUCT && a.type != PIN_UNION && a.type != PIN_POINTER) {
        return true;
    }
    if (a.record || b.record) {
        return pin_value_layout_same(&a, &b);
    }
    if (!a.type_name || !b.type_name) {
        return a.type == PIN_UNION && !a.type_name && !b.type_name;
    }
    return !strcmp(a.type_name, b.type_name);
}
/* A reference's root must hold the type it was made with: reads and writes
 * size and walk the root by that type. A component's root is its column's
 * record, when the column is the one the reference was made for. */
static bool root_matches(const language_reference *ref, pin_value root)
{
    if (ref->kind != LANGUAGE_REFERENCE_COMPONENT) {
        return same_storage(ref->root, root);
    }
    const type_desc *layout = reference_root_layout(ref);
    return !layout ||
           (ref->root.type == PIN_STRUCT && ref->root.container == PIN_CONTAINER_SINGLE &&
            ref->root.type_name && !strcmp(ref->root.type_name, layout->name));
}
static const char *array_mutate_place(arena *storage, const pin_value *args, int count, pin_value *out)
{
    if (count < 2 || count > 4) {
        return "Invalid array mutation arguments";
    }
    language_reference resolved,
        *ref = unbox((language_reference *)(uintptr_t)args[0].u64, &resolved);
    /* `out.append(x)` and `out.extend(xs)` through a reference to a whole
     * local array change the local's header in its slot, as the local's own
     * push and extend do. Resolving a path, detaching the owner and storing
     * a persistent root back are for fields, state and components. */
    execution_array_mutation operation = (execution_array_mutation)args[1].i;
    if (ref->kind == LANGUAGE_REFERENCE_VALUE && !ref->path && count == 3 &&
        ref->root.container == PIN_CONTAINER_ARRAY &&
        (operation == EXECUTION_ARRAY_APPEND ||
         (operation == EXECUTION_ARRAY_EXTEND && sequence_array(args[2])))) {
        if (!root_matches(ref, *ref->value)) {
            return "Array mutation owner differs from its reference";
        }
        pin_value local[] = { { .u64 = (uintptr_t)ref->value }, args[2] };
        return operation == EXECUTION_ARRAY_APPEND
                 ? array_push(storage, local, 2, out)
                 : array_extend(storage, local, 2, out);
    }
    pin_value root = reference_root(ref), target = root;
    if (!root_matches(ref, root)) {
        return "Array mutation owner differs from its reference";
    }
    bool persistent = ref->kind == LANGUAGE_REFERENCE_COMPONENT ||
                      ref->kind == LANGUAGE_REFERENCE_STATE;
    if (persistent && pin_value_collection_copy(storage, &root, &root)) {
        return "Cannot copy array mutation owner";
    }
    void *data = root.type == PIN_STRUCT && root.container == PIN_CONTAINER_SINGLE
                   ? root.ptr
                   : &root.d;
    data = reference_path_data_mode(
        storage,
        true,
        ref->path,
        &target,
        data,
        reference_root_layout(ref),
        reference_resolver(ref)
    );
    if (!data || !sequence_array(target)) {
        if (persistent) {
            pin_value_collection_release(&root);
        }
        return "Array mutation place is unavailable";
    }
    memcpy(&target.ptr, data, sizeof target.ptr);
    if (!owned_array(storage, &target)) {
        if (persistent) {
            pin_value_collection_release(&root);
        }
        return "Cannot retain array mutation place";
    }
    memcpy(data, &target.ptr, sizeof target.ptr);
    pin_value values[4] = { target, args[1] };
    for (int i = 2; i < count; i++) {
        values[i] = args[i];
    }
    const char *error = array_mutate(storage, values, count, out);
    if (!error && ref->kind == LANGUAGE_REFERENCE_STATE) {
        if (!ref->owner->store_value) {
            error = "Array mutation state owner is unavailable";
        } else {
            ref->owner->store_value(ref->owner->value_owner, ref->variable, &root);
        }
    } else if (!error && ref->kind == LANGUAGE_REFERENCE_COMPONENT) {
        if (!ref->owner->store_component ||
            ref->owner->store_component(ref->owner, ref->entity, ref->component, ref->layout, 0, ref->root, root)) {
            error = "Cannot retain array mutation component";
        }
    }
    if (persistent) {
        pin_value_collection_release(&root);
    }
    return error;
}
const node_host_call EXECUTION_array_mutate_place_call = { .value = array_mutate_place,
                                                           .variadic = true };
static const char *map_delete_place(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 2);
    (void)count;
    language_reference resolved,
        *ref = unbox((language_reference *)(uintptr_t)args[0].u64, &resolved);
    pin_value root = reference_root(ref), target = root;
    if (!root_matches(ref, root)) {
        return "Map deletion owner differs from its reference";
    }
    bool persistent = ref->kind == LANGUAGE_REFERENCE_COMPONENT ||
                      ref->kind == LANGUAGE_REFERENCE_STATE;
    if (persistent && pin_value_collection_copy(storage, &root, &root)) {
        return "Cannot copy map deletion owner";
    }
    void *data = root.type == PIN_STRUCT && root.container == PIN_CONTAINER_SINGLE
                   ? root.ptr
                   : &root.d;
    data = reference_path_data_mode(
        storage,
        true,
        ref->path,
        &target,
        data,
        reference_root_layout(ref),
        reference_resolver(ref)
    );
    const char *error = NULL;
    if (!data || target.container != PIN_CONTAINER_MAP) {
        error = "Map deletion place is unavailable";
    }
    pin_map *map = NULL;
    if (!error) {
        memcpy(&map, data, sizeof map);
        if (pin_map_delete_owned(storage, map, &args[1])) {
            error = "Invalid map removal key";
        }
    }
    if (!error && ref->kind == LANGUAGE_REFERENCE_STATE) {
        if (!ref->owner->store_value) {
            error = "Map deletion state owner is unavailable";
        } else {
            ref->owner->store_value(ref->owner->value_owner, ref->variable, &root);
        }
    } else if (!error && ref->kind == LANGUAGE_REFERENCE_COMPONENT) {
        if (!ref->owner->store_component ||
            ref->owner->store_component(ref->owner, ref->entity, ref->component, ref->layout, 0, ref->root, root)) {
            error = "Cannot retain map deletion component";
        }
    }
    if (persistent) {
        pin_value_collection_release(&root);
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = !error };
    return error;
}
const node_host_call EXECUTION_map_delete_place_call = { .value = map_delete_place };

/* A second argument is what an absent map key reads, its value type's zero. */
static const char *read_reference(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 1 || count == 2);
    language_reference view,
        *ref = unbox((language_reference *)(uintptr_t)args[0].u64, &view);
    /* The result keeps the header it was declared with, which the verifier
     * checked every later use against. */
    pin_value declared = *out, root;
    void *data = reference_data(ref, &root);
    if (invalid_component_target(ref, root)) {
        return invalid_component_target_error;
    }
    if (!root_matches(ref, root)) {
        return "Reference type differs from its storage";
    }
    if (ref->path && ref->path->element) {
        const pin_array *array = reference_array(ref, &root);
        if (!array) {
            return "Referenced array element is unavailable";
        }
        pin_value item = pin_array_get(array, ref->path->index), copy;
        if (pin_value_snapshot_to_arena(storage, &copy, &item)) {
            return "Referenced array element is unavailable";
        }
        if (!same_storage(declared, copy)) {
            return "Reference read differs from its declared result";
        }
        *out = copy;
        return NULL;
    }
    if (!same_storage(declared, ref->type)) {
        return "Reference read differs from its declared result";
    }
    if (!data && count == 2 && ref->path && ref->path->map) {
        return pin_value_snapshot_to_arena(storage, out, &args[1])
                 ? "Cannot retain referenced value"
                 : NULL;
    }
    const type_desc *layout = !ref->path ? reference_root_layout(ref) : NULL;
    type_resolver resolver = reference_resolver(ref);
    if (!layout && resolver.find && declared.type == PIN_STRUCT) {
        layout = type_resolve(resolver, declared.type_name);
    }
    size_t size = layout ? layout->size : execution_value_size(ref->type);
    if ((!data && (ref->path || ref->kind == LANGUAGE_REFERENCE_COMPONENT)) || !size) {
        return "Referenced storage is unavailable";
    }
    const type_field *text_field = reference_text_field(ref, root);
    const type_field *member_field = reference_member_field(ref, root);
    if (member_field && member_field->type == PIN_ENUM) {
        type_field field = *member_field;
        field.offset = 0;
        pin_value value;
        const char *error = pin_read_field(storage, data, &field, &value);
        if (!error) {
            value.type_name = ref->type.type_name;
            *out = value;
        }
        return error;
    }
    if (text_field && !text_field->text_descriptor) {
        const char *text;
        memcpy(&text, data, sizeof text);
        pin_value value;
        const char *error = pin_value_text(storage, &value, text ? text : "", text ? strlen(text) : 0);
        if (error) {
            return error;
        }
        value.type_name = ref->type.type_name;
        *out = value;
        return NULL;
    }
    *out = ref->type;
    if (out->type == PIN_STRUCT && out->container == PIN_CONTAINER_SINGLE) {
        out->ptr = data;
        out->schema = root.schema;
    } else {
        memcpy(&out->d, data, size);
    }
    pin_value retained;
    if (snapshot_reference_root(storage, &retained, out, layout, resolver)) {
        return "Cannot retain referenced value";
    }
    *out = retained;
    return NULL;
}
static bool component_scalar_path(const language_reference *ref)
{
    bool component = ref && (ref->kind == LANGUAGE_REFERENCE_COMPONENT ||
                             (ref->kind == LANGUAGE_REFERENCE_MANAGED &&
                              ref->managed && ref->managed->component));
    if (!component || !ref->path || ref->type.container != PIN_CONTAINER_SINGLE ||
        ref->type.type == PIN_STRUCT) {
        return false;
    }
    for (const language_reference_path *path = ref->path; path; path = path->parent) {
        if (path->element || path->map) {
            return false;
        }
    }
    return true;
}
static const char *write_reference_value(arena *owner_storage, language_reference *ref, const pin_value *args)
{
    if (ref->kind == LANGUAGE_REFERENCE_MANAGED && !ref->managed->component) {
        const char *error = behavior_managed_storage_value_error(ref->managed->storage, &args[1]);
        if (error) {
            return error;
        }
    }
    pin_value root;
    void *current = reference_data(ref, &root);
    if (invalid_component_target(ref, root)) {
        return invalid_component_target_error;
    }
    /* The value is copied by the reference's type, and an unpathed or element
     * write replaces the root's or the item's header with the value's. */
    bool element = ref->path && ref->path->element;
    bool map = ref->path && ref->path->map;
    const pin_array *array = element ? reference_array(ref, &root) : NULL;
    pin_value item = array ? pin_array_get(array, ref->path->index)
                           : (pin_value){ 0 };
    if (!root_matches(ref, root) || !same_storage(ref->type, args[1]) ||
        (array && !same_storage(item, args[1]))) {
        return "Reference write differs from the reference's type";
    }
    if (!current && !map &&
        (element ? !array
                 : ref->path || ref->kind == LANGUAGE_REFERENCE_COMPONENT ||
                       (ref->kind == LANGUAGE_REFERENCE_MANAGED &&
                        ref->managed->component))) {
        return "Referenced storage is unavailable";
    }
    if (ref->kind == LANGUAGE_REFERENCE_MANAGED && ref->managed->component) {
        size_t offset = ref->path ? (size_t)((char *)current - (char *)root.ptr) : 0;
        return ecs_component_cell_write(ref->managed->component, offset, ref->type, args[1])
                 ? "Cannot retain referenced component write"
                 : NULL;
    }
    if (!owner_storage) {
        return "Referenced owner has no retained value storage";
    }
    const type_field *text_field = reference_text_field(ref, root);
    if (text_field && !text_field->text_descriptor) {
        const char *text;
        const char *error = pin_text_cstring(owner_storage, args[1], &text, NULL);
        if (error) {
            return error;
        }
    }
    if (component_scalar_path(ref)) {
        size_t offset = (const char *)current - (const char *)root.ptr;
        return !ref->owner->store_component || ref->owner->store_component(
                                                   ref->owner,
                                                   ref->entity,
                                                   ref->component,
                                                   ref->layout,
                                                   offset,
                                                   ref->type,
                                                   args[1]
                                               )
                 ? "Cannot retain referenced component write"
                 : NULL;
    }
    if (ref->kind == LANGUAGE_REFERENCE_COMPONENT && !ref->path) {
        return !ref->owner->store_component || ref->owner->store_component(
                                                   ref->owner,
                                                   ref->entity,
                                                   ref->component,
                                                   ref->layout,
                                                   0,
                                                   root,
                                                   args[1]
                                               )
                 ? "Cannot retain referenced component write"
                 : NULL;
    }
    const type_desc *root_layout = reference_root_layout(ref);
    bool persistent = ref->kind == LANGUAGE_REFERENCE_COMPONENT ||
                      ref->kind == LANGUAGE_REFERENCE_STATE;
    pin_value value, retained = root;
    if (pin_value_collection_copy(owner_storage, &value, &args[1]) ||
        (persistent && ref->path &&
         pin_value_collection_copy(owner_storage, &retained, &root))) {
        return "Cannot retain referenced value";
    }
    if (!ref->path) {
        retained = value;
    } else if (map) {
        pin_value type = retained;
        void *data = retained.type == PIN_STRUCT && retained.container == PIN_CONTAINER_SINGLE
                       ? retained.ptr
                       : &retained.d;
        void *parent = reference_path_data_mode(
            owner_storage,
            true,
            ref->path->parent,
            &type,
            data,
            root_layout,
            reference_resolver(ref)
        );
        if (!parent || type.container != PIN_CONTAINER_MAP) {
            return "Referenced Map owner is unavailable";
        }
        pin_map *old;
        memcpy(&old, parent, sizeof old);
        if (pin_map_store_owned(owner_storage, &old, &ref->path->key, &value)) {
            pin_value_collection_release(&value);
            return "Cannot retain Map update";
        }
        memcpy(parent, &old, sizeof old);
        pin_value_collection_release(&value);
    } else if (element) {
        pin_value parent_type = retained;
        void *parent_data = retained.type == PIN_STRUCT &&
                                    retained.container == PIN_CONTAINER_SINGLE
                              ? retained.ptr
                              : &retained.d;
        if (!reference_path_data_mode(
                owner_storage,
                true,
                ref->path->parent,
                &parent_type,
                parent_data,
                root_layout,
                reference_resolver(ref)
            )) {
            return "Referenced array owner is unavailable";
        }
        pin_array *array = reference_array(ref, &retained);
        if (!array ||
            (array->stride && value.type == PIN_STRUCT && !value.ptr
                 ? pin_array_snapshot_set(owner_storage, array, ref->path->index, &value)
                 : pin_array_set(array, ref->path->index, &value))) {
            return "Referenced array element is unavailable";
        }
    } else {
        pin_value type = retained;
        void *data = retained.type == PIN_STRUCT && retained.container == PIN_CONTAINER_SINGLE
                       ? retained.ptr
                       : &retained.d;
        void *target = reference_path_data_mode(
            owner_storage,
            true,
            ref->path,
            &type,
            data,
            root_layout,
            reference_resolver(ref)
        );
        size_t size = execution_value_size(ref->type);
        if (!target || !size) {
            return "Referenced path is unavailable";
        }
        const void *source = value.type == PIN_STRUCT && value.container == PIN_CONTAINER_SINGLE
                               ? value.ptr
                               : &value.d;
        if (text_field && !text_field->text_descriptor) {
            type_field field = *text_field;
            field.offset = 0;
            const char *error = pin_write_field(owner_storage, target, &field, value);
            if (error) {
                return error;
            }
        } else {
            pin_value previous = ref->type;
            if (previous.type == PIN_STRUCT &&
                previous.container == PIN_CONTAINER_SINGLE) {
                previous.ptr = target;
            } else {
                memcpy(&previous.d, target, size);
            }
            if (ref->kind == LANGUAGE_REFERENCE_VALUE) {
                behavior_function_collection_transfer(ref->owner, NULL, &previous);
            }
            pin_value_collection_release(&previous);
            memcpy(target, source, size);
        }
    }
    if (ref->kind != LANGUAGE_REFERENCE_COMPONENT && retained.type == PIN_STRUCT &&
        retained.container == PIN_CONTAINER_SINGLE) {
        const type_desc *type = pin_value_type(&retained);
        assert(type);
        type_validate_ranges(type, retained.ptr);
    }
    if (ref->kind == LANGUAGE_REFERENCE_COMPONENT) {
        if (!ref->owner->store_component ||
            ref->owner->store_component(ref->owner, ref->entity, ref->component, ref->layout, 0, root, retained)) {
            return "Cannot retain referenced component write";
        }
    } else if (ref->kind == LANGUAGE_REFERENCE_STATE) {
        if (!ref->owner->store_value) {
            return "Referenced state owner has no persistent storage";
        }
        ref->owner->store_value(ref->owner->value_owner, ref->variable, &retained);
    } else if (ref->kind == LANGUAGE_REFERENCE_MANAGED) {
        ref->managed->value = retained;
        if (!ref->path) {
            pin_value_collection_release(&root);
        }
    } else {
        *ref->value = retained;
        if (!ref->path) {
            behavior_function_collection_drop(ref->owner, &root);
        }
        behavior_function_collection_retain(ref->owner, &retained);
    }
    if (persistent) {
        if (ref->path) {
            pin_value_collection_release(&retained);
        } else {
            pin_value_collection_release(&value);
        }
    }
    return NULL;
}
static const char *write_reference(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 2);
    (void)count;
    language_reference view,
        *ref = unbox((language_reference *)(uintptr_t)args[0].u64, &view);
    const char *error;
    if (ref->kind == LANGUAGE_REFERENCE_VALUE && ref->box &&
        ref->box->owner == &ref->box->detached) {
        execution_box *box = ref->box;
        assert(box->value == &box->kept && box->detached.value_storage == &box->storage);
        /* Logical references keep the box, not an address in its payload.
         * Stage the write without publishing, then retain only the final root:
         * a path write's working snapshot still includes the replaced value. */
        arena working = { 0 }, committed = { 0 };
        behavior_function_context staged_owner = { .managed_heap = box->owner->managed_heap,
                                                   .value_storage = &working };
        arena *heap = staged_owner.managed_heap ? &staged_owner.managed_heap->storage
                                                : &working;
        pin_value candidate, retained;
        error = pin_value_collection_copy(heap, &candidate, &box->kept)
                  ? "Cannot copy referenced box value"
                  : NULL;
        if (!error) {
            behavior_function_collection_retain(&staged_owner, &candidate);
            language_reference staged = *ref;
            staged.value = &candidate;
            staged.owner = &staged_owner;
            staged.box = NULL;
            error = write_reference_value(heap, &staged, args);
        }
        if (!error && pin_value_snapshot_to_arena(&committed, &retained, &candidate)) {
            error = "Cannot retain referenced value";
        }
        if (!error) {
            arena previous = box->storage;
            behavior_function_collection_drop(&box->detached, &box->kept);
            behavior_function_collection_transfer(&staged_owner, &box->detached, &retained);
            box->kept = retained;
            box->storage = committed;
            arena_free(&previous);
        } else {
            arena_free(&committed);
        }
        behavior_function_collection_clear(&staged_owner);
        arena_free(&working);
    } else if (ref->kind == LANGUAGE_REFERENCE_MANAGED) {
        error = write_reference_value(ref->managed->storage, ref, args);
    } else if (ref->kind == LANGUAGE_REFERENCE_VALUE) {
        error = write_reference_value(
            ref->owner->managed_heap ? &ref->owner->managed_heap->storage
                                     : ref->owner->value_storage,
            ref,
            args
        );
    } else {
        /* Scalars never allocate, so the arena gets a block only on demand. */
        arena temporary = { 0 };
        arena *owner = ref->owner->managed_heap ? &ref->owner->managed_heap->storage
                                                : &temporary;
        error = write_reference_value(owner, ref, args);
        arena_free(&temporary);
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = !error };
    return error;
}

/* Managed cells contain values, never invocation contexts or frame addresses. */
static pin_pointer *pointer_root(pin_pointer *pointer)
{
    while (pointer->base) {
        pointer = pointer->base;
    }
    return pointer;
}
static language_reference pointer_view(pin_pointer *pointer)
{
    pin_pointer *root = pointer_root(pointer);
    return (language_reference){ .kind = LANGUAGE_REFERENCE_MANAGED,
                                 .managed = root,
                                 .root = root->value,
                                 .type = pointer->value,
                                 .path = pointer->path,
                                 .variable = -1 };
}
static const char *pointer_make(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 3);
    (void)count;
    behavior_function_context *context = value_context(args[0]);
    if (!context || !context->managed_heap ||
        !context->managed_heap->storage.collectable) {
        return "Managed pointer requires an initialized caller-owned managed "
               "heap";
    }
    if (!same_storage(args[1], args[2])) {
        return "Managed pointer initializer differs from its pointee type";
    }
    const char *error = behavior_managed_storage_value_error(
        &context->managed_heap->storage,
        &args[2]
    );
    if (error) {
        return error;
    }
    arena *heap = &context->managed_heap->storage;
    pin_pointer *pointer = arena_alloc(heap, sizeof *pointer);
    *pointer = (pin_pointer){ .storage = heap };
    if (pin_value_collection_copy(heap, &pointer->value, &args[2])) {
        return "Cannot retain managed pointer initializer";
    }
    pin_arena_trace_pointer(heap, pointer);
    out->type = PIN_POINTER;
    out->container = PIN_CONTAINER_SINGLE;
    out->ptr = pointer;
    return NULL;
}
static void component_pointer_release(void *data)
{
    pin_pointer *pointer = data;
    ecs_component_cell_release_storage(pointer->component, pointer->storage);
}
static bool pointer_header(arena *heap, pin_value *out, pin_value header)
{
    header.ptr = NULL;
    if (header.record) {
        return !pin_value_snapshot_to_arena(heap, out, &header);
    }
    *out = header;
    if (header.schema) {
        out->schema = pin_schema_snapshot(heap, header.schema);
    }
    if (header.type_name) {
        size_t bytes = strlen(header.type_name) + 1;
        char *name = arena_alloc(heap, bytes);
        memcpy(name, header.type_name, bytes);
        out->type_name = name;
    }
    return true;
}
static const char *component_pointer(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 6);
    (void)count;
    behavior_function_context *context = value_context(args[0]);
    if (!context || !context->managed_heap ||
        !context->managed_heap->storage.collectable || !context->world) {
        return "Component pointer requires a managed heap and execution world";
    }
    ecs_component_cell *cell = ecs_component_cell_acquire(
        (ecs_world *)context->world,
        ecs_entity_from_u64(args[1].u64),
        args[2].u64
    );
    out->type = PIN_POINTER;
    out->container = PIN_CONTAINER_SINGLE;
    out->ptr = NULL;
    if (!cell) {
        return ecs_component_has(
                   (ecs_world *)context->world,
                   ecs_entity_from_u64(args[1].u64),
                   args[2].u64
               )
                 ? "Component pointer requires a retainable reflected layout"
                 : NULL;
    }
    const type_desc *type = ecs_component_cell_type(cell);
    const ecs_execution_column
        *column = ecs_execution_find_column(context->world, args[2].u64);
    if (args[3].type != PIN_STRUCT ||
        args[3].container != PIN_CONTAINER_SINGLE || !args[3].type_name ||
        strcmp(args[3].type_name, type->name) || args[5].u64 != type->size ||
        args[5].u64 != column->stride || args[4].u64 != column->layout) {
        ecs_component_cell_release(cell);
        return "Component pointer differs from its pointee type";
    }
    arena *heap = &context->managed_heap->storage;
    pin_pointer *pointer = arena_alloc(heap, sizeof *pointer);
    *pointer = (pin_pointer){ .storage = heap, .component = cell };
    if (!pointer_header(heap, &pointer->value, args[3])) {
        ecs_component_cell_release(cell);
        return "Cannot retain component pointer type";
    }
    ecs_component_cell_retain_storage(cell, heap);
    pin_arena_trace_pointer(heap, pointer);
    arena_set_cleanup(heap, pointer, component_pointer_release, pointer, 0);
    out->ptr = pointer;
    return NULL;
}
static const char *pointer_read(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 2);
    (void)count;
    if (!args[0].ptr) {
        return "Cannot dereference an absent managed pointer";
    }
    language_reference view = pointer_view(args[0].ptr);
    pin_value reference = { .type = BF_POINTER_TYPE, .u64 = (uintptr_t)&view };
    return read_reference(storage, &reference, 1, out);
}
static const char *pointer_write(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 2);
    (void)count;
    if (!args[0].ptr) {
        return "Cannot dereference an absent managed pointer";
    }
    language_reference view = pointer_view(args[0].ptr);
    pin_value values[] = { { .type = BF_POINTER_TYPE, .u64 = (uintptr_t)&view },
                           args[1] };
    return write_reference(storage, values, 2, out);
}
static const char *pointer_field(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 5);
    (void)count;
    if (!args[0].ptr) {
        return "Cannot project an absent managed pointer";
    }
    if (args[3].b) {
        return "Managed pointers to array entries are not supported";
    }
    pin_pointer *base = args[0].ptr;
    if (!same_storage(base->value, args[4])) {
        return "Managed pointer base differs from its expected record layout";
    }
    language_reference view = pointer_view(base);
    pin_value root;
    void *data = reference_data(&view, &root);
    if (invalid_component_target(&view, root)) {
        return invalid_component_target_error;
    }
    type_resolver resolver = reference_resolver(&view);
    const type_desc *layout = !base->path ? reference_root_layout(&view) : NULL;
    if (!layout) {
        layout = resolver.find ? type_resolve(resolver, base->value.type_name)
                               : pin_value_type(&base->value);
    }
    if (!data || !layout || !field_at(layout, args[2].u64, args[1])) {
        return "Managed pointer field differs from its record layout";
    }
    arena *heap = base->storage;
    pin_value owned;
    if (!pointer_header(heap, &owned, args[1])) {
        return "Cannot retain managed pointer field type";
    }
    language_reference_path *path = arena_alloc(heap, sizeof *path);
    *path = (language_reference_path){ .parent = base->path,
                                       .type = owned,
                                       .offset = args[2].u64 };
    arena_set_trace(heap, path, reference_path_trace, NULL);
    pin_pointer *pointer = arena_alloc(heap, sizeof *pointer);
    *pointer = (pin_pointer){ .base = base,
                              .value = owned,
                              .path = path,
                              .offset = args[2].u64,
                              .storage = heap };
    /* Projected values carry a header only; the root cell owns the payload. */
    pointer->value.ptr = NULL;
    pin_arena_trace_pointer(heap, pointer);
    out->type = PIN_POINTER;
    out->container = PIN_CONTAINER_SINGLE;
    out->ptr = pointer;
    return NULL;
}
bool execution_reference_managed_pointer(pin_value value, pin_value *out)
{
    const language_reference *reference = (const void *)(uintptr_t)value.u64;
    if (!reference || reference->kind != LANGUAGE_REFERENCE_MANAGED) {
        return false;
    }
    *out = (pin_value){ .type = PIN_POINTER, .ptr = reference->managed };
    return true;
}
static const char *pointer_reference(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 1);
    (void)count;
    if (!args[0].ptr) {
        return "Cannot reference an absent managed pointer";
    }
    language_reference *reference = arena_alloc(storage, sizeof *reference);
    *reference = pointer_view(args[0].ptr);
    arena_set_trace(storage, reference, reference_trace, NULL);
    *out = (pin_value){ .type = BF_POINTER_TYPE, .u64 = (uintptr_t)reference };
    return NULL;
}
const node_host_call EXECUTION_pointer_make_call = { .value = pointer_make };
const node_host_call EXECUTION_component_pointer_call = { .value = component_pointer };
const node_host_call EXECUTION_pointer_read_call = { .value = pointer_read };
const node_host_call EXECUTION_pointer_write_call = { .value = pointer_write };
const node_host_call EXECUTION_pointer_field_call = { .value = pointer_field };
const node_host_call EXECUTION_pointer_reference_call = { .value = pointer_reference };
static const char *reduce_reference(
    const behavior_function_context *caller,
    void *service,
    const pin_value *args,
    pin_value *out
)
{
    (void)service;
    language_reference *ref = (language_reference *)(uintptr_t)args[0].u64;
    if (!component_scalar_path(ref) || ref->type.type < PIN_I8 ||
        ref->type.type > PIN_F64 || args[1].type != ref->type.type ||
        args[1].container != PIN_CONTAINER_SINGLE || args[2].type != PIN_I32 ||
        args[2].container != PIN_CONTAINER_SINGLE || args[2].i < EXECUTION_REDUCTION_ADD ||
        args[2].i > EXECUTION_REDUCTION_ADD_UNORDERED) {
        return "Reduction requires a numeric component field and matching "
               "value";
    }
    behavior_function_context *context = (void *)caller;
    if (ref->kind == LANGUAGE_REFERENCE_MANAGED &&
        !ecs_component_cell_value(ref->managed->component).ptr) {
        return invalid_component_target_error;
    }
    uint64_t entity = ref->entity, component = ref->component, layout = ref->layout;
    if (!context || !context->world || !context->reduce_component ||
        (ref->kind == LANGUAGE_REFERENCE_MANAGED
             ? !ecs_component_cell_target(ref->managed->component, context->world, &entity, &component, &layout)
             : ref->owner->world != context->world)) {
        return "Reduction destination is unavailable";
    }
    pin_value root;
    const void *target = reference_data(ref, &root);
    if (!target || !root.ptr || !root_matches(ref, root)) {
        return "Reduction destination is unavailable";
    }
    size_t offset = (const char *)target - (const char *)root.ptr;
    int status = context->reduce_component(
        context,
        entity,
        component,
        layout,
        offset,
        ref->type,
        args[1],
        (execution_reduction_op)args[2].i
    );
    *out = (pin_value){ .type = PIN_BOOL, .b = status == 0 };
    return status ? "Cannot collect component reduction" : NULL;
}
/* A declaration starts a new local, so tasks keep the old one in its box. */
static const char *release_local(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 1);
    (void)count;
    const language_reference *ref = (const language_reference *)(uintptr_t)args[0]
                                        .u64;
    if (ref->owner->release_local) {
        ref->owner->release_local(ref->owner, ref->value);
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = true };
    return NULL;
}
const node_host_call EXECUTION_reference_make_call = { .value = make_reference };
const node_host_call EXECUTION_local_release_call = { .value = release_local };
const node_host_call EXECUTION_reference_field_call = { .value = field_reference };
const node_host_call EXECUTION_reference_read_call = { .value = read_reference };
const node_host_call EXECUTION_reference_write_call = { .value = write_reference };
const node_host_call EXECUTION_reference_reduce_call = { .context = reduce_reference };
static const char *read_action_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 3);
    (void)count;
    behavior_function_context *context = value_context(args[0]);
    const pin_value
        *value = context->action_result
                   ? context->action_result(context, args[1].u64, args[2].i)
                   : NULL;
    return !value || pin_value_snapshot_to_arena(storage, out, value)
             ? "Cannot read retained action result"
             : NULL;
}
static const char *complete_action_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count >= 2);
    behavior_function_context *context = value_context(args[0]);
    int pending = context->pending_commands;
    if (!context->complete_action ||
        context->complete_action(context, args[1].u64, args + 2, count - 2) < 0) {
        return "Cannot dispatch action completion";
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = context->pending_commands > pending };
    return NULL;
}
const node_host_call EXECUTION_action_read_call = { .value = read_action_value };
const node_host_call EXECUTION_action_complete_call = { .value = complete_action_value };

static const char *component_has_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 3);
    (void)count;
    const behavior_function_context *context = value_context(args[0]);
    if (!context->world) {
        return "Component lookup requires an ECS world";
    }
    uint32_t row = ecs_execution_entity(context->world, args[1].u64);
    *out = (pin_value){
        .type = PIN_BOOL,
        .b = ecs_execution_component(context->world, row, args[2].u64) != NULL
    };
    return NULL;
}
static const char *query_source_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 5);
    (void)count;
    const behavior_function_context *context = value_context(args[0]);
    if (!context->world || !context->world->query_source) {
        return "Query source lookup requires an ECS world";
    }
    *out = (pin_value){ .type = PIN_HANDLE,
                        .u64 = context->world->query_source(
                            context->world,
                            args[1].u64,
                            args[2].u64,
                            args[3].u64,
                            args[4].i
                        ) };
    return NULL;
}
const node_host_call EXECUTION_query_source_call = { .value = query_source_value };
static const char *query_target_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 4);
    (void)count;
    const behavior_function_context *context = value_context(args[0]);
    if (!context->world || !context->world->query_target) {
        return "Relationship iteration requires an ECS world";
    }
    *out = (pin_value){ .type = PIN_HANDLE,
                        .u64 = context->world->query_target(
                            context->world,
                            args[1].u64,
                            args[2].u64,
                            args[3].i
                        ) };
    return NULL;
}
const node_host_call EXECUTION_query_target_call = { .value = query_target_value };
static const char *component_membership_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 4);
    (void)count;
    behavior_function_context *context = value_context(args[0]);
    bool changed = false;
    if (!context->component_membership ||
        context->component_membership(context, args[1].u64, args[2].u64, args[3].b, &changed) <
            0) {
        return "Cannot change component membership";
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = changed };
    return NULL;
}
const node_host_call EXECUTION_component_has_call = { .value = component_has_value };
const node_host_call EXECUTION_component_membership_call = { .value = component_membership_value };
const node_access EXECUTION_component_read_access = { .flags = NODE_EFFECT_WORLD_READ };
const node_access EXECUTION_component_membership_access = {
    .flags = NODE_EFFECT_STRUCTURAL | NODE_EFFECT_HOST_SINK | NODE_EFFECT_ENTITY_SCOPED,
    .target_input = 2
};
static const char *break_request_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 2);
    (void)count;
    behavior_function_context *context = value_context(args[0]);
    if (!context->request_action_break ||
        context->request_action_break(context, args[1].u64) < 0) {
        return "Cannot request action loop break";
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = true };
    return NULL;
}
static const char *break_take_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 2);
    (void)count;
    behavior_function_context *context = value_context(args[0]);
    bool requested = false;
    if (!context->take_action_break ||
        context->take_action_break(context, args[1].u64, &requested) < 0) {
        return "Cannot consume action loop break";
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = requested };
    return NULL;
}
const node_host_call EXECUTION_break_request_call = { .value = break_request_value };
const node_host_call EXECUTION_break_take_call = { .value = break_take_value };
static const char *state_demand_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 3);
    (void)count;
    behavior_function_context *context = value_context(args[0]);
    int demand;
    if (!context->state_inputs ||
        context->state_inputs(context, args[1].u64, args[2].i, &demand) < 0) {
        return "Invalid action state input demand";
    }
    *out = (pin_value){ .type = PIN_I32, .i = demand };
    return NULL;
}
static const char *state_transition_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count >= 3 && count <= 6);
    behavior_function_context *context = value_context(args[0]);
    int selected;
    if (!context->state_transition ||
        context->state_transition(context, args[1].u64, args[2].i, args + 3, count - 3, &selected) <
            0) {
        return "Invalid action state transition";
    }
    *out = (pin_value){ .type = PIN_I32, .i = selected };
    return NULL;
}
static const char *retrigger_action_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 3);
    (void)count;
    behavior_function_context *context = value_context(args[0]);
    bool found;
    if (!context->retrigger_action ||
        context->retrigger_action(context, args[1].u64, args[2].d, &found) < 0) {
        return "Invalid retriggerable action timer";
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = found };
    return NULL;
}
const node_host_call EXECUTION_state_demand_call = { .value = state_demand_value };
const node_host_call EXECUTION_state_transition_call = { .value = state_transition_value };
const node_host_call EXECUTION_retrigger_action_call = { .value = retrigger_action_value };
/* A loop cursor measures bytes; the yielded value is exactly one Unicode
 * scalar. Reject overlong encodings, surrogate code points and values above
 * U+10FFFF rather than splitting malformed bytes into apparent characters. */
static int scalar_bytes(const unsigned char *p, int remaining)
{
    int count = p[0] < 0x80                  ? 1
              : p[0] >= 0xc2 && p[0] <= 0xdf ? 2
              : p[0] >= 0xe0 && p[0] <= 0xef ? 3
              : p[0] >= 0xf0 && p[0] <= 0xf4 ? 4
                                             : 0;
    if (!count || count > remaining) {
        return 0;
    }
    for (int i = 1; i < count; i++) {
        if (p[i] < 0x80 || p[i] > 0xbf) {
            return 0;
        }
    }
    if ((p[0] == 0xe0 && p[1] < 0xa0) || (p[0] == 0xed && p[1] >= 0xa0) ||
        (p[0] == 0xf0 && p[1] < 0x90) || (p[0] == 0xf4 && p[1] >= 0x90)) {
        return 0;
    }
    return count;
}
typedef struct
{
    int bytes;
    char text[];
} string_snapshot;
static const char *string_cursor(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 1);
    (void)count;
    pin_string text = pin_text(args[0]);
    size_t bytes = text.length;
    if (bytes > INT_MAX) {
        return "String iterator exceeds its byte cursor range";
    }
    string_snapshot *cursor = arena_alloc(storage, sizeof *cursor + bytes + 1);
    cursor->bytes = (int)bytes;
    if (bytes) {
        memcpy(cursor->text, text.bytes, bytes);
    }
    cursor->text[bytes] = 0;
    *out = (pin_value){ .type = BF_POINTER_TYPE, .u64 = (uintptr_t)cursor };
    return NULL;
}
/* Executable provenance permits only STRING_CURSOR-produced values here.
 * The snapshot owns its inline bytes, so GC needs no borrowed string edge. */
static const char *string_bytes(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 1);
    (void)count;
    const string_snapshot *cursor = (const void *)(uintptr_t)args[0].u64;
    if (!cursor) {
        return "String iterator requires a live cursor";
    }
    *out = (pin_value){ .type = PIN_I32, .i = cursor->bytes };
    return NULL;
}
static const char *string_step(arena *storage, const pin_value *args, int count, pin_value *out, bool next)
{
    assert(count == 2);
    (void)count;
    const string_snapshot *cursor = (const void *)(uintptr_t)args[0].u64;
    int offset = args[1].i;
    if (!cursor || offset < 0 || offset >= cursor->bytes) {
        return "String iterator is outside its snapshot";
    }
    const unsigned char *p = (const unsigned char *)cursor->text + offset;
    int width = scalar_bytes(p, cursor->bytes - offset);
    if (!width) {
        return "String iterator requires valid UTF-8";
    }
    if (next) {
        *out = (pin_value){ .type = PIN_I32, .i = offset + width };
        return NULL;
    }
    return pin_value_text(storage, out, (const char *)p, (size_t)width);
}
static const char *string_item(arena *storage, const pin_value *args, int count, pin_value *out)
{
    return string_step(storage, args, count, out, false);
}
static const char *string_next(arena *storage, const pin_value *args, int count, pin_value *out)
{
    return string_step(storage, args, count, out, true);
}
const node_host_call EXECUTION_string_bytes_call = { .value = string_bytes };
const node_host_call EXECUTION_string_item_call = { .value = string_item };
const node_host_call EXECUTION_string_next_call = { .value = string_next };
const node_host_call EXECUTION_string_cursor_call = { .value = string_cursor };
const node_host_call *execution_intrinsic_call(execution_intrinsic id)
{
    switch (id) {
        case EXECUTION_INTRINSIC_COMPONENT_SNAPSHOT:
            return &EXECUTION_component_snapshot_call;
        case EXECUTION_INTRINSIC_QUERY_TEST: return &EXECUTION_query_test_call;
        case EXECUTION_INTRINSIC_QUERY_MAKE: return &EXECUTION_query_make_call;
        case EXECUTION_INTRINSIC_QUERY_BEGIN:
            return &EXECUTION_query_begin_call;
        case EXECUTION_INTRINSIC_QUERY_NEXT: return &EXECUTION_query_next_call;
        case EXECUTION_INTRINSIC_QUERY_END: return &EXECUTION_query_end_call;
        case EXECUTION_INTRINSIC_QUERY_ROW_GET:
            return &EXECUTION_query_row_get_call;
        case EXECUTION_INTRINSIC_QUERY_CURRENT_ROW:
            return &EXECUTION_query_current_row_call;
        case EXECUTION_INTRINSIC_COMPONENT_POINTER:
            return &EXECUTION_component_pointer_call;
        case EXECUTION_INTRINSIC_POINTER_MAKE:
            return &EXECUTION_pointer_make_call;
        case EXECUTION_INTRINSIC_POINTER_READ:
            return &EXECUTION_pointer_read_call;
        case EXECUTION_INTRINSIC_POINTER_WRITE:
            return &EXECUTION_pointer_write_call;
        case EXECUTION_INTRINSIC_POINTER_FIELD:
            return &EXECUTION_pointer_field_call;
        case EXECUTION_INTRINSIC_POINTER_REFERENCE:
            return &EXECUTION_pointer_reference_call;
        case EXECUTION_INTRINSIC_STRING_CURSOR:
            return &EXECUTION_string_cursor_call;
        case EXECUTION_INTRINSIC_STRING_BYTES:
            return &EXECUTION_string_bytes_call;
        case EXECUTION_INTRINSIC_STRING_ITEM:
            return &EXECUTION_string_item_call;
        case EXECUTION_INTRINSIC_STRING_NEXT:
            return &EXECUTION_string_next_call;
        case EXECUTION_INTRINSIC_MAP_MAKE: return &EXECUTION_map_make_call;
        case EXECUTION_INTRINSIC_MAP_GET: return &EXECUTION_map_get_call;
        case EXECUTION_INTRINSIC_MAP_HAS: return &EXECUTION_map_has_call;
        case EXECUTION_INTRINSIC_MAP_REMOVE: return &EXECUTION_map_remove_call;
        case EXECUTION_INTRINSIC_MAP_ITEM: return &EXECUTION_map_item_call;
        case EXECUTION_INTRINSIC_MAP_STORE: return &EXECUTION_map_store_call;
        case EXECUTION_INTRINSIC_MAP_DELETE: return &EXECUTION_map_delete_call;
        case EXECUTION_INTRINSIC_REFERENCE_KEY:
            return &EXECUTION_reference_key_call;
        case EXECUTION_INTRINSIC_STATE_DEMAND:
            return &EXECUTION_state_demand_call;
        case EXECUTION_INTRINSIC_STATE_TRANSITION:
            return &EXECUTION_state_transition_call;
        case EXECUTION_INTRINSIC_RETRIGGER_ACTION:
            return &EXECUTION_retrigger_action_call;
        case EXECUTION_INTRINSIC_SNAPSHOT: return &EXECUTION_snapshot_call;
        case EXECUTION_INTRINSIC_CONSTRUCT: return &EXECUTION_construct_call;
        case EXECUTION_INTRINSIC_VALUE_EQUAL:
            return &EXECUTION_value_equal_call;
        case EXECUTION_INTRINSIC_SEQUENCE_SLICE:
            return &EXECUTION_sequence_slice_call;
        case EXECUTION_INTRINSIC_SEQUENCE_VIEW:
            return &EXECUTION_sequence_view_call;
        case EXECUTION_INTRINSIC_SEQUENCE_CLONE:
            return &EXECUTION_sequence_clone_call;
        case EXECUTION_INTRINSIC_SEQUENCE_GET:
            return &EXECUTION_sequence_get_call;
        case EXECUTION_INTRINSIC_SEQUENCE_BORROW:
            return &EXECUTION_sequence_borrow_call;
        case EXECUTION_INTRINSIC_SEQUENCE_LENGTH:
            return &EXECUTION_sequence_length_call;
        case EXECUTION_INTRINSIC_VALUE_COPY: return &EXECUTION_value_copy_call;
        case EXECUTION_INTRINSIC_VALUE_DROP: return &EXECUTION_value_drop_call;
        case EXECUTION_INTRINSIC_ARRAY_MUTATE_PLACE:
            return &EXECUTION_array_mutate_place_call;
        case EXECUTION_INTRINSIC_REFERENCE_UNION:
            return &EXECUTION_reference_union_call;
        case EXECUTION_INTRINSIC_MAP_DELETE_PLACE:
            return &EXECUTION_map_delete_place_call;
        case EXECUTION_INTRINSIC_ARRAY_PUSH: return &EXECUTION_array_push_call;
        case EXECUTION_INTRINSIC_ARRAY_STORE:
            return &EXECUTION_array_store_call;
        case EXECUTION_INTRINSIC_ARRAY_EXTEND:
            return &EXECUTION_array_extend_call;
        case EXECUTION_INTRINSIC_UNION_MAKE: return &EXECUTION_union_make_call;
        case EXECUTION_INTRINSIC_UNION_KEEP: return &EXECUTION_union_keep_call;
        case EXECUTION_INTRINSIC_UNION_TEST: return &EXECUTION_union_test_call;
        case EXECUTION_INTRINSIC_UNION_GET: return &EXECUTION_union_get_call;
        case EXECUTION_INTRINSIC_REFERENCE_MAKE:
            return &EXECUTION_reference_make_call;
        case EXECUTION_INTRINSIC_REFERENCE_FIELD:
            return &EXECUTION_reference_field_call;
        case EXECUTION_INTRINSIC_REFERENCE_READ:
            return &EXECUTION_reference_read_call;
        case EXECUTION_INTRINSIC_REFERENCE_WRITE:
            return &EXECUTION_reference_write_call;
        case EXECUTION_INTRINSIC_REFERENCE_REDUCE:
            return &EXECUTION_reference_reduce_call;
        case EXECUTION_INTRINSIC_ACTION_READ:
            return &EXECUTION_action_read_call;
        case EXECUTION_INTRINSIC_ACTION_COMPLETE:
            return &EXECUTION_action_complete_call;
        case EXECUTION_INTRINSIC_COMPONENT_HAS:
            return &EXECUTION_component_has_call;
        case EXECUTION_INTRINSIC_QUERY_SOURCE:
            return &EXECUTION_query_source_call;
        case EXECUTION_INTRINSIC_QUERY_TARGET:
            return &EXECUTION_query_target_call;
        case EXECUTION_INTRINSIC_COMPONENT_MEMBERSHIP:
            return &EXECUTION_component_membership_call;
        case EXECUTION_INTRINSIC_BREAK_REQUEST:
            return &EXECUTION_break_request_call;
        case EXECUTION_INTRINSIC_BREAK_TAKE: return &EXECUTION_break_take_call;
        case EXECUTION_INTRINSIC_LOCAL_RELEASE:
            return &EXECUTION_local_release_call;
        default: return NULL;
    }
}
uint64_t execution_intrinsic_id(const node_host_call *call)
{
    for (int id = EXECUTION_INTRINSIC_QUERY_MAKE;
         id <= EXECUTION_INTRINSIC_COMPONENT_SNAPSHOT;
         id++) {
        if (call == execution_intrinsic_call((execution_intrinsic)id)) {
            return UINT64_C(0x4348494900000000) | (unsigned)id;
        }
    }
    if (call == &EXECUTION_component_pointer_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_COMPONENT_POINTER;
    }
    for (int id = EXECUTION_INTRINSIC_POINTER_MAKE;
         id <= EXECUTION_INTRINSIC_POINTER_REFERENCE;
         id++) {
        if (call == execution_intrinsic_call((execution_intrinsic)id)) {
            return UINT64_C(0x4348494900000000) | (unsigned)id;
        }
    }

    for (int id = EXECUTION_INTRINSIC_STRING_BYTES;
         id <= EXECUTION_INTRINSIC_STRING_CURSOR;
         id++) {
        if (call == execution_intrinsic_call((execution_intrinsic)id)) {
            return UINT64_C(0x4348494900000000) | (unsigned)id;
        }
    }
    for (int id = EXECUTION_INTRINSIC_MAP_MAKE; id <= EXECUTION_INTRINSIC_MAP_DELETE;
         id++) {
        if (call == execution_intrinsic_call((execution_intrinsic)id)) {
            return UINT64_C(0x4348494900000000) | (unsigned)id;
        }
    }
    for (int id = EXECUTION_INTRINSIC_SEQUENCE_SLICE;
         id <= EXECUTION_INTRINSIC_SEQUENCE_BORROW;
         id++) {
        if (call == execution_intrinsic_call((execution_intrinsic)id)) {
            return UINT64_C(0x4348494900000000) | (unsigned)id;
        }
    }
    if (call == &EXECUTION_value_equal_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_VALUE_EQUAL;
    }
    if (call == &EXECUTION_query_target_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_QUERY_TARGET;
    }
    if (call == &EXECUTION_array_push_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_ARRAY_PUSH;
    }
    if (call == &EXECUTION_array_store_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_ARRAY_STORE;
    }
    if (call == &EXECUTION_array_extend_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_ARRAY_EXTEND;
    }
    if (call == &EXECUTION_union_make_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_UNION_MAKE;
    }
    if (call == &EXECUTION_union_keep_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_UNION_KEEP;
    }
    if (call == &EXECUTION_union_test_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_UNION_TEST;
    }
    if (call == &EXECUTION_union_get_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_UNION_GET;
    }
    if (call == &EXECUTION_query_source_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_QUERY_SOURCE;
    }
    if (call == &EXECUTION_state_demand_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_STATE_DEMAND;
    }
    if (call == &EXECUTION_state_transition_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_STATE_TRANSITION;
    }
    if (call == &EXECUTION_retrigger_action_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_RETRIGGER_ACTION;
    }
    if (call == &EXECUTION_snapshot_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_SNAPSHOT;
    }
    if (call == &EXECUTION_construct_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_CONSTRUCT;
    }
    if (call == &EXECUTION_reference_make_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_REFERENCE_MAKE;
    }
    if (call == &EXECUTION_reference_field_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_REFERENCE_FIELD;
    }
    if (call == &EXECUTION_reference_read_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_REFERENCE_READ;
    }
    if (call == &EXECUTION_reference_write_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_REFERENCE_WRITE;
    }
    if (call == &EXECUTION_reference_reduce_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_REFERENCE_REDUCE;
    }
    if (call == &EXECUTION_action_read_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_ACTION_READ;
    }
    if (call == &EXECUTION_action_complete_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_ACTION_COMPLETE;
    }
    if (call == &EXECUTION_component_has_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_COMPONENT_HAS;
    }
    if (call == &EXECUTION_component_membership_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_COMPONENT_MEMBERSHIP;
    }
    if (call == &EXECUTION_break_request_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_BREAK_REQUEST;
    }
    if (call == &EXECUTION_break_take_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_BREAK_TAKE;
    }
    if (call == &EXECUTION_local_release_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_LOCAL_RELEASE;
    }
    return 0;
}
/* The intrinsics that may build a returned local in the caller's storage;
 * union_keep runs only there. */
static bool result_kind(execution_intrinsic kind)
{
    return kind == EXECUTION_INTRINSIC_CONSTRUCT ||
           kind == EXECUTION_INTRINSIC_ARRAY_PUSH ||
           kind == EXECUTION_INTRINSIC_ARRAY_STORE ||
           kind == EXECUTION_INTRINSIC_ARRAY_EXTEND ||
           kind == EXECUTION_INTRINSIC_UNION_KEEP;
}
const node_host_call *execution_intrinsic_resolve(uint64_t id)
{
    execution_intrinsic kind = execution_intrinsic_kind(id);
    if (((id & ~EXECUTION_INTRINSIC_IN_RESULT) & UINT64_C(0xffffffffffffff00)) !=
        UINT64_C(0x4348494900000000)) {
        return NULL;
    }
    if (execution_intrinsic_in_result(id) ? !result_kind(kind)
                                          : kind == EXECUTION_INTRINSIC_UNION_KEEP) {
        return NULL;
    }
    return execution_intrinsic_call(kind);
}
const node_access *execution_intrinsic_access(uint64_t id)
{
    if (!execution_intrinsic_resolve(id)) {
        return NULL;
    }
    /* Building in the caller's storage writes outside the call. */
    if (execution_intrinsic_in_result(id)) {
        return &EXECUTION_assignment_access;
    }
    switch (execution_intrinsic_kind(id)) {
        case EXECUTION_INTRINSIC_VALUE_COPY:
        case EXECUTION_INTRINSIC_SEQUENCE_GET:
        case EXECUTION_INTRINSIC_SEQUENCE_BORROW:
        case EXECUTION_INTRINSIC_SEQUENCE_LENGTH:
        case EXECUTION_INTRINSIC_SEQUENCE_SLICE:
        case EXECUTION_INTRINSIC_SEQUENCE_VIEW:
        case EXECUTION_INTRINSIC_SEQUENCE_CLONE:
            return &EXECUTION_sequence_read_access;
        case EXECUTION_INTRINSIC_QUERY_MAKE:
        case EXECUTION_INTRINSIC_QUERY_ROW_GET:
        case EXECUTION_INTRINSIC_QUERY_CURRENT_ROW:
            return &EXECUTION_value_access;
        case EXECUTION_INTRINSIC_QUERY_TEST:
            return &EXECUTION_component_read_access;
        case EXECUTION_INTRINSIC_QUERY_BEGIN:
        case EXECUTION_INTRINSIC_QUERY_NEXT:
            return &EXECUTION_component_read_access;
        case EXECUTION_INTRINSIC_STRING_BYTES:
        case EXECUTION_INTRINSIC_STRING_CURSOR:
        case EXECUTION_INTRINSIC_STRING_ITEM:
        case EXECUTION_INTRINSIC_STRING_NEXT:
        case EXECUTION_INTRINSIC_MAP_MAKE:
        case EXECUTION_INTRINSIC_MAP_GET:
        case EXECUTION_INTRINSIC_MAP_HAS:
        case EXECUTION_INTRINSIC_MAP_REMOVE:
        case EXECUTION_INTRINSIC_MAP_ITEM:
        case EXECUTION_INTRINSIC_REFERENCE_KEY:
        case EXECUTION_INTRINSIC_SNAPSHOT:
        case EXECUTION_INTRINSIC_VALUE_EQUAL:
        case EXECUTION_INTRINSIC_CONSTRUCT:
        case EXECUTION_INTRINSIC_POINTER_MAKE:
        case EXECUTION_INTRINSIC_POINTER_FIELD:
        case EXECUTION_INTRINSIC_POINTER_REFERENCE:
        case EXECUTION_INTRINSIC_REFERENCE_MAKE:
        case EXECUTION_INTRINSIC_REFERENCE_FIELD:
        case EXECUTION_INTRINSIC_REFERENCE_UNION:
            return &EXECUTION_value_access;
        case EXECUTION_INTRINSIC_COMPONENT_HAS:
        case EXECUTION_INTRINSIC_COMPONENT_SNAPSHOT:
        case EXECUTION_INTRINSIC_COMPONENT_POINTER:
        case EXECUTION_INTRINSIC_QUERY_SOURCE:
        case EXECUTION_INTRINSIC_QUERY_TARGET:
            return &EXECUTION_component_read_access;
        case EXECUTION_INTRINSIC_COMPONENT_MEMBERSHIP:
            return &EXECUTION_component_membership_access;
        default: return &EXECUTION_assignment_access;
    }
}
uint64_t execution_intrinsic_abi(uint64_t id)
{
    if (execution_intrinsic_kind(id) >= EXECUTION_INTRINSIC_STRING_BYTES &&
        execution_intrinsic_kind(id) <= EXECUTION_INTRINSIC_STRING_NEXT &&
        execution_intrinsic_resolve(id)) {
        return 2;
    }
    return execution_intrinsic_resolve(id) ? 1 : 0;
}
