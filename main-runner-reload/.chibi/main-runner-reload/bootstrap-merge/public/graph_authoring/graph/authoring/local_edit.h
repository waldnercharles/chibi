#ifndef CHIBI_GRAPH_AUTHORING_LOCAL_EDIT_H
#define CHIBI_GRAPH_AUTHORING_LOCAL_EDIT_H

#include "graph/model/local.h"

/* Names every existing consumer atomically. Refuses dormant bindings. */
uint64_t graph_create_local(graph *g, node *source, int pin, const char *name);

/* Wires the local's source into an input and names it there, as a use. */
int graph_use_local(graph *g, uint64_t id, node *n, int pin);

const char *graph_validate_locals(const graph *g);

#endif
