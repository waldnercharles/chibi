#ifndef CHIBI_NATIVE_TYPE_DEFINITIONS_H
#define CHIBI_NATIVE_TYPE_DEFINITIONS_H
#include "runtime/native_capabilities.h"

typedef struct
{
    const type_desc *layout;
    pin_type storage;
    const char *source_name, *key;
    uint64_t component;
} native_type_definition;
typedef struct native_type_catalog native_type_catalog;

/* Explicit neutral layouts own the field declarations. This intermediate owns
 * converted arrays and borrows layout/name records until table construction.
 * Only flat structs with fixed scalar fields are accepted; scalar widths,
 * alignment and field identities are checked here. The final native table
 * verifies target layout hashes and nominal references. */
native_type_catalog *native_type_catalog_new(
    const native_type_definition *,
    int count,
    char *error,
    size_t size
);
native_capability_pack native_type_catalog_pack(const native_type_catalog *);
void native_type_catalog_free(native_type_catalog *);
#endif
