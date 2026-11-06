#ifndef CHIBI_NATIVE_COMPARISONS_H
#define CHIBI_NATIVE_COMPARISONS_H
#include "execution/host.h"
#include "runtime/comparison_definition.h"
#define DECLARE(KEY)                                                           \
    pin_value native_compare_##KEY(const pin_value *, int, arena *);           \
    extern const node_host_call NATIVE_COMPARE_##KEY##_CALL;
#define REAL(KEY, NAME, SOURCE, OP, EQUALITY, NEGATE, KEYWORDS, DESCRIPTION)   \
    DECLARE(KEY)
#define INTEGER(KEY, NAME, SOURCE, OP, KEYWORDS, DESCRIPTION) DECLARE(KEY)
#define VALUE(KEY, NAME, SOURCE, INPUTS, CATEGORY, KEYWORDS, DESCRIPTION)      \
    DECLARE(KEY)
NATIVE_COMPARE_REALS(REAL)
NATIVE_COMPARE_INTEGERS(INTEGER)
NATIVE_COMPARE_VALUES(VALUE)
#undef DECLARE
#undef REAL
#undef INTEGER
#undef VALUE
extern const node_access NATIVE_COMPARE_ACCESS;
#endif
