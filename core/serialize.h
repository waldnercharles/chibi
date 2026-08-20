#ifndef CBLUEPRINT_SERIALIZE_H
#define CBLUEPRINT_SERIALIZE_H

#include "blueprint.h"
#include "graph.h"

char *graph_save_json(const graph *g);
graph *graph_load_json(const char *json);
const char *graph_last_error(void);
char *graph_save_nodes_json(const graph *g, node *const *subset, int count);
int graph_append_json(graph *g, const char *json, float dx, float dy);

/* FMT-1/FMT-2/FMT-16: the version-3 Blueprint document (metadata, ID
 * counter, optional owner type, variable list, graph list). Runtime state is
 * never serialized. */
char *blueprint_save_json(const blueprint *bp);
blueprint *blueprint_load_json(const char *json);

#endif
