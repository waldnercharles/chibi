#include "types/type.h"

#include "support/arena.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/record.h"
#include "types/text.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const type_desc TYPE_MARKER = { .name = "marker", .size = 1 };

const type_desc *type_resolve(type_resolver resolver, const char *name)
{
    if (!name) {
        return NULL;
    }
    return resolver.find ? resolver.find(resolver.user, name)
                         : type_catalog_find(name);
}

const type_desc *type_field_type(const type_field *field, type_resolver resolver)
{
    return field->layout ? field->layout : type_resolve(resolver, field->type_name);
}
const type_desc *type_alias_type(const type_desc *type, type_resolver resolver)
{
    return !type || !type->alias_of ? type
         : type->alias              ? type->alias
                                    : type_resolve(resolver, type->alias_of);
}
bool type_alias_layout(const type_desc *type)
{
    return !type->field_count && (type->alias_of || type->alias);
}

enum
{
    PIN_FIELD_PATH_MAX_DEPTH = 16,
};

static const type_field *type_field_segment(const type_desc *type, const char *name, size_t length)
{
    for (int depth = 0; type && type->alias_of && depth < TYPE_MAX_NESTING; depth++) {
        type = type_alias_type(type, (type_resolver){ 0 });
    }
    if (!type || !name || !length) {
        return NULL;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->name && strlen(field->name) == length &&
            !strncmp(field->name, name, length)) {
            return field;
        }
    }
    return NULL;
}

const type_field *pin_type_field_path(const type_desc *type, const char *path)
{
    const char *at = path;
    for (int depth = 0; type && at && *at && depth < PIN_FIELD_PATH_MAX_DEPTH; depth++) {
        const char *end = strchr(at, '.');
        size_t length = end ? (size_t)(end - at) : strlen(at);
        const type_field *field = type_field_segment(type, at, length);
        if (!field) {
            return NULL;
        }
        if (!end) {
            return field;
        }
        if (field->container != PIN_CONTAINER_SINGLE ||
            field->type != PIN_STRUCT || !field->type_name) {
            return NULL;
        }
        type = type_field_type(field, (type_resolver){ 0 });
        at = end + 1;
    }
    return NULL;
}

int pin_read_field_path(
    arena *storage,
    const void *base,
    const type_desc *type,
    const char *path,
    pin_value *out
)
{
    if (!out) {
        return -1;
    }
    const char *at = path;
    const void *current = base;
    for (int depth = 0; current && type && at && *at && depth < PIN_FIELD_PATH_MAX_DEPTH;
         depth++) {
        const char *end = strchr(at, '.');
        size_t length = end ? (size_t)(end - at) : strlen(at);
        const type_field *field = type_field_segment(type, at, length);
        if (!field) {
            return -1;
        }
        pin_value value;
        if (pin_read_field(storage, current, field, &value)) {
            return -1;
        }
        if (!end) {
            if (out) {
                *out = value;
            }
            return out ? 0 : -1;
        }
        if (value.type != PIN_STRUCT || value.container != PIN_CONTAINER_SINGLE ||
            !value.ptr || !value.type_name) {
            return -1;
        }
        current = value.ptr;
        type = pin_value_type(&value);
        at = end + 1;
    }
    return -1;
}

const char *type_field_type_name(const type_field *f)
{
    bool scalar = f->type >= PIN_BOOL && f->type <= PIN_F64;
    return f->type_name && !scalar ? f->type_name : pin_type_name(f->type);
}

