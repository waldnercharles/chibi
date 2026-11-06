#ifndef CHIBI_LANGUAGE_SCALAR_CONSTANT_H
#define CHIBI_LANGUAGE_SCALAR_CONSTANT_H

#include "language/semantic.h"

#define SCRIPT_SCALAR_CONSTANT_STEPS 65536

/* Internal evaluation of type-correct analyzer facts after successful scalar
 * admissibility. Literal storage agrees with its type; comparison/NOT results
 * are Boolean, integer operators have integer results and floating operators
 * have floating results. Admissibility alone does not validate those families.
 * The analysis owns literal strings; output is published only on success. */
const char *script_scalar_evaluate(
    arena *storage,
    const script_analysis *,
    int expression,
    pin_type expected,
    pin_value *out
);
typedef enum
{
    SCRIPT_FOLD_OK,
    SCRIPT_FOLD_RANGE, /* The value at `expression` does not fit its type. */
    SCRIPT_FOLD_SHIFT, /* The count at `expression` is above the exact limit. */
    SCRIPT_FOLD_NEGATIVE_SHIFT, /* The exact count is negative. */
    SCRIPT_FOLD_CONVERSION, /* T(value) at `expression` receives a value outside T. */
    SCRIPT_FOLD_REPORTED, /* A zero divisor or unsigned negation. */
} script_fold_code;
typedef struct
{
    script_fold_code code;
    int expression;
} script_fold_error;
/* An integer constant expression: integer literals and named constants, and
 * the integer operators and conversions over them, within
 * SCRIPT_SCALAR_CONSTANT_STEPS nodes. */
bool script_integer_constant(const script_analysis *, int expression);
/* Its exact value: only the result and each conversion operand must fit
 * their types. A zero divisor and the negation of an unsigned value fail as
 * REPORTED, since the analyzer reports them at their own operations. Negative
 * counts are reported from the exact value, even when it cannot fit int64. */
bool script_integer_fold(const script_analysis *, int expression, pin_value *out, script_fold_error *);
/* One operator over operands already converted to its operand type, with
 * run-time semantics: integers wrap, x / 0 is 0, x % 0 is x and a shift past
 * the width gives 0 or the sign. `type` is the result storage. Returns NULL
 * or a static diagnostic. */
const char *script_scalar_operation(
    script_operator,
    pin_value left,
    pin_value right,
    pin_type type,
    pin_value *out
);

#endif
