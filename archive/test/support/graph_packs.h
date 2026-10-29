#ifndef CHIBI_TEST_GRAPH_PACKS_H
#define CHIBI_TEST_GRAPH_PACKS_H

#include <stdbool.h>

/* Installs the graph registry an application composes: the default native
 * catalog's nodes over the game's reflected types and components. Returns
 * false with a message on stderr when the catalog cannot be built. */
bool test_graph_packs_set();
/* Restores the packs installed before and releases the catalog. */
void test_graph_packs_clear();

#endif
