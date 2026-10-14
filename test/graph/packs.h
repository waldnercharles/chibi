#ifndef CHIBI_TEST_GRAPH_PACKS_H
#define CHIBI_TEST_GRAPH_PACKS_H

#include "graph/model/registry.h"

/* Registers the model, shader and function call nodes that model and material
 * assets load with, as chibi_ctl does, until test_graph_packs_restore puts the
 * previous packs back. Calls do not nest. */
void test_graph_packs_install();
void test_graph_packs_restore();
/* The shader types, then the game types. */
type_catalog_pack test_graph_types(int index);

#endif
