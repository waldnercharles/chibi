#include "language/scalar_constant.h"
#include "language/semantic_internal.h"

/* Admissibility checks the complete graph before short-circuit evaluation. */
const char *script_analyzer_scalar_constant(analyzer *a, int index, pin_type expected, pin_value *out)
{
    int steps = SCRIPT_SCALAR_CONSTANT_STEPS;
    if (!script_analyzer_compile_time_value(a, index, &steps, true)) {
        return "requires bounded compile-time values and operators";
    }
    return script_scalar_evaluate(&a->out->constants, a->out, index, expected, out);
}
