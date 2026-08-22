#include "pin_conv.h"

#include <string.h>

int pin_compatible(const pin_spec *a, const pin_spec *b)
{
    if (!a || !b || a->container != b->container) {
        return 0;
    }
    int real = (a->type == PIN_F32 || a->type == PIN_F64) &&
               (b->type == PIN_F32 || b->type == PIN_F64);
    if (!real && a->type != b->type) {
        return 0;
    }
    if (a->type == PIN_STRUCT || a->type == PIN_ENUM || a->type == PIN_HANDLE) {
        return a->type_name && b->type_name && strcmp(a->type_name, b->type_name) == 0;
    }
    return 1;
}

int pin_coerce(pin_value *v, const pin_spec *target)
{
    if (!v || !target) {
        return 0;
    }
    if (v->type == PIN_F32 && target->type == PIN_F64) {
        v->type = PIN_F64;
        v->type_name = target->type_name;
        v->d = (double)v->f;
        return 1;
    }
    if (v->type == PIN_F64 && target->type == PIN_F32) {
        v->type = PIN_F32;
        v->type_name = target->type_name;
        v->f = (float)v->d;
        return 1;
    }
    return 0;
}
