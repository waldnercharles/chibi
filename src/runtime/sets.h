#ifndef CHIBI_NATIVE_SETS_H
#define CHIBI_NATIVE_SETS_H

#include "execution/host.h"
#include "runtime/sets_definition.h"

/* Direct functions and HOST descriptors return arena-owned payloads. CALL
 * descriptors additionally snapshot through the shared execution ABI. Equality
 * and ordered set indexing use the existing reflected pin-array contract. */
#define DECLARE(KERNEL, NAME, SOURCE, INPUTS, OUTPUTS, VARIADIC)               \
    pin_value native_set_##KERNEL(const pin_value *, int, arena *);            \
    extern const node_host_call NATIVE_SET_##KERNEL##_CALL;                    \
    extern const node_host_call NATIVE_SET_##KERNEL##_HOST;
NATIVE_SET_OPERATIONS(DECLARE)
#undef DECLARE

extern const node_access NATIVE_SET_ACCESS;

#endif
