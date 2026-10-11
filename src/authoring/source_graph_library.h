#ifndef CHIBI_AUTHORING_SOURCE_GRAPH_LIBRARY_H
#define CHIBI_AUTHORING_SOURCE_GRAPH_LIBRARY_H

#include "authoring/source_collection.h"
#include "graph/model/behavior_asset.h"

/* Graph assets such as fields call library functions as graphs. These are the
 * collection's source libraries lowered to function graphs: a pure function
 * whose body returns values built from its parameters, constants and pure
 * native calls that name graph nodes becomes a graph of those nodes; other
 * functions are left out. Calls name these functions, since source functions
 * have no stable graph ids. An array.h vector of owned assets, freed with
 * authoring_source_graph_libraries_free. */
behavior_asset **authoring_source_graph_libraries(const authoring_sources *);
void authoring_source_graph_libraries_free(behavior_asset **);

#endif
