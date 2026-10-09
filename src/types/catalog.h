#ifndef CHIBI_TYPE_CATALOG_H
#define CHIBI_TYPE_CATALOG_H

#include "types/type.h"

typedef struct
{
    const type_desc *const *types;
    int type_count;
    const enum_desc *const *enums;
    int enum_count;
} type_catalog_pack;

/* Final targets supply an ordered, immutable composition. Descriptors and
 * their strings outlive all programs and values resolved against them. */
int type_catalog_pack_count();
type_catalog_pack type_catalog_pack_at(int index);
/* The loaded project's declared types follow the final target's packs. The
 * owner sets the slot while no project is loaded and clears it (NULL) before
 * its descriptors are freed. */
void type_catalog_set_project(const type_catalog_pack *);

int type_catalog_count();
const type_desc *type_catalog_at(int index);
const type_desc *type_catalog_find(const char *name);
int type_catalog_enum_count();
const enum_desc *type_catalog_enum_at(int index);
const enum_desc *type_catalog_enum_find(const char *name);
const char *type_catalog_enum_label(const char *name, int value);
bool type_catalog_enum_value(const char *name, const char *text, int *out);

#endif
