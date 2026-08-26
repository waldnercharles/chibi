#ifndef CBLUEPRINT_CONV_NODES_H
#define CBLUEPRINT_CONV_NODES_H

#include "node.h"

/* CAT-26 / TYPE-7: explicit conversion descriptors. Each is pure with one
 * typed input and one typed output. The editor stores compatible conversions
 * on data edges, so they do not appear as graph nodes. */

extern node_desc NODE_To_String, NODE_To_Real, NODE_To_Integer64;

/* Returns the conversion descriptor permitted for `from` feeding `to`, or
 * NULL when the pair is compatible or no conversion is registered. */
const node_desc *conversion_for(pin_type from, pin_type to);
const node_desc *conversion_for_pins(const pin_spec *from, const pin_spec *to);

#endif
