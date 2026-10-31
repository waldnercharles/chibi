#pragma once
#include "graph/model/registry.h"

extern const registry_pack FIELD_NODE_PACK;
/* Scalar IR opcode, or -1 for nodes outside the field domain. */
int field_node_opcode(const node_desc *desc);
