#ifndef CHIBI_GRAPH_MODEL_VALUE_COPY_H
#define CHIBI_GRAPH_MODEL_VALUE_COPY_H

#include "types/array.h"
#include "types/text.h"

typedef struct graph graph;

/* Graph-owned copies retain registered aggregates and nested string fields. */
int pin_value_copy_to_graph(graph *g, pin_value *dst, const pin_value *src);

pin_value graph_text_value(graph *, const char *bytes, size_t length);
/* NULL denotes a rejected C-string boundary, including embedded NUL. */
const char *graph_text_cstring(const graph *, pin_value);

#endif
