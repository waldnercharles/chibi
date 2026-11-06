#ifndef CHIBI_GRAPH_MODEL_VALUE_COPY_H
#define CHIBI_GRAPH_MODEL_VALUE_COPY_H

#include "types/array.h"

typedef struct graph graph;

/* Graph-owned copies retain registered aggregates and nested string fields. */
pin_array *pin_array_clone_to_graph(graph *g, const pin_array *src);
int pin_value_copy_to_graph(graph *g, pin_value *dst, const pin_value *src);

#endif