bool type_field_range_valid(const type_field *f)
{
    if (!f->range_contract) {
        return true;
    }
    if (!isfinite(f->min) || !isfinite(f->max) || !(f->min < f->max) ||
        !isfinite(f->range_default)) {
        return false;
    }
    if (f->type == PIN_F64) {
        return true;
    }
    if (f->type == PIN_F32) {
        if (f->min < -FLT_MAX || f->max > FLT_MAX) {
            return false;
        }
        float low = (float)f->min, high = (float)f->max;
        low = (double)low < f->min ? nextafterf(low, INFINITY) : low;
        high = (double)high > f->max ? nextafterf(high, -INFINITY) : high;
        return low <= high;
    }
    if (trunc(f->min) != f->min || trunc(f->max) != f->max) {
        return false;
    }
    switch (f->type) {
        case PIN_I8: return f->min >= INT8_MIN && f->max <= INT8_MAX;
        case PIN_U8: return f->min >= 0 && f->max <= UINT8_MAX;
        case PIN_I16: return f->min >= INT16_MIN && f->max <= INT16_MAX;
        case PIN_U16: return f->min >= 0 && f->max <= UINT16_MAX;
        case PIN_I32: return f->min >= INT32_MIN && f->max <= INT32_MAX;
        case PIN_U32: return f->min >= 0 && f->max <= UINT32_MAX;
        case PIN_I64: return f->min >= -0x1p63 && f->max < 0x1p63;
        case PIN_U64: return f->min >= 0 && f->max < 0x1p64;
        default: return false;
    }
}

pin_value pin_clamp_field(const type_field *f, pin_value v)
{
    if (!f->range_contract) {
        return v;
    }
    assert(type_field_range_valid(f));
    assert(v.type == f->type && v.container == PIN_CONTAINER_SINGLE);
    if (f->type == PIN_F32 || f->type == PIN_F64) {
        double number = f->type == PIN_F32 ? v.f : v.d;
        if (isnan(number)) {
#ifndef NDEBUG
            fprintf(
                stderr,
                "Range contract: NaN in %s; using validated default\n",
                f->name ? f->name : "numeric field"
            );
#endif
            number = f->range_default;
        }
        number = number < f->min ? f->min : number > f->max ? f->max : number;
        if (f->type == PIN_F64) {
            v.d = number;
            return v;
        }
        v.f = (float)number;
        /* A decimal endpoint may lie between adjacent floats. Keep the
         * represented value inside the declared interval. */
        if ((double)v.f < f->min) {
            v.f = nextafterf(v.f, INFINITY);
        }
        if ((double)v.f > f->max) {
            v.f = nextafterf(v.f, -INFINITY);
        }
        return v;
    }
    /* Compare in the storage type. Validated integral bounds make every cast
     * defined, including on targets where long double is only double. */
#define CLAMP(PIN, TYPE, MEMBER)                                               \
    case PIN: {                                                                \
        TYPE low = (TYPE)f->min, high = (TYPE)f->max;                          \
        v.MEMBER = v.MEMBER < low ? low : v.MEMBER > high ? high : v.MEMBER;   \
        break;                                                                 \
    }
    switch (f->type) {
        CLAMP(PIN_I8, int8_t, i8)
        CLAMP(PIN_U8, uint8_t, u8)
        CLAMP(PIN_I16, int16_t, i16)
        CLAMP(PIN_U16, uint16_t, u16)
        CLAMP(PIN_I32, int32_t, i)
        CLAMP(PIN_U32, uint32_t, u)
        CLAMP(PIN_I64, int64_t, i64)
        CLAMP(PIN_U64, uint64_t, u64)
        default: assert(false); break;
    }
#undef CLAMP
    return v;
}

pin_value pin_field_number(const type_field *f, double number)
{
    assert(f->range_contract && type_field_range_valid(f));
    type_field wide = *f;
    wide.type = PIN_F64;
    number = pin_clamp_field(&wide, (pin_value){ .type = PIN_F64, .d = number }).d;
    pin_value value = { .type = f->type, .type_name = f->type_name };
    switch (f->type) {
        case PIN_I8: value.i8 = (int8_t)number; break;
        case PIN_U8: value.u8 = (uint8_t)number; break;
        case PIN_I16: value.i16 = (int16_t)number; break;
        case PIN_U16: value.u16 = (uint16_t)number; break;
        case PIN_I32: value.i = (int32_t)number; break;
        case PIN_U32: value.u = (uint32_t)number; break;
        case PIN_I64: value.i64 = (int64_t)number; break;
        case PIN_U64: value.u64 = (uint64_t)number; break;
        case PIN_F32: value.f = (float)number; break;
        case PIN_F64: value.d = number; break;
        default: assert(false); break;
    }
    return pin_clamp_field(f, value);
}

