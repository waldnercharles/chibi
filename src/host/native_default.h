#ifndef CHIBI_HOST_NATIVE_DEFAULT_H
#define CHIBI_HOST_NATIVE_DEFAULT_H
#include "host/native_catalog.h"
/* Full native application selection. SDK clients may instead select narrower
 * packs through host_native_catalog_new. Additional declarations are explicit,
 * copied into the owned table; this factory has no registry or process cache. */
native_capabilities *host_native_default_catalog_new(
    const native_capability_pack *,
    int native_count,
    const native_definition_pack *,
    int definition_count,
    char *error,
    size_t size
);
#endif
