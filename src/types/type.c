#include "types/type.h"

#include "types/catalog.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

const type_desc TYPE_MARKER = { .name = "marker", .size = 1 };

enum
{
    PIN_FIELD_PATH_MAX_DEPTH = 16,
};

static const type_field *type_field_segment(const type_desc *type, const char *name, size_t length)
{
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
        if (field->type != PIN_STRUCT || !field->type_name) {
            return NULL;
        }
        type = type_catalog_find(field->type_name);
        at = end + 1;
    }
    return NULL;
}

int pin_read_field_path(const void *base, const type_desc *type, const char *path, pin_value *out)
{
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
        pin_read_field(current, field, &value);
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
        type = type_catalog_find(value.type_name);
        at = end + 1;
    }
    return -1;
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

static void validate_ranges(const type_desc *type, void *data, int depth)
{
    assert(depth < TYPE_MAX_NESTING);
    for (int i = 0; i < type->field_count; i++) {
        const type_field *f = &type->fields[i];
        if (f->type == PIN_STRUCT) {
            const type_desc *nested = type_catalog_find(f->type_name);
            if (nested && nested->size == f->size) {
                validate_ranges(nested, (char *)data + f->offset, depth + 1);
            }
        } else if (f->range_contract) {
            pin_value value;
            pin_read_field(data, f, &value);
            pin_write_field(data, f, value);
        }
    }
}

void type_validate_ranges(const type_desc *type, void *data)
{
    validate_ranges(type, data, 0);
}

void pin_write_field(void *base, const type_field *f, pin_value v)
{
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
        case PIN_STR: *(const char **)dst = v.str; break;
        case PIN_HANDLE:
        case PIN_TASK: *(uint64_t *)dst = v.u64; break;
        case PIN_UNION: *(void **)dst = v.ptr; break;
        case PIN_ENUM:
            memcpy(dst, &v.e, f->size < sizeof v.e ? f->size : sizeof v.e);
            break;
        case PIN_STRUCT:
            if (v.ptr) {
                memcpy(dst, v.ptr, f->size);
                const type_desc *nested = type_catalog_find(f->type_name);
                if (nested && nested->size == f->size) {
                    type_validate_ranges(nested, dst);
                }
            }
            break;
        case PIN_FLOW: break;
    }
}

void pin_read_field(const void *base, const type_field *f, pin_value *out)
{
    const unsigned char *src = (const unsigned char *)base + f->offset;
    *out = (pin_value){ .type = f->type, .type_name = f->type_name };
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
        case PIN_STR: out->str = *(const char *const *)src; break;
        case PIN_HANDLE:
        case PIN_TASK: out->u64 = *(const uint64_t *)src; break;
        case PIN_UNION: out->ptr = *(void *const *)src; break;
        case PIN_ENUM: {
            out->e = 0;
            memcpy(&out->e, src, f->size < sizeof out->e ? f->size : sizeof out->e);
            break;
        }
        case PIN_STRUCT: out->ptr = (void *)src; break;
        case PIN_FLOW: break;
    }
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
                layout_type(field->type_name, types, count, explicit),
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
            f->type == PIN_FLOW || f->type == PIN_STR || f->type == PIN_NAME ||
            f->type == PIN_UNION) {
            return false;
        }
        if (f->type == PIN_STRUCT) {
            const type_desc *nested = type_catalog_find(f->type_name);
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
            const type_desc *nested = type_catalog_find(field->type_name);
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
