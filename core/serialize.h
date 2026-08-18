#ifndef CBLUEPRINT_SERIALIZE_H
#define CBLUEPRINT_SERIALIZE_H

#include "graph.h"

/* Serialize g to a pretty JSON string (malloc'd; caller frees). NULL on error. */
char *graph_save_json(const graph *g);

/* Parse JSON into a fresh heap-allocated graph (caller calls graph_free).
 * Returns NULL on error; the message is available via graph_last_error(). */
graph *graph_load_json(const char *json);

/* Message from the most recent failed graph_load_json on this thread. A
 * subsequent serialization call overwrites it. */
const char *graph_last_error(void);

/* Serialize only the nodes in subset (in subset order; ids local to the
 * subset) plus the edges whose both endpoints are in subset. Same schema as
 * graph_save_json. NULL on error. */
char *graph_save_nodes_json(const graph *g, node *const *subset, int count);

/* Parse json (the graph_save_json schema) and append its nodes (positions
 * offset by dx/dy), edges, and owned string literals to g. Returns 0 on
 * success, -1 on error (g is left unchanged); message in graph_last_error(). */
int graph_append_json(graph *g, const char *json, float dx, float dy);

#endif
