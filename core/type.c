#include "type.h"

#include <string.h>

void pin_write_field(void *base, const type_field *f, pin_value v)
{
    unsigned char *dst = (unsigned char *)base + f->offset;
    switch (f->type) {
        case PIN_BOOL: *(bool *)dst = v.b; break;
        case PIN_I32: *(int32_t *)dst = v.i; break;
        case PIN_U32: *(uint32_t *)dst = v.u; break;
        case PIN_F32: *(float *)dst = v.f; break;
        case PIN_F64: *(double *)dst = v.d; break;
        case PIN_STR: *(const char **)dst = v.str; break;
        case PIN_ENUM: *(int32_t *)dst = v.e; break;
        case PIN_STRUCT: memcpy(dst, v.ptr, f->size); break;
        case PIN_FLOW: break;
    }
}

void pin_read_field(const void *base, const type_field *f, pin_value *out)
{
    const unsigned char *src = (const unsigned char *)base + f->offset;
    out->type = f->type;
    out->type_name = f->type_name;
    switch (f->type) {
        case PIN_BOOL: out->b = *(const bool *)src; break;
        case PIN_I32: out->i = *(const int32_t *)src; break;
        case PIN_U32: out->u = *(const uint32_t *)src; break;
        case PIN_F32: out->f = *(const float *)src; break;
        case PIN_F64: out->d = *(const double *)src; break;
        case PIN_STR: out->str = *(const char *const *)src; break;
        case PIN_ENUM: out->e = *(const int32_t *)src; break;
        case PIN_STRUCT: out->ptr = (void *)src; break;
        case PIN_FLOW: break;
    }
}
