#include "runtime/arithmetic.h"
#define PORT(NAME, KIND, TYPE, NOMINAL, CONTAINER, FLAGS, GROUP, REFERENCE, OWNER, VALUE) \
    { .name = NAME,                                                                       \
      .kind = KIND,                                                                       \
      .storage = TYPE,                                                                    \
      .type_name = NOMINAL,                                                               \
      .container = CONTAINER,                                                             \
      .type_flags = FLAGS,                                                                \
      .generic_group = GROUP,                                                             \
      .reference_kind = REFERENCE,                                                        \
      .default_owner = OWNER,                                                             \
      .has_default = true,                                                                \
      .default_value = VALUE },
#define BANKS(KEY, KERNEL, NAME, SOURCE, OPERATOR, INPUTS, OUTPUTS, AGGREGATE)  \
    static const native_parameter_definition KEY##_inputs[] = { INPUTS(PORT) }; \
    static const native_parameter_definition KEY##_outputs[] = { OUTPUTS(PORT) };
NATIVE_ARITHMETIC_OPERATIONS(BANKS)
#undef BANKS
#undef PORT
#define COUNT(A) (sizeof(A) / sizeof *(A))
#define DEFINITION(KEY, KERNEL, NAME, SOURCE, OPERATOR, INPUTS, OUTPUTS, AGGREGATE) \
    { .canonical_name = NAME,                                                       \
      .source_name = SOURCE,                                                        \
      .operator_name = OPERATOR,                                                    \
      .numeric_aggregate = AGGREGATE,                                               \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                           \
      .inputs = KEY##_inputs,                                                       \
      .input_count = COUNT(KEY##_inputs),                                           \
      .outputs = KEY##_outputs,                                                     \
      .output_count = COUNT(KEY##_outputs),                                         \
      .completion_output = -1,                                                      \
      .pure = true,                                                                 \
      .effects = 0 },
static const native_capability_definition DEFINITIONS[] = {
    NATIVE_ARITHMETIC_OPERATIONS(DEFINITION)
};
#undef DEFINITION
#define BINDING(KEY, KERNEL, NAME, SOURCE, OPERATOR, INPUTS, OUTPUTS, AGGREGATE) \
    { .canonical_name = NAME,                                                    \
      .implementation = { .call = &NATIVE_ARITHMETIC_##KEY##_CALL,               \
                          .access = &NATIVE_ARITHMETIC_ACCESS } },
static const native_definition_binding BINDINGS[] = { NATIVE_ARITHMETIC_OPERATIONS(BINDING) };
#undef BINDING
const native_definition_pack NATIVE_ARITHMETIC_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS)
};
#undef COUNT
