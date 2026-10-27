#include "types/value_format.h"

#include "types/array.h"
#include "types/catalog.h"
#include "types/map.h"
#include "types/text.h"
#include "types/union.h"

#include <stdio.h>

#define PIN_FMT_RING 8
#define PIN_FMT_CAP 64

const char *pin_value_to_string(const pin_value *v)
{
    static _Thread_local char ring[PIN_FMT_RING][PIN_FMT_CAP];
    static _Thread_local int slot;
    char *buf = ring[slot];
    slot = (slot + 1) % PIN_FMT_RING;

    if (!v) {
        snprintf(buf, PIN_FMT_CAP, "<null>");
        return buf;
    }

    if (v->container == PIN_CONTAINER_MAP) {
        const pin_map *map = v->ptr;
        snprintf(buf, PIN_FMT_CAP, "Map[%d]", map ? map->count : 0);
        return buf;
    }

    if (v->container >= PIN_CONTAINER_ARRAY) {
        const pin_array *array = v->ptr;
        snprintf(buf, PIN_FMT_CAP, "Array[%d]", array ? array->count : 0);
        return buf;
    }

    switch (v->type) {
        case PIN_POINTER:
            snprintf(buf, PIN_FMT_CAP, "%s", v->ptr ? "pointer" : "null");
            break;
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
        case PIN_STR: {
            pin_string text = pin_text(*v);
            size_t out = 0;
            bool utf8 = pin_text_valid_utf8(text);
            for (uint32_t i = 0; i < text.length && out + 1 < PIN_FMT_CAP; i++) {
                unsigned char byte = (unsigned char)text.bytes[i];
                if (utf8 && byte >= 128) {
                    unsigned width = byte < 0xe0 ? 2 : byte < 0xf0 ? 3 : 4;
                    if (out + width >= PIN_FMT_CAP) {
                        break;
                    }
                    for (unsigned j = 0; j < width; j++) {
                        buf[out++] = text.bytes[i + j];
                    }
                    i += width - 1;
                    continue;
                }
                if (byte < 32 || byte == 127 || (byte >= 128 && !utf8) || byte == '\\') {
                    if (out + 4 >= PIN_FMT_CAP) {
                        break;
                    }
                    snprintf(buf + out, PIN_FMT_CAP - out, "\\x%02x", byte);
                    out += 4;
                } else {
                    buf[out++] = (char)byte;
                }
            }
            buf[out] = 0;
            break;
        }
        case PIN_TASK:
            snprintf(buf, PIN_FMT_CAP, "task(%llu)", (unsigned long long)v->u64);
            break;
        case PIN_UNION: {
            const pin_union *value = v->ptr;
            if (!value) {
                snprintf(buf, PIN_FMT_CAP, "nil");
            } else if (value->tag == PIN_UNION_CANCELLED_TAG) {
                snprintf(buf, PIN_FMT_CAP, "Cancelled");
            } else {
                snprintf(
                    buf,
                    PIN_FMT_CAP,
                    "%s(%llu)",
                    v->type_name ? v->type_name : "union",
                    (unsigned long long)value->tag
                );
            }
            break;
        }
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
