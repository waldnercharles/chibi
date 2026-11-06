#include "authoring/studio_definition.h"
#define PORT(NAME, KIND, STORAGE, NOMINAL, CONTAINER, FLAGS, GROUP, REFERENCE, OWNER, VALUE) \
    { .name = NAME,                                                                          \
      .kind = KIND,                                                                          \
      .storage = STORAGE,                                                                    \
      .type_name = NOMINAL,                                                                  \
      .container = CONTAINER,                                                                \
      .type_flags = FLAGS,                                                                   \
      .generic_group = GROUP,                                                                \
      .reference_kind = REFERENCE,                                                           \
      .default_owner = OWNER,                                                                \
      .has_default = true,                                                                   \
      .default_value = VALUE },
#define BANK(ID, OP, NAME, SOURCE, KIND, PURE, EFFECT, RESULTS)                \
    static const native_parameter_definition ID##_inputs[] = {                 \
        STUDIO_##ID##_INPUTS(PORT){ 0 }                                        \
    };                                                                         \
    static const native_parameter_definition ID##_outputs[] = {                \
        STUDIO_##ID##_OUTPUTS(PORT){ 0 }                                       \
    };
STUDIO_OPERATIONS(BANK)
#undef BANK
#undef PORT
#define COUNT(A) ((int)(sizeof A / sizeof *A) - 1)
#define DEFINITION(ID, OP, NAME, SOURCE, KIND, PURE, EFFECT, RESULTS)          \
    { .canonical_name = NAME,                                                  \
      .source_name = SOURCE,                                                   \
      .kind = KIND,                                                            \
      .pure = PURE,                                                            \
      .inputs = ID##_inputs,                                                   \
      .input_count = COUNT(ID##_inputs),                                       \
      .outputs = ID##_outputs,                                                 \
      .output_count = COUNT(ID##_outputs),                                     \
      .completion_output = PURE ? -1 : 0 },
static const native_capability_definition DEFINITIONS[] = { STUDIO_OPERATIONS(DEFINITION) };
#undef DEFINITION
#define BINDING(ID, OP, NAME, SOURCE, KIND, PURE, EFFECT, RESULTS)             \
    { .canonical_name = NAME,                                                  \
      .implementation = { .call = &STUDIO_CALLS[OP],                           \
                          .access = EFFECT ? &STUDIO_WRITE_ACCESS              \
                                           : &STUDIO_READ_ACCESS } },
static const native_definition_binding BINDINGS[] = { STUDIO_OPERATIONS(BINDING) };
#undef BINDING
const native_definition_pack STUDIO_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = sizeof DEFINITIONS / sizeof *DEFINITIONS,
    .bindings = BINDINGS,
    .binding_count = sizeof BINDINGS / sizeof *BINDINGS
};
