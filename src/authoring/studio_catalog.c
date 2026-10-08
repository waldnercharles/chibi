#include "authoring/studio_catalog.h"

#include "authoring/studio_definition.h"
#include "host/native_default.h"
#include "support/array.h"

#include <stdio.h>

native_capabilities *authoring_studio_native_catalog_new(
    const native_definition_pack *extra,
    int count,
    char *error,
    size_t size
)
{
    if (count < 0 || count > 64 || (count && !extra)) {
        if (size) {
            snprintf(error, size, "Invalid Studio application packs");
        }
        return NULL;
    }
    native_type_catalog *types = native_type_catalog_new(STUDIO_TYPE_DEFINITIONS, 2, error, size);
    if (!types) {
        return NULL;
    }
    native_capability_pack pack = native_type_catalog_pack(types);
    native_definition_pack *definitions = NULL;
    apush(definitions, STUDIO_DEFINITION_PACK);
    for (int i = 0; i < count; i++) {
        apush(definitions, extra[i]);
    }
    native_capabilities *out = host_native_default_catalog_new(
        &pack,
        1,
        definitions,
        acount(definitions),
        error,
        size
    );
    afree(definitions);
    native_type_catalog_free(types);
    return out;
}
