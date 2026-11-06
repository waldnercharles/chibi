#ifndef CHIBI_GRAPH_STUDIO_NODES_H
#define CHIBI_GRAPH_STUDIO_NODES_H
#include "authoring/studio_schema.h"
#include "graph/compiler/validation.h"
#include "graph/model/registry.h"
extern const registry_pack STUDIO_PACK;
bool studio_node_is(const node_desc *);
int studio_nodes_check(const behavior_asset *, compile_report *);
#endif
