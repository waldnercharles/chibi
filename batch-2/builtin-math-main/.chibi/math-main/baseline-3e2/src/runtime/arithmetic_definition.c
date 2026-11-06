#include "runtime/arithmetic.h"
#define BANKS(KEY, KERNEL, NAME, SOURCE, OPERATOR, INPUTS, OUTPUTS, AGGREGATE)         \
    static const native_parameter_definition KEY##_inputs[] = { INPUTS(NATIVE_PORT) }; \
    static const native_parameter_definition KEY##_outputs[] = { OUTPUTS(NATIVE_PORT) };
NATIVE_ARITHMETIC_OPERATIONS(BANKS)
#undef BANKS
#define DEFINITION(KEY, KERNEL, NAME, SOURCE, OPERATOR, INPUTS, OUTPUTS, AGGREGATE) \
    { .canonical_name = NAME,                                                       \
      .source_name = SOURCE,                                                        \
      .operator_name = OPERATOR,                                                    \
      .numeric_aggregate = AGGREGATE,                                               \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                           \
      .inputs = KEY##_inputs,                                                       \
      .input_count = NATIVE_COUNT(KEY##_inputs),                                    \
      .outputs = KEY##_outputs,                                                     \
      .output_count = NATIVE_COUNT(KEY##_outputs),                                  \
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
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS)
};
