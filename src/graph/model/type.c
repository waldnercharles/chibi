#include "graph/model/type.h"

#include "graph/model/registry.h"

#include <string.h>

void pin_write_field(void *base, const type_field *f, pin_value v)
{
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
        case PIN_HANDLE: *(uint64_t *)dst = v.u64; break;
        case PIN_ENUM:
            memcpy(dst, &v.e, f->size < sizeof v.e ? f->size : sizeof v.e);
            break;
        case PIN_STRUCT:
            if (v.ptr) {
                memcpy(dst, v.ptr, f->size);
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
        case PIN_HANDLE: out->u64 = *(const uint64_t *)src; break;
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

static uint64_t layout_hash(const type_desc *type, int depth)
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
        if (field->type == PIN_STRUCT && field->type_name) {
            uint64_t nested = layout_hash(registry_type_by_name(field->type_name), depth + 1);
            hash = hash_bytes(hash, &nested, sizeof nested);
        }
    }
    return hash;
}

uint64_t type_layout_hash(const type_desc *type)
{
    return layout_hash(type, 0);
}
