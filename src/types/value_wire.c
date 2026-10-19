#include "types/value_wire.h"

#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/map.h"
#include "types/record.h"
#include "types/scalar.h"
#include "types/trace.h"
#include "types/union.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

static void fail(execution_wire *w, const char *error)
{
    if (!w->error) {
        w->error = error;
    }
}
static bool available(execution_wire *w, size_t bytes)
{
    if (w->error) {
        return false;
    }
    if (w->size > EXECUTION_WIRE_MAX_BYTES || w->offset > w->size ||
        bytes > (w->reading ? w->size - w->offset : EXECUTION_WIRE_MAX_BYTES - w->size)) {
        fail(w, "Prepared byte limit or truncated input");
        return false;
    }
    if (w->reading && bytes && !w->data) {
        fail(w, "Prepared input is absent");
        return false;
    }
    return true;
}
void *execution_wire_allocate(execution_wire *w, size_t bytes)
{
    if (w->error) {
        return NULL;
    }
    if (!w->storage || bytes > EXECUTION_WIRE_MAX_BYTES - w->owned_bytes) {
        fail(w, "Prepared decoded storage limit exceeded");
        return NULL;
    }
    w->owned_bytes += bytes;
    if (!w->storage_budget) {
        return arena_alloc(w->storage, bytes);
    }
    size_t before = arena_storage_bytes(w->storage);
    size_t budget = *w->storage_budget > SIZE_MAX - before
                      ? SIZE_MAX
                      : before + *w->storage_budget;
    void *value = arena_alloc_bounded(w->storage, bytes, budget);
    if (!value) {
        fail(w, "Prepared aggregate storage budget exceeded");
        return NULL;
    }
    size_t growth = arena_storage_bytes(w->storage) - before;
    assert(growth <= *w->storage_budget);
    *w->storage_budget -= growth;
    w->storage_charged += growth;
    return value;
}
static void write_byte(execution_wire *w, unsigned char byte)
{
    if (w->hash) {
        *w->hash = (*w->hash ^ byte) * UINT64_C(1099511628211);
        return;
    }
    apush(w->data, byte);
}
/* Unsigned LEB128: seven bits per byte, low group first, shortest form only. */
void execution_wire_u64(execution_wire *w, uint64_t *value)
{
    if (!w->reading) {
        unsigned char bytes[10];
        size_t width = 0;
        uint64_t rest = *value;
        do {
            bytes[width] = rest & 0x7f;
            rest >>= 7;
            bytes[width++] |= rest ? 0x80 : 0;
        } while (rest);
        if (!available(w, width)) {
            return;
        }
        for (size_t i = 0; i < width; i++) {
            write_byte(w, bytes[i]);
        }
        w->size += width;
        w->offset = w->size;
        return;
    }
    uint64_t decoded = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (!available(w, 1)) {
            return;
        }
        unsigned char byte = w->data[w->offset++];
        if ((shift == 63 && byte > 1) || (shift && !byte)) {
            fail(w, "Invalid prepared integer encoding");
            return;
        }
        decoded |= (uint64_t)(byte & 0x7f) << shift;
        if (!(byte & 0x80)) {
            *value = decoded;
            return;
        }
    }
    fail(w, "Invalid prepared integer encoding");
}
/* Zigzag keeps small negative values, such as -1 for "none", one byte long. */
void execution_wire_int(execution_wire *w, int *value)
{
    int64_t signed_value = w->reading ? 0 : *value;
    uint64_t bits = ((uint64_t)signed_value << 1) ^ (uint64_t)(signed_value >> 63);
    execution_wire_u64(w, &bits);
    if (!w->reading || w->error) {
        return;
    }
    int64_t decoded = (int64_t)(bits >> 1) ^ -(int64_t)(bits & 1);
    if (decoded < INT_MIN || decoded > INT_MAX) {
        fail(w, "Prepared integer is outside domain range");
        return;
    }
    *value = (int)decoded;
}
void execution_wire_size(execution_wire *w, size_t *value)
{
    uint64_t bits = w->reading ? 0 : *value;
    execution_wire_u64(w, &bits);
    if (!w->reading || w->error) {
        return;
    }
    if (bits > SIZE_MAX) {
        fail(w, "Prepared size exceeds target range");
        return;
    }
    *value = (size_t)bits;
}
void execution_wire_bool(execution_wire *w, bool *value)
{
    uint64_t bits = w->reading ? 0 : *value;
    execution_wire_u64(w, &bits);
    if (w->error) {
        return;
    }
    if (bits > 1) {
        fail(w, "Prepared boolean is invalid");
        return;
    }
    if (w->reading) {
        *value = bits != 0;
    }
}
void execution_wire_count(execution_wire *w, int *count, size_t minimum_fields)
{
    execution_wire_int(w, count);
    if (w->error) {
        return;
    }
    /* Every encoded field takes at least one byte. */
    if (*count < 0 || (size_t)*count > EXECUTION_WIRE_MAX_RECORDS - w->records ||
        (w->reading && minimum_fields &&
         (size_t)*count > (w->size - w->offset) / minimum_fields)) {
        fail(w, "Prepared record count limit or truncated table");
        return;
    }
    w->records += (size_t)*count;
}
void execution_wire_string(execution_wire *w, const char **value)
{
    size_t bytes = 0;
    if (!w->reading && *value) {
        bytes = strnlen(*value, EXECUTION_WIRE_MAX_BYTES);
        if (bytes == EXECUTION_WIRE_MAX_BYTES) {
            fail(w, "Prepared string exceeds byte limit");
            return;
        }
        bytes++;
    }
    execution_wire_size(w, &bytes);
    if (w->error) {
        return;
    }
    if (!bytes) {
        if (w->reading) {
            *value = NULL;
        }
        return;
    }
    size_t length = bytes - 1;
    if (!available(w, length)) {
        return;
    }
    if (w->reading) {
        if (length && memchr(w->data + w->offset, 0, length)) {
            fail(w, "Prepared string contains an embedded NUL");
            return;
        }
        char *text = execution_wire_allocate(w, bytes);
        if (!text) {
            return;
        }
        if (length) {
            memcpy(text, w->data + w->offset, length);
        }
        text[length] = 0;
        w->offset += length;
        *value = text;
        return;
    }
    for (size_t i = 0; i < length; i++) {
        write_byte(w, (unsigned char)(*value)[i]);
    }
    w->size += length;
    w->offset = w->size;
}
static bool same_name(const char *a, const char *b)
{
    return !strcmp(a ? a : "", b ? b : "");
}
static bool valid_field(const type_desc *type, const type_field *field)
{
    if (field->type < PIN_BOOL || field->type > PIN_ENUM ||
        field->offset > type->size || field->size > type->size - field->offset) {
        return false;
    }
    if (field->type == PIN_STRUCT) {
        const type_desc *nested = type_catalog_find(field->type_name);
        return nested && nested->size == field->size;
    }
    if (field->type == PIN_ENUM) {
        return field->size > 0 && field->size <= sizeof(int32_t);
    }
    return field->size == behavior_scalar_size(field->type);
}
static uint64_t scalar_bits(pin_value v)
{
    if (v.type == PIN_F32) {
        uint32_t bits;
        memcpy(&bits, &v.f, sizeof bits);
        return bits;
    }
    if (v.type == PIN_F64) {
        uint64_t bits;
        memcpy(&bits, &v.d, sizeof bits);
        return bits;
    }
    return behavior_scalar_bits(v);
}
typedef struct
{
    execution_wire *wire;
    execution_wire_resolver resolve;
    void *user;
} value_codec;
static void value(value_codec *, pin_value *, unsigned);
static void tagged_value(value_codec *c, pin_value *v, unsigned depth)
{
    execution_wire *w = c->wire;
    const pin_union *source = w->reading ? NULL : v->ptr;
    bool present = source != NULL;
    execution_wire_bool(w, &present);
    if (w->error || !present) {
        return;
    }
    uint64_t tag = source ? source->tag : 0;
    int count = source ? source->count : 0;
    execution_wire_u64(w, &tag);
    execution_wire_count(w, &count, 3);
    if (w->error) {
        return;
    }
    if (!tag || (tag == PIN_UNION_CANCELLED_TAG && count)) {
        fail(w, "Prepared union tag is invalid");
        return;
    }
    pin_union *decoded = NULL;
    if (w->reading) {
        decoded = execution_wire_allocate(w, sizeof *decoded + (size_t)count * sizeof(pin_value));
        if (!decoded) {
            return;
        }
        *decoded = (pin_union){ .tag = tag, .count = count };
        memset(decoded->values, 0, (size_t)count * sizeof(pin_value));
        pin_arena_trace_union(w->storage, decoded);
        v->ptr = decoded;
    }
    for (int i = 0; i < count && !w->error; i++) {
        pin_value item = source ? source->values[i] : (pin_value){ 0 };
        value(c, &item, depth + 1);
        if (decoded) {
            decoded->values[i] = item;
        }
    }
}
static void value_into(value_codec *, pin_value *, unsigned, const pin_value *);
static void structure(value_codec *c, pin_value *v, unsigned depth, void *destination)
{
    execution_wire *w = c->wire;
    const type_desc *type = pin_value_type(v);
    if (!type || !type->size || type->field_count < 0 ||
        (type->field_count && !type->fields) || type->size > EXECUTION_WIRE_MAX_BYTES) {
        fail(w, "Prepared struct type is unavailable");
        return;
    }
    uint64_t layout = type_layout_hash(type), expected = layout;
    execution_wire_u64(w, &layout);
    if (w->error) {
        return;
    }
    if (!layout || layout != expected) {
        fail(w, "Prepared struct layout differs from linked type");
        return;
    }
    bool present = !w->reading && v->ptr;
    execution_wire_bool(w, &present);
    int fields = type->field_count;
    execution_wire_count(w, &fields, present ? 3 : 0);
    if (w->error) {
        return;
    }
    bool owned_header = c->resolve && !v->record && !present && fields == 0;
    if (fields != type->field_count && !owned_header) {
        fail(w, "Prepared struct field count differs from linked type");
        return;
    }
    if (!present) {
        if (w->reading) {
            v->ptr = NULL;
        }
        return;
    }
    if (!fields && type != &TYPE_MARKER) {
        fail(w, "Prepared struct has no reflected payload fields");
        return;
    }
    if (w->reading) {
        v->ptr = destination ? destination : execution_wire_allocate(w, type->size);
        if (!v->ptr) {
            return;
        }
        memset(v->ptr, 0, type->size);
        if (v->record && !destination) {
            pin_arena_trace_struct(w->storage, v->ptr, type);
        }
    }
    for (int i = 0; i < fields && !w->error; i++) {
        const type_field *field = &type->fields[i];
        if (!valid_field(type, field)) {
            fail(w, "Prepared reflected field exceeds linked layout");
            return;
        }
        pin_value member = { 0 };
        if (!w->reading) {
            pin_read_field(v->ptr, field, &member);
        }
        pin_value target = { .type = field->type,
                             .type_name = field->type_name,
                             .ptr = (unsigned char *)v->ptr + field->offset };
        value_into(c, &member, depth + 1, w->reading && field->type == PIN_STRUCT ? &target : NULL);
        if (w->error) {
            return;
        }
        if (member.type != field->type || member.container != PIN_CONTAINER_SINGLE ||
            !same_name(member.type_name, field->type_name) ||
            (member.type == PIN_STRUCT && !member.ptr)) {
            fail(w, "Prepared field type differs from linked type");
            return;
        }
        if (w->reading) {
            pin_write_field(v->ptr, field, member);
        }
    }
}
static void map_collection(value_codec *c, pin_value *v, unsigned depth)
{
    execution_wire *w = c->wire;
    if (v->type != PIN_UNION || v->record) {
        fail(w, "Prepared Map requires associative value storage");
        return;
    }
    pin_map *map = w->reading ? NULL : v->ptr;
    bool present = map != NULL;
    execution_wire_bool(w, &present);
    if (w->error) {
        return;
    }
    if (!present) {
        if (w->reading) {
            v->ptr = NULL;
        }
        return;
    }
    int count = w->reading ? 0 : map->count;
    if (!w->reading && count > 0 && !map->entries) {
        fail(w, "Prepared Map has no entries storage");
        return;
    }
    execution_wire_count(w, &count, 6);
    if (w->error) {
        return;
    }
    if (w->reading) {
        map = execution_wire_allocate(w, sizeof *map);
        if (!map) {
            return;
        }
        *map = (pin_map){ 0 };
        map->entries = execution_wire_allocate(w, (size_t)count * sizeof *map->entries);
        if (count && !map->entries) {
            return;
        }
        if (count) {
            memset(map->entries, 0, (size_t)count * sizeof *map->entries);
        }
        pin_map_trace_storage(w->storage, map);
        v->ptr = map;
    }
    value(c, &map->key_type, depth + 1);
    value(c, &map->value_type, depth + 1);
    if (w->error || !pin_map_key_supported(map->key_type)) {
        fail(w, "Prepared Map key type is unsupported");
        return;
    }
    for (int i = 0; i < count && !w->error; i++) {
        pin_map_entry entry = w->reading ? (pin_map_entry){ 0 } : map->entries[i];
        value(c, &entry.key, depth + 1);
        value(c, &entry.value, depth + 1);
        if (w->error) {
            return;
        }
        if (!pin_map_entry_valid(map, &entry.key, &entry.value)) {
            fail(w, "Prepared Map entry differs from its key/value types");
            return;
        }
        if (w->reading) {
            if (pin_map_find(map, &entry.key) >= 0) {
                fail(w, "Prepared Map contains duplicate keys");
                return;
            }
            map->entries[map->count++] = entry;
        }
    }
}
static void collection(value_codec *c, pin_value *v, unsigned depth)
{
    execution_wire *w = c->wire;
    pin_array *array = w->reading ? NULL : v->ptr;
    bool present = array != NULL;
    execution_wire_bool(w, &present);
    if (w->error) {
        return;
    }
    if (!present) {
        if (w->reading) {
            v->ptr = NULL;
        }
        return;
    }
    int count = w->reading ? 0 : array->count;
    execution_wire_count(w, &count, 3);
    if (w->error) {
        return;
    }
    if (w->reading) {
        array = execution_wire_allocate(w, sizeof *array);
        if (!array) {
            return;
        }
        *array = (pin_array){
            .element_type = v->type,
            .type_name = v->type_name,
            .record = v->record,
            .count = count,
            .capacity = count,
            .stride = v->type_name && !strncmp(v->type_name, "__collection_", 13)
                        ? 0
                        : pin_array_element_stride(v->type, v->type_name, v->record)
        };
        if (array->stride && (size_t)count > EXECUTION_WIRE_MAX_BYTES / array->stride) {
            fail(w, "Prepared decoded storage limit exceeded");
            return;
        }
        size_t bytes = (size_t)count *
                       (array->stride ? array->stride : sizeof(pin_value));
        void *payload = count ? execution_wire_allocate(w, bytes) : NULL;
        if (count && !payload) {
            return;
        }
        if (array->stride) {
            array->data = payload;
        } else {
            array->items = payload;
        }
        if (bytes) {
            memset(payload, 0, bytes);
        }
        pin_arena_trace_array(w->storage, array);
        v->ptr = array;
    } else if (
        array->element_type != v->type ||
        !same_name(array->type_name, v->type_name) || array->capacity < count ||
        !pin_array_valid(array) || !pin_record_same(array->record, v->record)
    ) {
        fail(w, "Prepared collection schema differs from its value");
        return;
    }
    pin_value element_shape = { 0 };
    bool descriptor = !array->stride && !v->record && v->type_name &&
                      !strncmp(v->type_name, "__collection_", 13);
    for (int i = 0; i < count && !w->error; i++) {
        pin_value item = w->reading ? (pin_value){ 0 } : pin_array_get(array, i);
        pin_value target = { .type = v->type,
                             .type_name = v->type_name,
                             .record = v->record,
                             .ptr = array->stride && v->type == PIN_STRUCT
                                      ? pin_array_data(array, i)
                                      : NULL };
        value_into(c, &item, depth + 1, w->reading && target.ptr ? &target : NULL);
        if (w->error) {
            return;
        }
        if (w->reading && array->stride && item.type == PIN_STRUCT && !item.ptr) {
            pin_value *items = execution_wire_allocate(w, (size_t)count * sizeof *items);
            if (!items) {
                return;
            }
            /* Existing inline values keep pointing into the raw allocation;
             * boxed storage is needed only to retain the null element. */
            for (int j = 0; j < count; j++) {
                items[j] = pin_array_get(array, j);
            }
            array->items = items;
            array->data = NULL;
            array->stride = 0;
        }
        bool nested = !array->stride && item.container != PIN_CONTAINER_SINGLE;
        bool nested_descriptor = nested && !v->record && v->type_name &&
                                 !strncmp(v->type_name, "__collection_", 13);
        if (descriptor && i &&
            (item.container != element_shape.container ||
             !same_name(item.type_name, element_shape.type_name) ||
             !pin_record_same(item.record, element_shape.record))) {
            fail(w, "Prepared nested collection headers differ");
            return;
        }
        if (descriptor && !i) {
            element_shape = item;
        }
        if (item.type != v->type ||
            (!nested && item.container != PIN_CONTAINER_SINGLE) ||
            (!nested && !same_name(item.type_name, v->type_name)) ||
            (!nested_descriptor && !pin_record_same(item.record, v->record)) ||
            (w->reading && pin_array_set(array, i, &item))) {
            fail(w, "Prepared collection item differs from declared type");
            return;
        }
    }
}
static void value_into(value_codec *c, pin_value *v, unsigned depth, const pin_value *destination)
{
    execution_wire *w = c->wire;
    if (!w->reading && v->record && !c->resolve) {
        fail(w, "Authored record requires a prepared layout table");
    }
    if (!w->reading && (v->type < PIN_FLOW || v->type > PIN_UNION ||
                        v->container < PIN_CONTAINER_SINGLE ||
                        v->container > PIN_CONTAINER_MAP ||
                        (v->type == PIN_FLOW && v->container))) {
        fail(w, "Prepared value tag is invalid");
    }
    bool authored = !w->reading && v->record;
    if (!w->reading && c->resolve) {
        c->resolve(w, c->user, v, authored);
    }
    if (w->error) {
        return;
    }
    if (depth >= EXECUTION_WIRE_MAX_DEPTH || ++w->records > EXECUTION_WIRE_MAX_RECORDS) {
        fail(w, "Prepared value nesting or record limit exceeded");
        return;
    }
    int tag = w->reading ? 0 : v->type;
    int container = w->reading ? 0 : v->container;
    execution_wire_int(w, &tag);
    execution_wire_int(w, &container);
    if (w->error) {
        return;
    }
    if (tag < PIN_FLOW || tag > PIN_UNION || container < PIN_CONTAINER_SINGLE ||
        container > PIN_CONTAINER_MAP || (tag == PIN_FLOW && container)) {
        fail(w, "Prepared value tag is invalid");
        return;
    }
    if (w->reading) {
        *v = (pin_value){ .type = tag, .container = container };
    }
    execution_wire_string(w, &v->type_name);
    if (w->error) {
        return;
    }
    if (c->resolve) {
        execution_wire_bool(w, &authored);
        if (w->error) {
            return;
        }
        if (authored && v->type != PIN_STRUCT) {
            fail(w, "Prepared authored flag requires a struct");
            return;
        }
        if (w->reading) {
            v->record = c->resolve(w, c->user, v, authored);
            if (w->error) {
                return;
            }
            if (authored && !v->record) {
                fail(w, "Prepared authored layout is unavailable");
                return;
            }
            if (v->record) {
                v->type_name = v->record->type.name;
            }
        }
    }
    if (v->type == PIN_ENUM && v->type_name && !type_catalog_enum_find(v->type_name)) {
        fail(w, "Prepared enum type is unavailable");
        return;
    }
    if (destination && (v->type != destination->type || v->container != PIN_CONTAINER_SINGLE ||
                        !same_name(v->type_name, destination->type_name) ||
                        !pin_record_same(v->record, destination->record))) {
        fail(w, "Prepared inline value differs from its destination type");
        return;
    }
    if (container == PIN_CONTAINER_MAP) {
        map_collection(c, v, depth);
        return;
    }
    if (container != PIN_CONTAINER_SINGLE) {
        collection(c, v, depth);
        return;
    }
    if (tag == PIN_STRUCT) {
        structure(c, v, depth, destination ? destination->ptr : NULL);
        return;
    }
    if (tag == PIN_UNION) {
        tagged_value(c, v, depth);
        return;
    }
    if (tag == PIN_STR || tag == PIN_NAME) {
        execution_wire_string(w, &v->str);
        return;
    }
    uint64_t bits = w->reading || tag == PIN_FLOW ? 0 : scalar_bits(*v);
    execution_wire_u64(w, &bits);
    if (w->error) {
        return;
    }
    if ((tag == PIN_HANDLE || tag == PIN_TASK || tag == PIN_FLOW) && bits) {
        fail(w, "Prepared handles and flow values must be zero");
        return;
    }
    if (tag == PIN_BOOL && bits > 1) {
        fail(w, "Prepared boolean value is invalid");
        return;
    }
    if (tag == PIN_FLOW) {
        return;
    }
    pin_value decoded = { .type = tag };
    if (tag >= PIN_I8 && tag <= PIN_U64) {
        decoded = behavior_scalar_from_bits(tag, bits);
    } else {
        switch (tag) {
            case PIN_BOOL: decoded.b = bits != 0; break;
            case PIN_F32: {
                uint32_t narrow = (uint32_t)bits;
                memcpy(&decoded.f, &narrow, sizeof narrow);
                break;
            }
            case PIN_F64: memcpy(&decoded.d, &bits, sizeof bits); break;
            case PIN_ENUM: {
                uint32_t narrow = (uint32_t)bits;
                memcpy(&decoded.e, &narrow, sizeof narrow);
                break;
            }
            case PIN_HANDLE:
            case PIN_TASK: decoded.u64 = 0; break;
            default: fail(w, "Prepared scalar type is invalid"); return;
        }
    }
    if (scalar_bits(decoded) != bits) {
        fail(w, "Prepared scalar exceeds declared width");
        return;
    }
    if (w->reading) {
        decoded.type_name = v->type_name;
        *v = decoded;
    }
}
static void value(value_codec *c, pin_value *v, unsigned depth)
{
    value_into(c, v, depth, NULL);
}
void execution_wire_value(execution_wire *w, pin_value *v)
{
    value_codec c = { .wire = w };
    value(&c, v, 0);
}
void execution_wire_value_owned(execution_wire *w, pin_value *v, execution_wire_resolver resolve, void *user)
{
    if (!resolve) {
        fail(w, "Prepared layout resolver is absent");
        return;
    }
    value_codec c = { .wire = w, .resolve = resolve, .user = user };
    value(&c, v, 0);
}

