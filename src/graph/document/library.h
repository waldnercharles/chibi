#ifndef CHIBI_LIBRARY_H
#define CHIBI_LIBRARY_H

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
/* Where a node of a linked snapshot was authored. Graphs copied from a library,
 * or expanded from a macro, name their source asset and give the ids the node
 * and graph have there; the snapshot's own graphs keep theirs with no asset. */
typedef struct
{
    const char *asset; /* borrowed from the graph; NULL for the snapshot's own */
    uint64_t graph_id, node_id;
} graph_node_source;
graph_node_source graph_node_authored(const graph *g, uint64_t node_id);
const char *graph_library_refresh_call(graph *caller, node *call, const graph *target);
bool behavior_library_node_allowed(behavior_domain domain, const node_desc *desc);
const char *behavior_asset_validate_library(const behavior_asset *asset);

#endif
