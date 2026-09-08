#ifndef CBLUEPRINT_STATE_NODES_H
#define CBLUEPRINT_STATE_NODES_H

#include "graph/model/node.h"

extern node_desc NODE_DoOnce, NODE_DoN, NODE_Gate, NODE_MultiGate, NODE_FlipFlop;
extern node_desc *const STATE_NODES[];
extern const int STATE_NODE_COUNT;

#endif
