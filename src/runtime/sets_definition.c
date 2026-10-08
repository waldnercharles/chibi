#include "runtime/sets.h"

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
#define ARRAYS(KERNEL, NAME, SOURCE, INPUTS, OUTPUTS, VARIADIC)                    \
    static const native_parameter_definition KERNEL##_inputs[] = { INPUTS(PORT) }; \
    static const native_parameter_definition KERNEL##_outputs[] = { OUTPUTS(PORT) };
NATIVE_SET_OPERATIONS(ARRAYS)
#undef ARRAYS
#undef PORT
#define COUNT(A) (sizeof(A) / sizeof *(A))
#define DEFINITION(KERNEL, NAME, SOURCE, INPUTS, OUTPUTS, VARIADIC)            \
    { .canonical_name = NAME,                                                  \
      NATIVE_SET_##KERNEL##_PRESENTATION,                                      \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                      \
      .inputs = KERNEL##_inputs,                                               \
      .input_count = COUNT(KERNEL##_inputs),                                   \
      .outputs = KERNEL##_outputs,                                             \
      .output_count = COUNT(KERNEL##_outputs),                                 \
      .completion_output = -1,                                                 \
      .pure = true,                                                            \
      .variadic = VARIADIC },
static const native_capability_definition DEFINITIONS[] = {
    NATIVE_SET_OPERATIONS(DEFINITION)
};
#undef DEFINITION
#define BINDING(KERNEL, NAME, SOURCE, INPUTS, OUTPUTS, VARIADIC)               \
    { .canonical_name = NAME,                                                  \
      .implementation = { .call = &NATIVE_SET_##KERNEL##_CALL,                 \
                          .access = &NATIVE_SET_ACCESS } },
static const native_definition_binding BINDINGS[] = { NATIVE_SET_OPERATIONS(BINDING) };
#undef BINDING
const native_definition_pack NATIVE_SET_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS),
};
#undef COUNT
