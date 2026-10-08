#ifndef CHIBI_NATIVE_VECTORS_H
#define CHIBI_NATIVE_VECTORS_H
#include "execution/host.h"
#include "runtime/vectors_definition.h"

/* Struct results are owned by the supplied arena. */
#define DECLARE(KERNEL, NAME, SOURCE, METHOD, INPUTS, OUTPUTS, RANDOM)         \
    pin_value native_##KERNEL(const pin_value *, int, arena *);                \
    extern const node_host_call NATIVE_##KERNEL##_CALL;
NATIVE_VECTOR_OPERATIONS(DECLARE)
#undef DECLARE
extern const node_access NATIVE_VECTOR_ACCESS, NATIVE_VECTOR_RANDOM_ACCESS;
#endif
