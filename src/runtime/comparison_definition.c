#include "runtime/comparisons.h"
static const native_parameter_definition REAL_INPUTS[] = {
    NATIVE_COMPARE_REAL_INPUTS(NATIVE_PORT)
};
static const native_parameter_definition INTEGER_INPUTS[] = {
    NATIVE_COMPARE_INTEGER_INPUTS(NATIVE_PORT)
};
static const native_parameter_definition OUTPUTS[] = { NATIVE_COMPARE_OUTPUTS(NATIVE_PORT) };
#define VALUE(KEY, NAME, SOURCE, INPUTS, CATEGORY, KEYWORDS, DESCRIPTION)      \
    static const native_parameter_definition KEY##_inputs[] = { INPUTS(NATIVE_PORT) };
NATIVE_COMPARE_VALUES(VALUE)
#undef VALUE
#define DEFINITION(KEY, NAME, SOURCE, INPUTS, OP)                              \
    { .canonical_name = NAME,                                                  \
      .source_name = SOURCE,                                                   \
      .operator_name = OP,                                                     \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                      \
      .inputs = INPUTS,                                                        \
      .input_count = NATIVE_COUNT(INPUTS),                                     \
      .outputs = OUTPUTS,                                                      \
      .output_count = NATIVE_COUNT(OUTPUTS),                                   \
      .completion_output = -1,                                                 \
      .pure = true },
#define REAL(KEY, NAME, SOURCE, OP, EQUALITY, NEGATE, KEYWORDS, DESCRIPTION)   \
    DEFINITION(KEY, NAME, SOURCE, REAL_INPUTS, #OP)
#define INTEGER(KEY, NAME, SOURCE, OP, KEYWORDS, DESCRIPTION)                  \
    DEFINITION(KEY, NAME, SOURCE, INTEGER_INPUTS, #OP)
#define VALUE(KEY, NAME, SOURCE, INPUTS, CATEGORY, KEYWORDS, DESCRIPTION)      \
    DEFINITION(KEY, NAME, SOURCE, KEY##_inputs, NULL)
static const native_capability_definition DEFINITIONS[] = {
    NATIVE_COMPARE_REALS(REAL) NATIVE_COMPARE_INTEGERS(INTEGER)
        NATIVE_COMPARE_VALUES(VALUE)
};
#undef REAL
#undef INTEGER
#undef VALUE
#define REAL(KEY, NAME, SOURCE, OP, EQUALITY, NEGATE, KEYWORDS, DESCRIPTION)   \
    { .canonical_name = NAME,                                                  \
      .implementation = { .call = &NATIVE_COMPARE_##KEY##_CALL,                \
                          .access = &NATIVE_COMPARE_ACCESS } },
#define INTEGER(KEY, NAME, SOURCE, OP, KEYWORDS, DESCRIPTION)                  \
    { .canonical_name = NAME,                                                  \
      .implementation = { .call = &NATIVE_COMPARE_##KEY##_CALL,                \
                          .access = &NATIVE_COMPARE_ACCESS } },
#define VALUE(KEY, NAME, SOURCE, INPUTS, CATEGORY, KEYWORDS, DESCRIPTION)      \
    { .canonical_name = NAME,                                                  \
      .implementation = { .call = &NATIVE_COMPARE_##KEY##_CALL,                \
                          .access = &NATIVE_COMPARE_ACCESS } },
static const native_definition_binding BINDINGS[] = { NATIVE_COMPARE_REALS(
    REAL
) NATIVE_COMPARE_INTEGERS(INTEGER) NATIVE_COMPARE_VALUES(VALUE) };
const native_definition_pack NATIVE_COMPARISON_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS)
};
