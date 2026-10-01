#ifndef CHIBI_AUTOMATION_GRAPH_EDIT_H
#define CHIBI_AUTOMATION_GRAPH_EDIT_H

#include "automation/controller.h"
#include "graph/model/behavior_asset.h"

#include <stddef.h>

typedef struct yyjson_mut_doc yyjson_mut_doc;
typedef struct yyjson_mut_val yyjson_mut_val;
typedef struct yyjson_val yyjson_val;

/* Apply a bounded semantic edit batch to a behavior or shader asset clone.
 *
 * The request is an object whose optional graph_id/graph_name selects the
 * graph, and whose operations array contains semantic operations. Node
 * selectors are numeric node IDs or string refs returned by add_node. Pin
 * selectors are pin names or zero-based indices. The helper owns no IO: on
 * success it returns a newly allocated serialized asset in edited_text; on
 * validation or request failure it leaves edited_text NULL and the source
 * remains untouched.
 */
int automation_graph_edit(
    const char *source_text,
    const char *kind,
    const char *name,
    const behavior_asset *const *libraries,
    int library_count,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char **edited_text,
    char *error,
    size_t error_size
);

#endif
