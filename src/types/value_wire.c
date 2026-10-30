#include "types/value_wire.h"

#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/map.h"
#include "types/record.h"
#include "types/scalar.h"
#include "types/schema.h"
#include "types/text.h"
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
static size_t owned_limit(const execution_wire *w)
{
    return w->owned_limit ? w->owned_limit : EXECUTION_WIRE_MAX_BYTES;
}
static size_t record_limit(const execution_wire *w)
{
    return w->record_limit ? w->record_limit : EXECUTION_WIRE_MAX_RECORDS;
}
void *execution_wire_allocate(execution_wire *w, size_t bytes)
{
    if (w->error) {
        return NULL;
    }
    if (!w->storage || w->owned_bytes > owned_limit(w) ||
        bytes > owned_limit(w) - w->owned_bytes) {
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
    if (*count < 0 || w->records > record_limit(w) ||
        (size_t)*count > record_limit(w) - w->records ||
        (w->reading && minimum_fields &&
         (size_t)*count > (w->size - w->offset) / minimum_fields)) {
        fail(w, "Prepared record count limit or truncated table");
        return;
    }
    w->records += (size_t)*count;
}
bool execution_wire_record(execution_wire *w)
{
    if (w->records >= record_limit(w)) {
        fail(w, "Prepared value nesting or record limit exceeded");
        return false;
    }
    w->records++;
    return true;
}
enum
{
    STRING_TABLE_OFFSET_BYTES = 8
};
static size_t text_slot(const char *text, size_t mask)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (; *text; text++) {
        hash = (hash ^ (unsigned char)*text) * UINT64_C(1099511628211);
    }
    return (size_t)hash & mask;
}
/* The scratch a writer's table holds: its string vector and its slots. */
static size_t table_scratch(size_t slots)
{
    return 2 * sizeof(dyna_hdr) + slots / 2 * sizeof(const char *) +
           slots * sizeof(int);
}
static size_t strings_scratch(const execution_wire *w)
{
    return w->slots ? table_scratch((size_t)acap(w->slots)) : 0;
}
static void release(execution_wire *w, size_t bytes)
{
    if (w->storage_budget) {
        *w->storage_budget += bytes;
        w->storage_charged -= bytes;
    }
    w->owned_bytes -= bytes;
}
/* The slot holding `text`, or the empty slot where it goes. */
static size_t probe(const int *slots, size_t mask, const char *const *strings, const char *text)
{
    size_t at = text_slot(text, mask);
    while (slots[at] && strcmp(strings[slots[at] - 1], text)) {
        at = (at + 1) & mask;
    }
    return at;
}
/* Doubles the slots once they are half full, keeping probes short. The
 * vector holds half as many strings, so it grows only here. The old and new
 * tables are both live while rehashing and while the vector moves, so both
 * stay charged until the old one is freed. */
