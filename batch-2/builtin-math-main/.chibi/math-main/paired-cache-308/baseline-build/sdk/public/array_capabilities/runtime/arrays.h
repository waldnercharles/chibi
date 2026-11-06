#ifndef CHIBI_NATIVE_ARRAYS_H
#define CHIBI_NATIVE_ARRAYS_H

#include "execution/host.h"
#include "runtime/arrays_definition.h"

/* Every retained result owns its payload in the supplied arena. */
#define DECLARE(KERNEL, NAME, SOURCE, INPUTS, OUTPUTS, VARIADIC)               \
    pin_value native_array_##KERNEL(const pin_value *, int, arena *);          \
    extern const node_host_call NATIVE_ARRAY_##KERNEL##_CALL;                  \
    extern const node_host_call NATIVE_ARRAY_##KERNEL##_HOST;
NATIVE_ARRAY_OPERATIONS(DECLARE)
#undef DECLARE

extern const node_access NATIVE_ARRAY_ACCESS;

#endif
