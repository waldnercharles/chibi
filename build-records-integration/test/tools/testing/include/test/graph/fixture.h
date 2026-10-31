#ifndef CHIBI_TEST_GRAPH_FIXTURE_H
#define CHIBI_TEST_GRAPH_FIXTURE_H

#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"

/* Composes the application's graph palette (core, shader, field and native
 * nodes over the game's reflected components) into the registry, and restores
 * the previous composition when closed. */
void graph_palette_open();
void graph_palette_close();
/* The registry's node of that name; asserts that it exists. */
const node_desc *graph_palette_node(const char *name);

/* Executable behaviors save as readable .chibi source, so their graphs
 * persist through the asset value document that history snapshots and the
 * clipboard use. The caller frees the text and the asset. */
char *graph_asset_text(const behavior_asset *asset);
behavior_asset *graph_asset_read(const char *text);

#endif