static void validate_ranges(const type_desc *type, void *data, int depth, type_resolver resolver)
{
    assert(depth < TYPE_MAX_NESTING);
    for (int i = 0; i < type->field_count; i++) {
        const type_field *f = &type->fields[i];
        if (f->container != PIN_CONTAINER_SINGLE) {
            continue;
        }
        if (f->type == PIN_STRUCT) {
            const type_desc *nested = type_field_type(f, resolver);
            if (nested && nested->size == f->size) {
                validate_ranges(nested, (char *)data + f->offset, depth + 1, resolver);
            }
        } else if (f->range_contract) {
            pin_value value;
            const char *error = pin_read_field(NULL, data, f, &value);
            assert(!error);
            error = pin_write_field(NULL, data, f, value);
            assert(!error);
            (void)error;
        }
    }
}

void type_validate_ranges(const type_desc *type, void *data)
{
    type_validate_ranges_resolved(type, data, (type_resolver){ 0 });
}

void type_validate_ranges_resolved(const type_desc *type, void *data, type_resolver resolver)
{
    validate_ranges(type, data, 0, resolver);
}

/* Only the string allocations made by a failed staged field write are owned.
 * Names and other native pointers retain their existing borrowed contracts. */
static void free_field_text(const type_desc *type, void *data, int depth, type_resolver resolver)
{
    assert(depth < TYPE_MAX_NESTING);
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        void *member = (char *)data + field->offset;
        if (field->container != PIN_CONTAINER_SINGLE) {
            continue;
        }
        if (field->type == PIN_STR) {
            if (field->text_descriptor) {
                pin_text_destroy(*(const pin_string **)member);
            } else {
                free(*(void **)member);
            }
        } else if (field->type == PIN_STRUCT) {
            const type_desc *nested = type_field_type(field, resolver);
            if (nested) {
                free_field_text(nested, member, depth + 1, resolver);
            }
        }
    }
}
const char *pin_write_struct_resolved(
    arena *storage,
    void *destination,
    const type_desc *target,
    pin_value value,
    type_resolver resolver
)
{
    if (resolver.find && storage && storage->collectable) {
        return "Custom type resolution requires ordinary storage";
    }
    if (!target || !destination || !value.ptr || value.type != PIN_STRUCT ||
        value.container != PIN_CONTAINER_SINGLE || !target->size ||
        target->field_count < 0 || (target->field_count && !target->fields) ||
        (value.record && (value.schema ? !pin_value_record_current(&value)
                                       : !pin_record_valid(value.record) ||
                                             !pin_record_current(value.record)))) {
        return "Field record layout is unavailable";
    }
    const type_desc *source = value.record || value.schema
                                ? pin_value_type(&value)
                                : type_resolve(resolver, value.type_name);
    if (!source && !value.record && value.type_name && target->name &&
        !strcmp(value.type_name, target->name)) {
        source = target;
    }
    if (!source || target->field_count != source->field_count ||
        (source->field_count && !source->fields)) {
        return "Field record layout differs";
    }
    if (!target->field_count) {
        if ((value.record && !value.record->native) ||
            source->size != target->size || !value.type_name || !target->name ||
            strcmp(value.type_name, target->name)) {
            return "Field record layout differs";
        }
        memcpy(destination, value.ptr, target->size);
        return NULL;
    }
    void *copy = xcalloc(1, target->size);
    arena temporary = { 0 };
    const char *error = NULL;
    for (int i = 0; i < target->field_count && !error; i++) {
        if (!target->fields[i].name || !source->fields[i].name ||
            target->fields[i].offset > target->size ||
            target->fields[i].size > target->size - target->fields[i].offset ||
            source->fields[i].offset > source->size ||
            source->fields[i].size > source->size - source->fields[i].offset ||
            strcmp(target->fields[i].name, source->fields[i].name) ||
            target->fields[i].type != source->fields[i].type ||
            target->fields[i].container != source->fields[i].container) {
            error = "Field record layout differs";
            break;
        }
        pin_type type = target->fields[i].type;
        if ((type == PIN_STRUCT || type == PIN_ENUM || type == PIN_HANDLE ||
             type == PIN_POINTER || type == PIN_QUERY) &&
            strcmp(
                target->fields[i].type_name ? target->fields[i].type_name : "",
                source->fields[i].type_name ? source->fields[i].type_name : ""
            )) {
            error = "Field record nominal type differs";
            break;
        }
        pin_value member;
        error = pin_read_field(&temporary, value.ptr, &source->fields[i], &member);
        if (!error && member.type == PIN_STRUCT) {
            member.schema = value.schema;
        }
        if (!error) {
            error = pin_write_field_resolved(storage, copy, &target->fields[i], member, resolver);
        }
    }
    arena_free(&temporary);
    if (!error) {
        if ((!value.record && source == target) ||
            (value.record && value.record->native && source->size == target->size &&
             type_layout_hash(source) == type_layout_hash(target))) {
            /* Native reflection may omit fields. Keep their original bytes,
             * then publish the validated, owned fields over them. */
            memmove(destination, value.ptr, target->size);
            for (int i = 0; i < target->field_count; i++) {
                const type_field *field = &target->fields[i];
                memcpy(
                    (char *)destination + field->offset,
                    (const char *)copy + field->offset,
                    field->size
                );
            }
        } else {
            memcpy(destination, copy, target->size);
        }
    }
    if (error && !storage) {
        free_field_text(target, copy, 0, resolver);
    }
    free(copy);
    return error;
}
const char *pin_write_field_resolved(arena *storage, void *base, const type_field *f, pin_value v, type_resolver resolver)
{
    if (resolver.find && storage && storage->collectable) {
        return "Custom type resolution requires ordinary storage";
    }
    if (v.type != f->type || v.container != f->container) {
        return "Field value type differs";
    }
    if (f->container == PIN_CONTAINER_ARRAY) {
        pin_value expected = { .type = f->type,
                               .container = f->container,
                               .type_name = f->type_name,
                               .record = f->layout ? f->layout->carrier : NULL };
        if (!pin_value_layout_same(&expected, &v)) {
            return "Field array element type differs";
        }
        pin_value copy;
        if (!storage || pin_value_collection_copy(storage, &copy, &v)) {
            return "Field array copy failed";
        }
        memcpy((char *)base + f->offset, &copy.ptr, sizeof copy.ptr);
        return NULL;
    }
    v = pin_clamp_field(f, v);
    unsigned char *dst = (unsigned char *)base + f->offset;
    switch (f->type) {
        case PIN_BOOL: *(bool *)dst = v.b; break;
        case PIN_I8: *(int8_t *)dst = v.i8; break;
        case PIN_U8: *(uint8_t *)dst = v.u8; break;
        case PIN_I16: *(int16_t *)dst = v.i16; break;
        case PIN_U16: *(uint16_t *)dst = v.u16; break;
        case PIN_I32: *(int32_t *)dst = v.i; break;
        case PIN_U32: *(uint32_t *)dst = v.u; break;
        case PIN_I64: *(int64_t *)dst = v.i64; break;
        case PIN_U64: *(uint64_t *)dst = v.u64; break;
        case PIN_F32: *(float *)dst = v.f; break;
        case PIN_F64: *(double *)dst = v.d; break;
        case PIN_NAME: *(const char **)dst = v.str; break;
        case PIN_STR: {
            pin_string text = pin_text(v);
            if (f->text_descriptor) {
                pin_value copy;
                const char *error = pin_value_text_share(storage, &copy, v);
                if (error) {
                    return error;
                }
                *(const pin_string **)dst = copy.text;
                break;
            }
            if (text.length && memchr(text.bytes, 0, text.length)) {
                return "String contains embedded NUL";
            }
            char *copy = storage ? arena_alloc(storage, (size_t)text.length + 1)
                                 : xmalloc((size_t)text.length + 1);
            if (!copy) {
                return "Field string allocation failed";
            }
            memcpy(copy, text.bytes, text.length);
            copy[text.length] = 0;
            *(const char **)dst = copy;
            break;
        }
        case PIN_HANDLE:
        case PIN_TASK: *(uint64_t *)dst = v.u64; break;
        case PIN_QUERY:
        case PIN_POINTER:
            if (strcmp(f->type_name ? f->type_name : "", v.type_name ? v.type_name : "")) {
                return f->type == PIN_QUERY ? "Field query type differs"
                                            : "Field pointer type differs";
            }
            *(void **)dst = v.ptr;
            break;
        case PIN_UNION: *(void **)dst = v.ptr; break;
        case PIN_ENUM:
            memcpy(dst, &v.e, f->size < sizeof v.e ? f->size : sizeof v.e);
            break;
        case PIN_STRUCT:
            if (v.ptr) {
                const type_desc *target = type_field_type(f, resolver);
                if (!target || target->size != f->size) {
                    return "Field record layout is unavailable";
                }
                return pin_write_struct_resolved(storage, dst, target, v, resolver);
            }
            break;
        case PIN_FLOW: break;
    }
    return NULL;
}

