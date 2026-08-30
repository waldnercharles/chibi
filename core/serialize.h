#ifndef CBLUEPRINT_SERIALIZE_H
#define CBLUEPRINT_SERIALIZE_H

#include "behavior_asset.h"
#include "graph.h"

char *graph_save_json(const graph *g);
graph *graph_load_json(const char *json);
const char *graph_last_error(void);
char *graph_save_nodes_json(const graph *g, node *const *subset, int count);
int graph_append_json(graph *g, const char *json, float dx, float dy);

/* FMT-1/FMT-2/FMT-16: the version-3 Behavior Asset document (metadata, ID
 * counter, optional owner type, variable list, graph list). Runtime state is
 * never serialized. */
char *behavior_asset_save_json(const behavior_asset *bp);
behavior_asset *behavior_asset_load_json(const char *json);

#endif
