#ifndef CBLUEPRINT_PIN_CONV_H
#define CBLUEPRINT_PIN_CONV_H

#include "pin.h" /* pin_type_name() via the PIN_TYPE_LIST X-macro */

/* TYPE-5/TYPE-10: connectable when containers match and types are equal, with
 * F32/F64 treated as one Real category; struct/enum also need equal type_name.
 * Returns 1 compatible, 0 not. */
int pin_compatible(const pin_spec *a, const pin_spec *b);

/* TYPE-10 Real auto-coercion: if v->type differs from target->type only as
 * F32<->F64, convert in place and return 1; else return 0 unchanged. TYPE-7
 * explicit conversions are future CAT-26 work and land here later. */
int pin_coerce(pin_value *v, const pin_spec *target);

#endif
