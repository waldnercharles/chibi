#ifndef CHIBI_GRAPH_PROBLEMS_H
#define CHIBI_GRAPH_PROBLEMS_H

#include "graph/compiler/validation.h"

/* Validating graph assets as the project runs them: linked with its libraries
 * and event definitions, then compiled. Studio's Problems, the graph editor's
 * live compile and native asset inspection share it. */

/* Compiles `asset` linked with the libraries and event definitions in
 * `catalog` into `report`; a failed link is one error. Returns the linked
 * snapshot, whose graphs name where each diagnostic was authored
 * (graph_node_authored), or NULL when linking failed. */
behavior_asset *behavior_asset_validate(
    const behavior_asset *asset,
    const behavior_asset *const *catalog,
    int count,
    compile_report *report
);

/* An error or warning where it was authored. */
typedef struct
{
    const behavior_asset *asset; /* the catalog asset holding the graph */
    uint64_t graph_id, node_id;  /* node 0: the graph as a whole */
    int severity;                /* CMPL_ERROR or CMPL_WARNING */
    char message[CMPL_MESSAGE_MAX];
} catalog_problem;

/* Validates every catalog asset against the rest and lists each error and
 * warning once, by asset in catalog order. A problem in a library shows
 * under the library, however many assets call it. The caller frees the
 * array. */
catalog_problem *behavior_catalog_problems(const behavior_asset *const *catalog, int count);

#endif
