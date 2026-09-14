#ifndef CHIBI_ARRAY_NODES_H
#define CHIBI_ARRAY_NODES_H

#include "graph/model/node.h"

extern node_desc *const ARRAY_NODES[];
extern const int ARRAY_NODE_COUNT;
int behavior_lower_array_search(struct behavior_lower_builder *builder, node *n, int pin);
int behavior_lower_array_get(struct behavior_lower_builder *builder, node *n, int pin);
extern node_desc NODE_ArrayLength, NODE_ArrayLastIndex, NODE_ArrayIsEmpty,
    NODE_ArrayIsNotEmpty, NODE_ArrayIsValidIndex, NODE_ArrayContains, NODE_ArrayFind;

#endif
