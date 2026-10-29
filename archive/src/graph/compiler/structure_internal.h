#ifndef CHIBI_GRAPH_STRUCTURE_INTERNAL_H
#define CHIBI_GRAPH_STRUCTURE_INTERNAL_H
#include "graph/compiler/validation.h"
int graph_structure_compile(const behavior_asset *, compile_report *);
void graph_structure_sort(compile_report *, const graph *);
#endif
