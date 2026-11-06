#include "runtime/vectors.h"
#define BANKS(KERNEL, NAME, SOURCE, METHOD, INPUTS, OUTPUTS, RANDOM)                      \
    static const native_parameter_definition KERNEL##_inputs[] = { INPUTS(NATIVE_PORT) }; \
    static const native_parameter_definition KERNEL##_outputs[] = { OUTPUTS(NATIVE_PORT) };
NATIVE_VECTOR_OPERATIONS(BANKS)
#undef BANKS
#define DEFINITION(KERNEL, NAME, SOURCE, METHOD, INPUTS, OUTPUTS, RANDOM)      \
    { .canonical_name = NAME,                                                  \
      .source_name = SOURCE,                                                   \
      .method_name = METHOD,                                                   \
      .receiver_type = METHOD ? "vec2" : NULL,                                 \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                      \
      .inputs = KERNEL##_inputs,                                               \
      .input_count = NATIVE_COUNT(KERNEL##_inputs),                            \
      .outputs = KERNEL##_outputs,                                             \
      .output_count = NATIVE_COUNT(KERNEL##_outputs),                          \
      .completion_output = -1,                                                 \
      .pure = true,                                                            \
      .effects = RANDOM ? SCRIPT_EFFECT_RANDOM : 0 },
static const native_capability_definition DEFINITIONS[] = {
    NATIVE_VECTOR_OPERATIONS(DEFINITION)
};
#undef DEFINITION
#define BINDING(KERNEL, NAME, SOURCE, METHOD, INPUTS, OUTPUTS, RANDOM)         \
    { .canonical_name = NAME,                                                  \
      .implementation = { .call = &NATIVE_##KERNEL##_CALL,                     \
                          .access = RANDOM ? &NATIVE_VECTOR_RANDOM_ACCESS      \
                                           : &NATIVE_VECTOR_ACCESS } },
static const native_definition_binding BINDINGS[] = { NATIVE_VECTOR_OPERATIONS(BINDING) };
#undef BINDING
const native_definition_pack NATIVE_VECTOR_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS)
};
