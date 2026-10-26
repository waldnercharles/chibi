#include "graph/model/pin_conv.h"

#include "types/catalog.h"

#include <string.h>

#define PIN_TYPE_ALIAS_DEPTH 16

static const char *type_root(const char *name)
{
    for (int i = 0; i < PIN_TYPE_ALIAS_DEPTH; i++) {
        const type_desc *type = type_catalog_find(name);
        if (!type || !type->alias_of || strcmp(type->alias_of, name) == 0) {
            return name;
        }
        name = type->alias_of;
    }
    return name;
}

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
        return a->type_name && b->type_name &&
               strcmp(type_root(a->type_name), type_root(b->type_name)) == 0;
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

int pin_vector_width(const pin_spec *pin)
{
    if (pin->type != PIN_STRUCT || pin->container != PIN_CONTAINER_SINGLE ||
        !pin->type_name) {
        return 0;
    }
    const char *name = type_root(pin->type_name);
    if (!strcmp(name, "vec2") || !strcmp(name, "ShaderVec2")) {
        return 2;
    }
    if (!strcmp(name, "vec3") || !strcmp(name, "ShaderVec3")) {
        return 3;
    }
    return !strcmp(name, "ShaderVec4") ? 4 : 0;
}
