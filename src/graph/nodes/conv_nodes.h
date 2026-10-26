#ifndef CHIBI_CONV_NODES_H
#define CHIBI_CONV_NODES_H

#include "graph/model/node.h"

/* CAT-26 / TYPE-7: explicit conversion descriptors. Each is pure with one
 * typed input and one typed output. The editor stores compatible conversions
 * on data edges, so they do not appear as graph nodes. */

extern node_desc NODE_To_String, NODE_To_Real, NODE_To_Byte, NODE_To_Integer,
    NODE_To_Integer64, NODE_To_Name;
extern node_desc NODE_Splat_vec2, NODE_Splat_vec3, NODE_Splat_ShaderVec2,
    NODE_Splat_ShaderVec3, NODE_Splat_ShaderVec4;
bool conversion_is_vector_splat(const node_desc *conversion);

/* Returns the conversion descriptor permitted for `from` feeding `to`, or
 * NULL when the pair is compatible or no conversion is registered. */
const node_desc *conversion_for(pin_type from, pin_type to);
const node_desc *conversion_for_pins(const pin_spec *from, const pin_spec *to);
int conversion_matches_pins(const pin_spec *from, const pin_spec *to, const node_desc *conversion);
/* How well a wire from output `from` fits input `to`: 3 connectable as is
 * (pin_compatible, or both exec), 2 through a conversion, 1 an untyped
 * wildcard, 0 not at all. Settings take no wires. */
int wire_fit(const pin_spec *from, const pin_spec *to);

#endif
