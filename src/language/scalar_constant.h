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
    const script_analysis *,
    int expression,
    pin_type expected,
    pin_value *out
);

#endif
