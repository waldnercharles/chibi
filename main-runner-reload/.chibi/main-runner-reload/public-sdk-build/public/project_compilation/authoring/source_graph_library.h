#ifndef CHIBI_AUTHORING_SOURCE_GRAPH_LIBRARY_H
#define CHIBI_AUTHORING_SOURCE_GRAPH_LIBRARY_H

#include "authoring/source_collection.h"
#include "graph/model/behavior_asset.h"

/* Graph assets such as fields call library functions as graphs. These are the
 * collection's source libraries lowered to function graphs: a pure function
 * whose body returns values built from its parameters, constants, pure native
 * calls that name graph nodes and calls to other such functions of one result
 * becomes a graph of those nodes and calls, and its parameters keep their
 * constant defaults; other functions are left out. Calls into another library
 * name it, so linking a caller links that library too.
 * Calls name these functions, since source functions have no stable graph
 * ids. The returned assets mark their function IDs as transient, retained by
 * in-memory cloning; authored external calls use names.
 * An array.h vector of owned assets, freed with
 * authoring_source_graph_libraries_free. */
behavior_asset **authoring_source_graph_libraries(const authoring_sources *);
/* The same native field/shader graph projection from a checked declaration
 * snapshot. Analyzes only library sources with that snapshot's identities,
 * catalog and specialization requests; never lowers executable modules.
 * Graphs own their names and defaults after the temporary analyses are freed.
 * `out` starts empty and stays empty on failure. */
bool authoring_source_graph_libraries_catalog(
    const source_catalog *,
    behavior_asset ***out,
    char *error,
    size_t size
);
void authoring_source_graph_libraries_free(behavior_asset **);

#endif
