#ifndef CBLUEPRINT_LIBRARY_H
#define CBLUEPRINT_LIBRARY_H

#include "graph/model/behavior_asset.h"

/* Produces an owned runtime snapshot with qualified library calls resolved.
 * Catalog definitions are borrowed; authored assets are never mutated. */
behavior_asset *behavior_asset_link_libraries(
    const behavior_asset *source,
    const behavior_asset *const *catalog,
    int count,
    char *error,
    size_t error_size
);
const char *graph_library_refresh_call(graph *caller, node *call, const graph *target);
bool behavior_library_node_allowed(behavior_domain domain, const node_desc *desc);
const char *behavior_asset_validate_library(const behavior_asset *asset);

#endif