const char *pin_write_struct(arena *storage, void *destination, const type_desc *type, pin_value value)
{
    return pin_write_struct_resolved(storage, destination, type, value, (type_resolver){ 0 });
}
const char *pin_write_field(arena *storage, void *base, const type_field *field, pin_value value)
{
    return pin_write_field_resolved(storage, base, field, value, (type_resolver){ 0 });
}

const char *pin_read_field(arena *storage, const void *base, const type_field *f, pin_value *out)
{
    const unsigned char *src = (const unsigned char *)base + f->offset;
    pin_value value = { .type = f->type,
                        .type_name = f->type_name,
                        .container = f->container };
    if (f->container == PIN_CONTAINER_ARRAY) {
        memcpy(&value.ptr, src, sizeof value.ptr);
        value.record = f->layout ? f->layout->carrier : NULL;
        *out = value;
        return NULL;
    }
    pin_value *destination = out;
    out = &value;
    switch (f->type) {
        case PIN_BOOL: out->b = *(const bool *)src; break;
        case PIN_I8: out->i8 = *(const int8_t *)src; break;
        case PIN_U8: out->u8 = *(const uint8_t *)src; break;
        case PIN_I16: out->i16 = *(const int16_t *)src; break;
        case PIN_U16: out->u16 = *(const uint16_t *)src; break;
        case PIN_I32: out->i = *(const int32_t *)src; break;
        case PIN_U32: out->u = *(const uint32_t *)src; break;
        case PIN_I64: out->i64 = *(const int64_t *)src; break;
        case PIN_U64: out->u64 = *(const uint64_t *)src; break;
        case PIN_F32: out->f = *(const float *)src; break;
        case PIN_F64: out->d = *(const double *)src; break;
        case PIN_NAME: out->str = *(const char *const *)src; break;
        case PIN_STR: {
            if (f->text_descriptor) {
                out->text = *(const pin_string *const *)src;
                break;
            }
            const char *bytes = *(const char *const *)src;
            bytes = bytes ? bytes : "";
            size_t length = strlen(bytes);
            const char *error = pin_value_text(storage, out, bytes, length);
            if (error) {
                return error;
            }
            out->type_name = f->type_name;
            break;
        }
        case PIN_HANDLE:
        case PIN_TASK: out->u64 = *(const uint64_t *)src; break;
        case PIN_QUERY:
        case PIN_POINTER:
        case PIN_UNION: out->ptr = *(void *const *)src; break;
        case PIN_ENUM: {
            out->e = 0;
            memcpy(&out->e, src, f->size < sizeof out->e ? f->size : sizeof out->e);
            break;
        }
        case PIN_STRUCT:
            out->ptr = (void *)src;
            if (f->layout) {
                out->record = f->layout->carrier;
                out->type_name = f->layout->name;
            }
            break;
        case PIN_FLOW: break;
    }
    *destination = value;
    return NULL;
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = (const unsigned char *)data;
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t hash_string(uint64_t hash, const char *text)
{
    if (!text) {
        return hash_bytes(hash, "", 1);
    }
    return hash_bytes(hash, text, strlen(text) + 1);
}

static const type_desc *layout_type(const char *name, const type_desc *const *types, int count, bool explicit)
{
    if (!explicit) {
        return type_catalog_find(name);
    }
    for (int i = 0; i < count; i++) {
        if (types[i] && types[i]->name && !strcmp(types[i]->name, name)) {
            return types[i];
        }
    }
    return NULL;
}
static uint64_t layout_hash(
    const type_desc *type,
    int depth,
    const type_desc *const *types,
    int count,
    bool explicit
)
{
    if (!type || depth > 32) {
        return 0;
    }
    uint64_t hash = UINT64_C(1469598103934665603);
    hash = hash_bytes(hash, &type->size, sizeof type->size);
    hash = hash_bytes(hash, &type->field_count, sizeof type->field_count);
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        hash = hash_string(hash, field->name);
        hash = hash_bytes(hash, &field->offset, sizeof field->offset);
        hash = hash_bytes(hash, &field->size, sizeof field->size);
        hash = hash_bytes(hash, &field->type, sizeof field->type);
        if (field->container != PIN_CONTAINER_SINGLE) {
            hash = hash_bytes(hash, &field->container, sizeof field->container);
        }
        if (field->text_descriptor) {
            hash = hash_bytes(hash, &field->text_descriptor, sizeof field->text_descriptor);
        }
        hash = hash_string(hash, field->type_name);
        if (field->range_contract) {
            hash = hash_bytes(hash, &field->range_contract, sizeof field->range_contract);
            hash = hash_bytes(hash, &field->min, sizeof field->min);
            hash = hash_bytes(hash, &field->max, sizeof field->max);
            hash = hash_bytes(hash, &field->range_default, sizeof field->range_default);
        }
        if (explicit && field->type == PIN_STRUCT && !field->type_name) {
            return 0;
        }
        if (field->type == PIN_STRUCT && field->type_name) {
            uint64_t nested = layout_hash(
                field->layout ? field->layout
                              : layout_type(field->type_name, types, count, explicit),
                depth + 1,
                types,
                count,
                explicit
            );
            if (explicit && !nested) {
                return 0;
            }
            hash = hash_bytes(hash, &nested, sizeof nested);
        }
    }
    return hash;
}