static bool grow_slots(execution_wire *w)
{
    size_t before = strings_scratch(w);
    size_t size = acap(w->slots) ? (size_t)acap(w->slots) * 2 : 64;
    if (size > INT_MAX || !execution_wire_reserve(w, table_scratch(size))) {
        return false;
    }
    int *slots = NULL;
    asetlen(slots, (int)size);
    memset(slots, 0, size * sizeof *slots);
    for (int i = 0; i < acount(w->strings); i++) {
        slots[probe(slots, size - 1, w->strings, w->strings[i])] = i + 1;
    }
    afree(w->slots);
    w->slots = slots;
    asetcap(w->strings, (int)(size / 2));
    assert(acap(w->slots) == (int)size && acap(w->strings) == (int)(size / 2));
    release(w, before);
    return true;
}
static size_t string_code(execution_wire *w, const char *text)
{
    if ((size_t)acount(w->strings) * 2 >= (size_t)acap(w->slots) && !grow_slots(w)) {
        return 0;
    }
    size_t at = probe(w->slots, (size_t)acap(w->slots) - 1, w->strings, text);
    if (!w->slots[at]) {
        apush(w->strings, text);
        w->slots[at] = acount(w->strings);
    }
    return (size_t)w->slots[at];
}
static void interned_string(execution_wire *w, const char **value)
{
    size_t code = 0;
    if (!w->reading && *value && !(code = string_code(w, *value))) {
        return;
    }
    execution_wire_size(w, &code);
    if (!w->reading || w->error) {
        return;
    }
    /* Entries are named in table order, each first named after the last. */
    if (code > w->strings_named + 1 || code > w->string_count) {
        fail(w, "Prepared string index is out of order or exceeds its table");
        return;
    }
    if (code == w->strings_named + 1) {
        w->strings_named++;
    }
    *value = code ? w->strings[code - 1] : NULL;
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
    if (w->interning) {
        interned_string(w, value);
        return;
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
/* Writers store each string once, so a table that repeats one would decode
 * to bytes no writer produces. Fails only on a repeat; a reservation failure
 * sets the wire's error instead. */
static bool distinct(execution_wire *w, const char **table, int count)
{
    size_t size = 64;
    while (size < 2 * (size_t)count) {
        size *= 2;
    }
    if (!execution_wire_reserve(w, size * sizeof(int))) {
        return true;
    }
    int *slots = xcalloc(size, sizeof *slots);
    bool unique = true;
    for (int i = 0; i < count && unique; i++) {
        size_t at = probe(slots, size - 1, table, table[i]);
        unique = !slots[at];
        slots[at] = i + 1;
    }
    free(slots);
    release(w, size * sizeof(int));
    return unique;
}
/* Reads the table between the offset the last 8 bytes give and those bytes,
 * then limits the wire to the bytes before it. */
static void read_strings(execution_wire *w)
{
    if (w->error || w->offset > w->size ||
        w->size - w->offset < STRING_TABLE_OFFSET_BYTES) {
        fail(w, "Prepared string table is absent");
        return;
    }
    size_t end = w->size - STRING_TABLE_OFFSET_BYTES;
    uint64_t at = 0;
    for (int i = 0; i < STRING_TABLE_OFFSET_BYTES; i++) {
        at |= (uint64_t)w->data[end + i] << (8 * i);
    }
    if (at < w->offset || at > end) {
        fail(w, "Prepared string table offset is invalid");
        return;
    }
    size_t body = w->offset;
    w->offset = (size_t)at;
    w->size = end;
    int count = 0;
    execution_wire_count(w, &count, 1);
    const char **table = NULL;
    if (count && !w->error) {
        table = execution_wire_allocate(w, (size_t)count * sizeof *table);
    }
    for (int i = 0; i < count && !w->error; i++) {
        execution_wire_string(w, &table[i]);
        if (!w->error && !table[i]) {
            fail(w, "Prepared string table entry is absent");
        }
    }
    if (!w->error && w->offset != end) {
        fail(w, "Prepared string table has trailing bytes");
    }
    if (!w->error && !distinct(w, table, count)) {
        fail(w, "Prepared string table repeats an entry");
    }
    w->strings = table;
    w->string_count = w->error ? 0 : (size_t)count;
    w->offset = body;
    w->size = (size_t)at;
}
void execution_wire_strings_begin(execution_wire *w)
{
    assert(!w->interning);
    if (w->reading) {
        read_strings(w);
    }
    w->interning = true;
}
static void write_strings(execution_wire *w)
{
    uint64_t at = w->size;
    int count = acount(w->strings);
    execution_wire_count(w, &count, 1);
    for (int i = 0; i < count; i++) {
        execution_wire_string(w, &w->strings[i]);
    }
    if (!available(w, STRING_TABLE_OFFSET_BYTES)) {
        return;
    }
    for (int i = 0; i < STRING_TABLE_OFFSET_BYTES; i++) {
        write_byte(w, (unsigned char)(at >> (8 * i)));
    }
    w->size += STRING_TABLE_OFFSET_BYTES;
    w->offset = w->size;
}
void execution_wire_strings_finish(execution_wire *w)
{
    assert(w->interning);
    w->interning = false;
    if (w->reading) {
        if (!w->error && w->strings_named != w->string_count) {
            fail(w, "Prepared string table has an unnamed entry");
        }
        return;
    }
    if (!w->error) {
        write_strings(w);
    }
    release(w, strings_scratch(w));
    afree(w->strings);
    afree(w->slots);
}
/* Language text has an explicit length; metadata uses wire_string above. */
static void text_value(execution_wire *w, pin_value *value)
{
    pin_string text = w->reading ? (pin_string){ 0 } : pin_text(*value);
    const unsigned char *bytes = (const unsigned char *)text.bytes;
    size_t length = text.length;
    execution_wire_blob(w, &bytes, &length);
    if (w->error || !w->reading) {
        return;
    }
    if (length > UINT32_MAX || length > SIZE_MAX - sizeof(pin_string) - 1) {
        fail(w, "Prepared text exceeds byte limit");
        return;
    }
    pin_string *owned = execution_wire_allocate(w, sizeof *owned + length + 1);
    if (!owned) {
        return;
    }
    char *payload = (char *)(owned + 1);
    memcpy(payload, bytes, length);
    payload[length] = 0;
    *owned = (pin_string){ .bytes = payload, .length = (uint32_t)length };
    value->text = owned;
}
static bool same_name(const char *a, const char *b)
{
    return !strcmp(a ? a : "", b ? b : "");
}
static bool valid_field(const type_desc *type, const type_field *field, type_resolver resolver)
{
    if (!((field->type >= PIN_BOOL && field->type <= PIN_ENUM) ||
          field->type == PIN_POINTER || field->type == PIN_QUERY) ||
        field->offset > type->size || field->size > type->size - field->offset) {
        return false;
    }
    if (field->container == PIN_CONTAINER_ARRAY) {
        return field->size == sizeof(pin_array *) &&
               (field->type != PIN_STRUCT || type_field_type(field, resolver));
    }
    if (field->container != PIN_CONTAINER_SINGLE) {
        return false;
    }
    if (field->type == PIN_STRUCT) {
        const type_desc *nested = type_field_type(field, resolver);
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
    type_resolver resolver = v->schema ? pin_schema_resolver(v->schema)
                                       : (type_resolver){ 0 };
    /* An authored record's descriptor is immutable and hashed once, when it
     * is created or validated; rehashing it for each header would visit its
     * whole nested layout again. */
    bool own_layout = v->record && !v->record->native && type == &v->record->type;
    uint64_t layout = v->schema  ? pin_schema_layout_type(v->schema, type)
                    : own_layout ? v->record->layout
                                 : type_layout_hash(type);
    uint64_t expected = layout;
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
        if (!valid_field(type, field, resolver)) {
            fail(w, "Prepared reflected field exceeds linked layout");
            return;
        }
        pin_value member = { 0 };
        pin_string borrowed = { 0 };
        if (!w->reading && field->container == PIN_CONTAINER_SINGLE &&
            field->type == PIN_STR && !field->text_descriptor) {
            const char *bytes = *(const char *const *)((const char *)v->ptr +
                                                       field->offset);
            bytes = bytes ? bytes : "";
            size_t limit = EXECUTION_WIRE_MAX_BYTES - w->size;
            size_t length = strnlen(bytes, limit);
            if (length == limit) {
                fail(w, "Prepared text exceeds remaining byte limit");
                return;
            }
            borrowed = (pin_string){ .bytes = bytes, .length = (uint32_t)length };
            member = (pin_value){ .type = PIN_STR,
                                  .type_name = field->type_name,
                                  .text = &borrowed };
        } else if (!w->reading) {
            /* All remaining reads borrow fields; no native C text is copied
             * before its encoded payload has passed the writer byte bound. */
            const char *error = pin_read_field(NULL, v->ptr, field, &member);
            if (error) {
                fail(w, error);
                return;
            }
            if (member.type == PIN_STRUCT) {
                member.schema = v->schema;
            }
        }
        pin_value target = { .type = field->type,
                             .type_name = field->type_name,
                             .record = field->layout ? field->layout->carrier : NULL,
                             .ptr = (unsigned char *)v->ptr + field->offset };
        value_into(
            c,
            &member,
            depth + 1,
            w->reading && field->type == PIN_STRUCT && field->container == PIN_CONTAINER_SINGLE
                ? &target
                : NULL
        );
        if (w->error) {
            return;
        }
        if (member.type != field->type || member.container != field->container ||
            !same_name(member.type_name, field->type_name) ||
            (member.type == PIN_STRUCT &&
             member.container == PIN_CONTAINER_SINGLE && !member.ptr)) {
            fail(w, "Prepared field type differs from linked type");
            return;
        }
        if (w->reading && field->container == PIN_CONTAINER_ARRAY) {
            memcpy((char *)v->ptr + field->offset, &member.ptr, sizeof member.ptr);
        } else if (w->reading && field->type == PIN_STR) {
            void *destination = (unsigned char *)v->ptr + field->offset;
            if (field->text_descriptor) {
                *(const pin_string **)destination = member.text;
            } else {
                const char *bytes;
                const char *error = pin_text_cstring(w->storage, member, &bytes, NULL);
                if (error) {
                    fail(w, error);
                    return;
                }
                *(const char **)destination = bytes;
            }
        } else if (w->reading && field->type != PIN_STRUCT) {
            const char *error = pin_write_field(w->storage, v->ptr, field, member);
            if (error) {
                fail(w, error);
                return;
            }
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
        *map = (pin_map){ .capacity = count };
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
    if (w->reading && !w->error) {
        size_t bytes = pin_map_index_bytes(map->count);
        pin_map_slot *slots = bytes ? execution_wire_allocate(w, bytes) : NULL;
        if (bytes && !slots) {
            return;
        }
        pin_map_index(map, slots);
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
    if (!w->reading && v->container == PIN_CONTAINER_VIEW && v->ptr) {
        fail(w, "Array views cannot be persisted");
        return;
    }
    /* A header without a payload persists only its type, which the reader
     * links by name and layout; a payload needs its native layout current,
     * unless a schema carries the native layouts the catalog may retire. */
    if (!w->reading && v->ptr && v->record && v->record->native && !v->schema &&
        !pin_record_native_current(v->record)) {
        fail(w, "Native record type is retired or its layout is incompatible");
        return;
    }
    if (!w->reading && v->record && !v->record->native && !c->resolve) {
        fail(w, "Authored record requires a prepared layout table");
    }
    if (!w->reading && (v->type < PIN_FLOW || v->type > PIN_QUERY ||
                        v->container < PIN_CONTAINER_SINGLE ||
                        v->container > PIN_CONTAINER_VIEW ||
                        (v->type == PIN_FLOW && v->container))) {
        fail(w, "Prepared value tag is invalid");
    }
    bool authored = !w->reading && v->record && !v->record->native;
    if (!w->reading && c->resolve) {
        /* The owner validates authored layouts and native freshness (or a
         * retained schema), with its own bounded per-write proof reuse. */
        c->resolve(w, c->user, v, authored);
    }
    if (w->error) {
        return;
    }
    if (depth >= EXECUTION_WIRE_MAX_DEPTH) {
        fail(w, "Prepared value nesting or record limit exceeded");
        return;
    }
    if (!execution_wire_record(w)) {
        return;
    }
    int tag = w->reading ? 0 : v->type;
    int container = w->reading ? 0 : v->container;
    execution_wire_int(w, &tag);
    execution_wire_int(w, &container);
    if (w->error) {
        return;
    }
    if (tag < PIN_FLOW || tag > PIN_QUERY || container < PIN_CONTAINER_SINGLE ||
        container > PIN_CONTAINER_VIEW || (tag == PIN_FLOW && container)) {
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
                        !pin_value_layout_same(v, destination))) {
        fail(w, "Prepared inline value differs from its destination type");
        return;
    }
    if (container == PIN_CONTAINER_VIEW) {
        bool present = false;
        execution_wire_bool(w, &present);
        if (!w->error && present) {
            fail(w, "Array views cannot be persisted");
        }
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
    if (tag == PIN_STR) {
        text_value(w, v);
        return;
    }
    if (tag == PIN_NAME) {
        execution_wire_string(w, &v->str);
        return;
    }
    uint64_t bits = w->reading || tag == PIN_FLOW ? 0 : scalar_bits(*v);
    execution_wire_u64(w, &bits);
    if (w->error) {
        return;
    }
    if ((tag == PIN_POINTER || tag == PIN_QUERY) && bits) {
        fail(w, tag == PIN_QUERY ? "Queries cannot be persisted" : "Managed pointers cannot be persisted");
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
            case PIN_QUERY:
            case PIN_POINTER: decoded.ptr = NULL; break;
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
    pin_value decoded = w->reading ? (pin_value){ 0 } : *v;
    value(&c, &decoded, 0);
    if (!w->error) {
        *v = decoded;
    }
}
void execution_wire_value_owned(execution_wire *w, pin_value *v, execution_wire_resolver resolve, void *user)
{
    if (!resolve) {
        fail(w, "Prepared layout resolver is absent");
        return;
    }
    value_codec c = { .wire = w, .resolve = resolve, .user = user };
    pin_value decoded = w->reading ? (pin_value){ 0 } : *v;
    value(&c, &decoded, 0);
    if (!w->error) {
        *v = decoded;
    }
}

bool execution_wire_reserve(execution_wire *w, size_t bytes)
{
    if (w->error) {
        return false;
    }
    if (w->owned_bytes > owned_limit(w) || bytes > owned_limit(w) - w->owned_bytes) {
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
