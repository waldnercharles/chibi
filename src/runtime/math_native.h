#ifndef CHIBI_NATIVE_MATH_H
#define CHIBI_NATIVE_MATH_H
#include "execution/host.h"
#include "runtime/math_definition.h"
#define DECLARE(KEY)                                                           \
    pin_value native_math_##KEY(const pin_value *, int, arena *);              \
    extern const node_host_call NATIVE_MATH_##KEY##_CALL;
#define UNARY(KEY, NAME, SOURCE, FN, DESCRIPTION) DECLARE(KEY)
#define BINARY(KEY, NAME, SOURCE, FN, INPUTS, KEYWORDS, DESCRIPTION)           \
    DECLARE(KEY)
#define VALUE(KEY, NAME, SOURCE, INPUTS, RANDOM) DECLARE(KEY)
NATIVE_MATH_UNARIES(UNARY)
NATIVE_MATH_BINARIES(BINARY)
NATIVE_MATH_VALUES(VALUE)
#undef UNARY
#undef BINARY
#undef VALUE
#undef DECLARE
const char *native_math_spring(const pin_value *, pin_value *);
extern const node_host_call NATIVE_MATH_Advance_Damped_Spring_CALL;
extern const node_access NATIVE_MATH_ACCESS, NATIVE_MATH_RANDOM_ACCESS;
#endif