uint64_t type_layout_hash(const type_desc *type)
{
    return layout_hash(type, 0, NULL, 0, false);
}

uint64_t type_layout_hash_explicit(const type_desc *type, const type_desc *const *types, int count)
{
    if (count < 0 || (count && !types)) {
        return 0;
    }
    return layout_hash(type, 0, types, count, true);
}

static bool plain(const type_desc *type, int depth)
{
    if (!type || !type->size || !type->field_count || depth > 16) {
        return false;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *f = &type->fields[i];
        if (f->offset > type->size || f->size > type->size - f->offset ||
            f->container != PIN_CONTAINER_SINGLE || f->type == PIN_FLOW ||
            f->type == PIN_STR || f->type == PIN_NAME || f->type == PIN_UNION ||
            f->type == PIN_POINTER || f->type == PIN_QUERY) {
            return false;
        }
        if (f->type == PIN_STRUCT) {
            const type_desc *nested = type_field_type(f, (type_resolver){ 0 });
            if (!plain(nested, depth + 1) || nested->size != f->size) {
                return false;
            }
        }
    }
    return true;
}
bool type_is_plain(const type_desc *type)
{
    return plain(type, 0);
}
bool type_holds_references(const type_desc *type)
{
    if (!type) {
        return true;
    }
    if (type_alias_layout(type)) {
        return type_holds_references(type_alias_type(type, (type_resolver){ 0 }));
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->container != PIN_CONTAINER_SINGLE ||
            field->type == PIN_UNION || field->type == PIN_POINTER ||
            (field->type == PIN_STRUCT &&
             type_holds_references(type_field_type(field, (type_resolver){ 0 })))) {
            return true;
        }
    }
    return false;
}

static bool value_finite(const type_desc *type, const unsigned char *value, int depth)
{
    if (!type || !value || depth > 16) {
        return false;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        const unsigned char *member = value + field->offset;
        if (field->type == PIN_F32 && !isfinite(*(const float *)member)) {
            return false;
        }
        if (field->type == PIN_F64 && !isfinite(*(const double *)member)) {
            return false;
        }
        if (field->type == PIN_STRUCT) {
            const type_desc *nested = type_field_type(field, (type_resolver){ 0 });
            if (!nested || !value_finite(nested, member, depth + 1)) {
                return false;
            }
        }
    }
    return true;
}

bool type_value_is_finite(const type_desc *type, const void *value)
{
    return type_is_plain(type) && value_finite(type, value, 0);
}
