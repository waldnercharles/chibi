#include "types/catalog.h"

#include <string.h>

int type_catalog_count()
{
    int count = 0;
    for (int i = 0; i < type_catalog_pack_count(); i++) {
        count += type_catalog_pack_at(i).type_count;
    }
    return count;
}

const type_desc *type_catalog_at(int index)
{
    if (index < 0) {
        return NULL;
    }
    for (int i = 0; i < type_catalog_pack_count(); i++) {
        type_catalog_pack pack = type_catalog_pack_at(i);
        if (index < pack.type_count) {
            return pack.types[index];
        }
        index -= pack.type_count;
    }
    return NULL;
}

const type_desc *type_catalog_find(const char *name)
{
    if (!name) {
        return NULL;
    }
    if (!strcmp(name, TYPE_MARKER.name)) {
        return &TYPE_MARKER;
    }
    for (int i = 0; i < type_catalog_pack_count(); i++) {
        type_catalog_pack pack = type_catalog_pack_at(i);
        for (int j = 0; j < pack.type_count; j++) {
            if (strcmp(pack.types[j]->name, name) == 0) {
                return pack.types[j];
            }
        }
    }
    return NULL;
}

int type_catalog_enum_count()
{
    int count = 0;
    for (int i = 0; i < type_catalog_pack_count(); i++) {
        count += type_catalog_pack_at(i).enum_count;
    }
    return count;
}

const enum_desc *type_catalog_enum_at(int index)
{
    if (index < 0) {
        return NULL;
    }
    for (int i = 0; i < type_catalog_pack_count(); i++) {
        type_catalog_pack pack = type_catalog_pack_at(i);
        if (index < pack.enum_count) {
            return pack.enums[index];
        }
        index -= pack.enum_count;
    }
    return NULL;
}

const enum_desc *type_catalog_enum_find(const char *name)
{
    if (!name) {
        return NULL;
    }
    for (int i = 0; i < type_catalog_pack_count(); i++) {
        type_catalog_pack pack = type_catalog_pack_at(i);
        for (int j = 0; j < pack.enum_count; j++) {
            if (strcmp(pack.enums[j]->name, name) == 0) {
                return pack.enums[j];
            }
        }
    }
    return NULL;
}

const char *type_catalog_enum_label(const char *enum_name, int value)
{
    const enum_desc *desc = type_catalog_enum_find(enum_name);
    for (int i = 0; desc && i < desc->value_count; i++) {
        if (desc->values[i].value == value) {
            return desc->values[i].label;
        }
    }
    return NULL;
}

bool type_catalog_enum_value(const char *enum_name, const char *text, int *out)
{
    const enum_desc *desc = type_catalog_enum_find(enum_name);
    for (int i = 0; desc && i < desc->value_count; i++) {
        if (!strcmp(desc->values[i].label, text) ||
            !strcmp(desc->values[i].name, text)) {
            *out = desc->values[i].value;
            return true;
        }
    }
    return false;
}
