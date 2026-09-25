#pragma once
#include "graph/model/registry.h"

extern const registry_pack FIELD_NODE_PACK;
/* Scalar IR opcode, or -1 for nodes outside the field domain. */
int field_node_opcode(const node_desc *desc);
/* The parameter a Field Parameter node reads (Radius), or NULL. */
const char *field_parameter_name(const node *n);
