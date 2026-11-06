#ifndef CHIBI_NATIVE_CONVERSIONS_H
#define CHIBI_NATIVE_CONVERSIONS_H
#include "execution/host.h"
#include "runtime/conversion_definition.h"
/* String/name/vector results are owned by the supplied arena. */
#define DECLARE(KEY)                                                           \
    pin_value native_conversion_##KEY(const pin_value *, int, arena *);        \
    extern const node_host_call NATIVE_CONVERSION_##KEY##_CALL;
#define SCALAR(KEY, NAME, SOURCE, INPUT, INPUT_NAME, RESULT, OUTPUT, OUTPUT_NAME) \
    DECLARE(KEY)
#define VECTOR(TYPE, WIDTH, NAME, SOURCE, DESCRIPTION) DECLARE(Splat_##TYPE)
NATIVE_CONVERSION_SCALARS(SCALAR)
NATIVE_CONVERSION_VECTORS(VECTOR)
#undef SCALAR
#undef VECTOR
#undef DECLARE
extern const node_access NATIVE_CONVERSION_ACCESS;
#endif
