#ifndef CBLUEPRINT_CONV_NODES_H
#define CBLUEPRINT_CONV_NODES_H

#include "node.h"

/* CAT-26 / TYPE-7: explicit conversion nodes. Each is pure with one typed
 * input and one typed output; the input is retypeable (PIN-3) so one node
 * type serves every source type in its retype set. */

extern node_desc NODE_To_String, NODE_To_Real, NODE_To_Integer64;

/* The conversion node TYPE-7 permits for `from` feeding `to`, or NULL when
 * nothing is permitted (the pair is compatible and never queried, or the
 * conversion is lossy). The caller retypes the returned node's input pin to
 * `from` before wiring. */
const node_desc *conversion_for(pin_type from, pin_type to);

#endif
