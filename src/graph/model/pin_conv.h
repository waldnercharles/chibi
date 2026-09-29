#ifndef CHIBI_PIN_CONV_H
#define CHIBI_PIN_CONV_H

#include "graph/model/pin.h" /* pin_type_name() via the PIN_TYPE_LIST X-macro */

/* TYPE-5/TYPE-10: connectable when containers match and types are equal, with
 * F32/F64 treated as one Real category; struct/enum also need equal type_name.
 * Returns 1 compatible, 0 not. */
int pin_compatible(const pin_spec *a, const pin_spec *b);
/* Whether a wildcard output takes its consumer's type on connection. A number
 * feeding a number of another category, such as a global read as int, keeps
 * its type and its wire converts; any other wildcard adopts. */
bool pin_wildcard_adopts(const pin_spec *out, const pin_spec *in);

/* TYPE-10 Real auto-coercion: if v->type differs from target->type only as
 * F32<->F64, convert in place and return 1; else return 0 unchanged.
 * Other scalar conversions use descriptors from conv_nodes.h. */
int pin_coerce(pin_value *v, const pin_spec *target);
int pin_vector_width(const pin_spec *pin);

#endif
