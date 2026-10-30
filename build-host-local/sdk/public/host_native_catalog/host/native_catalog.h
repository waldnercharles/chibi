#ifndef CHIBI_HOST_NATIVE_CATALOG_H
#define CHIBI_HOST_NATIVE_CATALOG_H

#include "runtime/native_definitions.h"

/* Application-selected neutral packs plus the authoritative engine event
 * declarations. The returned immutable table owns all converted records;
 * native kernels and target layouts retain their declaration lifetime. */
native_capabilities *host_native_catalog_new(
    const native_capability_pack *,
    int native_count,
    const native_definition_pack *,
    int definition_count,
    char *error,
    size_t size
);

#endif