bool execution_wire_reserve(execution_wire *w, size_t bytes)
{
    if (w->error) {
        return false;
    }
    if (w->owned_bytes > EXECUTION_WIRE_MAX_BYTES ||
        bytes > EXECUTION_WIRE_MAX_BYTES - w->owned_bytes) {
        w->error = "Prepared shader decoded storage limit";
        return false;
    }
    if (w->storage_budget) {
        if (bytes > *w->storage_budget) {
            fail(w, "Prepared aggregate storage budget exceeded");
            return false;
        }
        *w->storage_budget -= bytes;
        w->storage_charged += bytes;
    }
    w->owned_bytes += bytes;
    return true;
}
void *execution_wire_allocate_owned(execution_wire *w, size_t bytes)
{
    return execution_wire_reserve(w, bytes) ? xcalloc(1, bytes) : NULL;
}
void execution_wire_float(execution_wire *w, float *value)
{
    uint32_t bits = 0;
    if (!w->reading) {
        memcpy(&bits, value, sizeof bits);
    }
    uint64_t encoded = bits;
    execution_wire_u64(w, &encoded);
    if (w->error || !w->reading) {
        return;
    }
    if (encoded > UINT32_MAX) {
        w->error = "Invalid prepared F32 bits";
        return;
    }
    bits = (uint32_t)encoded;
    memcpy(value, &bits, sizeof bits);
}
void execution_wire_owned_string(execution_wire *w, char **value)
{
    const char *text = w->reading ? NULL : *value;
    execution_wire_string(w, &text);
    if (w->error || !w->reading || !text) {
        return;
    }
    size_t length = strlen(text) + 1;
    char *owned = execution_wire_allocate_owned(w, length);
    if (owned) {
        memcpy(owned, text, length);
        *value = owned;
    }
}
void execution_wire_blob(execution_wire *w, const unsigned char **bytes, size_t *length)
{
    execution_wire_size(w, length);
    if (w->error || !available(w, *length)) {
        return;
    }
    if (w->reading) {
        *bytes = w->data + w->offset;
        w->offset += *length;
        return;
    }
    if (*length && !*bytes) {
        fail(w, "Prepared byte payload is absent");
        return;
    }
    for (size_t i = 0; i < *length; i++) {
        write_byte(w, (*bytes)[i]);
    }
    w->size += *length;
    w->offset = w->size;
}
