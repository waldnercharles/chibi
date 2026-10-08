#include "runtime/vectors.h"
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
#define BANKS(KERNEL, NAME, SOURCE, METHOD, INPUTS, OUTPUTS, RANDOM)               \
    static const native_parameter_definition KERNEL##_inputs[] = { INPUTS(PORT) }; \
    static const native_parameter_definition KERNEL##_outputs[] = { OUTPUTS(PORT) };
NATIVE_VECTOR_OPERATIONS(BANKS)
#undef BANKS
#undef PORT
#define COUNT(A) (sizeof(A) / sizeof *(A))
#define DEFINITION(KERNEL, NAME, SOURCE, METHOD, INPUTS, OUTPUTS, RANDOM)      \
    { .canonical_name = NAME,                                                  \
      .source_name = SOURCE,                                                   \
      .method_name = METHOD,                                                   \
      .receiver_type = METHOD ? "CF_V2" : NULL,                                \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                      \
      .inputs = KERNEL##_inputs,                                               \
      .input_count = COUNT(KERNEL##_inputs),                                   \
      .outputs = KERNEL##_outputs,                                             \
      .output_count = COUNT(KERNEL##_outputs),                                 \
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
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS)
};
#undef COUNT
