#ifndef CHIBI_WORKSPACE_CATALOG_H
#define CHIBI_WORKSPACE_CATALOG_H
#include "runtime/native_definitions.h"
#include "types/catalog.h"

/* Optional source-authoring capabilities. This composition contains no
 * application state or UI policy. Extra definitions select optional backends,
 * for example declaration-only or GPU-bound preview operations. */
native_capabilities *authoring_workspace_catalog_new(
    const native_definition_pack *extra,
    int count,
    char *error,
    size_t size
);
/* Immutable target layouts for the final application's type composition. */
int authoring_workspace_type_pack_count();
type_catalog_pack authoring_workspace_type_pack_at(int index);
#endif
