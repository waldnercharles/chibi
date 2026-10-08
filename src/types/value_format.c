#include "types/value_format.h"

#include "types/array.h"
#include "types/catalog.h"

#include <stdio.h>

#define PIN_FMT_RING 8
#define PIN_FMT_CAP 64

const char *pin_value_to_string(const pin_value *v)
{
    static char ring[PIN_FMT_RING][PIN_FMT_CAP];
    static int slot;
    char *buf = ring[slot];
    slot = (slot + 1) % PIN_FMT_RING;

    if (!v) {
        snprintf(buf, PIN_FMT_CAP, "<null>");
        return buf;
    }

    if (v->container >= PIN_CONTAINER_ARRAY) {
        const pin_array *array = (const pin_array *)v->ptr;
        snprintf(buf, PIN_FMT_CAP, "Array[%d]", array ? array->count : 0);
        return buf;
    }

    switch (v->type) {
        case PIN_BOOL:
            snprintf(buf, PIN_FMT_CAP, "%s", v->b ? "true" : "false");
            break;
        case PIN_I8: snprintf(buf, PIN_FMT_CAP, "%d", (int)v->i8); break;
        case PIN_U8: snprintf(buf, PIN_FMT_CAP, "%u", (unsigned)v->u8); break;
        case PIN_I16: snprintf(buf, PIN_FMT_CAP, "%d", (int)v->i16); break;
        case PIN_U16: snprintf(buf, PIN_FMT_CAP, "%u", (unsigned)v->u16); break;
        case PIN_I32: snprintf(buf, PIN_FMT_CAP, "%d", (int)v->i); break;
        case PIN_U32: snprintf(buf, PIN_FMT_CAP, "%u", (unsigned)v->u); break;
        case PIN_I64:
            snprintf(buf, PIN_FMT_CAP, "%lld", (long long)v->i64);
            break;
        case PIN_U64:
            snprintf(buf, PIN_FMT_CAP, "%llu", (unsigned long long)v->u64);
            break;
        case PIN_F32: snprintf(buf, PIN_FMT_CAP, "%g", (double)v->f); break;
        case PIN_F64: snprintf(buf, PIN_FMT_CAP, "%g", v->d); break;
        case PIN_NAME:
            snprintf(buf, PIN_FMT_CAP, "%s", v->str ? v->str : "");
            break;
        case PIN_STR:
            snprintf(buf, PIN_FMT_CAP, "%s", v->str ? v->str : "");
            break;
        case PIN_HANDLE:
            if (v->type_name) {
                snprintf(
                    buf,
                    PIN_FMT_CAP,
                    "%s(%llu)",
                    v->type_name,
                    (unsigned long long)v->u64
                );
            } else {
                snprintf(buf, PIN_FMT_CAP, "handle(%llu)", (unsigned long long)v->u64);
            }
            break;
        case PIN_STRUCT:
            snprintf(buf, PIN_FMT_CAP, "%s", v->type_name ? v->type_name : "struct");
            break;
        case PIN_ENUM: {
            const enum_desc *enumeration = type_catalog_enum_find(v->type_name);
            for (int i = 0; enumeration && i < enumeration->value_count; i++) {
                if (enumeration->values[i].value == v->e) {
                    snprintf(buf, PIN_FMT_CAP, "%s", enumeration->values[i].label);
                    return buf;
                }
            }
            if (v->type_name) {
                snprintf(buf, PIN_FMT_CAP, "%s(%d)", v->type_name, (int)v->e);
            } else {
                snprintf(buf, PIN_FMT_CAP, "%d", (int)v->e);
            }
            break;
        }
        case PIN_FLOW:
        default: snprintf(buf, PIN_FMT_CAP, "flow"); break;
    }
    return buf;
}
