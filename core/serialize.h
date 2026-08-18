#ifndef CBLUEPRINT_SERIALIZE_H
#define CBLUEPRINT_SERIALIZE_H

#include "graph.h"

char *graph_save_json(const graph *g);
graph *graph_load_json(const char *json);
const char *graph_last_error(void);
char *graph_save_nodes_json(const graph *g, node *const *subset, int count);
int graph_append_json(graph *g, const char *json, float dx, float dy);

#endif
