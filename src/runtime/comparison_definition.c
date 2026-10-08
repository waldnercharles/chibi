#include "runtime/comparisons.h"
#define PORT(NAME, KIND, STORAGE, TYPE_NAME, CONTAINER, FLAGS, GROUP, REFERENCE, OWNER, VALUE) \
    { .name = NAME,                                                                            \
      .kind = KIND,                                                                            \
      .storage = STORAGE,                                                                      \
      .type_name = TYPE_NAME,                                                                  \
      .container = CONTAINER,                                                                  \
      .type_flags = FLAGS,                                                                     \
      .generic_group = GROUP,                                                                  \
      .reference_kind = REFERENCE,                                                             \
      .default_owner = OWNER,                                                                  \
      .has_default = true,                                                                     \
      .default_value = VALUE },
static const native_parameter_definition REAL_INPUTS[] = {
    NATIVE_COMPARE_REAL_INPUTS(PORT)
};
static const native_parameter_definition INTEGER_INPUTS[] = {
    NATIVE_COMPARE_INTEGER_INPUTS(PORT)
};
static const native_parameter_definition OUTPUTS[] = { NATIVE_COMPARE_OUTPUTS(PORT) };
#define VALUE(KEY, NAME, SOURCE, INPUTS, CATEGORY, KEYWORDS, DESCRIPTION)      \
    static const native_parameter_definition KEY##_inputs[] = { INPUTS(PORT) };
NATIVE_COMPARE_VALUES(VALUE)
#undef VALUE
#define COUNT(A) (sizeof(A) / sizeof *(A))
#define DEFINITION(KEY, NAME, SOURCE, INPUTS, OP)                              \
    { .canonical_name = NAME,                                                  \
      .source_name = SOURCE,                                                   \
      .operator_name = OP,                                                     \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                      \
      .inputs = INPUTS,                                                        \
      .input_count = COUNT(INPUTS),                                            \
      .outputs = OUTPUTS,                                                      \
      .output_count = COUNT(OUTPUTS),                                          \
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
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS)
};
