#ifndef CHIBI_AUTHORING_STUDIO_CATALOG_H
#define CHIBI_AUTHORING_STUDIO_CATALOG_H
#include "runtime/native_definitions.h"
/* Explicit Studio service/type augmentation of the native application packs.
 * The caller owns one table reference. No live Studio world is consulted. */
native_capabilities *authoring_studio_native_catalog_new(
    const native_definition_pack *extra,
    int count,
    char *error,
    size_t size
);
#endif
