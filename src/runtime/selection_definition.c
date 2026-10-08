#include "runtime/selection.h"

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
static const native_parameter_definition INPUTS[] = { NATIVE_SELECT_INPUTS(PORT) };
static const native_parameter_definition OUTPUTS[] = { NATIVE_SELECT_OUTPUTS(PORT) };
static const native_parameter_definition SWITCH_OUTPUTS[] = { NATIVE_SWITCH_OUTPUTS(PORT) };
#define INPUTS(KEY, NAME, SOURCE, PORTS, KERNEL)                               \
    static const native_parameter_definition KEY##_inputs[] = { PORTS(PORT) };
NATIVE_SWITCH_OPERATIONS(INPUTS)
#undef INPUTS
#undef PORT

#define SWITCH(KEY, NAME, SOURCE, PORTS, KERNEL)                               \
    { .canonical_name = NAME,                                                  \
      NATIVE_SWITCH_##KEY##_PRESENTATION,                                      \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .inputs = KEY##_inputs,                                                  \
      .input_count = sizeof KEY##_inputs / sizeof *KEY##_inputs,               \
      .outputs = SWITCH_OUTPUTS,                                               \
      .output_count = sizeof SWITCH_OUTPUTS / sizeof *SWITCH_OUTPUTS,          \
      .completion_output = -1 },

static const native_capability_definition DEFINITIONS[] = {
    NATIVE_SWITCH_OPERATIONS(SWITCH){ .canonical_name = NATIVE_SELECT_NAME,
                                      NATIVE_SELECT_PRESENTATION,
                                      .source_name = NATIVE_SELECT_SOURCE,
                                      .kind = SCRIPT_CAPABILITY_FUNCTION,
                                      .pure = true,
                                      .inputs = INPUTS,
                                      .input_count = sizeof INPUTS / sizeof *INPUTS,
                                      .outputs = OUTPUTS,
                                      .output_count = sizeof OUTPUTS / sizeof *OUTPUTS,
                                      .completion_output = -1 }
};
#undef SWITCH
#define BINDING(KEY, NAME, SOURCE, PORTS, KERNEL)                              \
    { .canonical_name = NAME,                                                  \
      .implementation = { .call = &NATIVE_SWITCH_##KERNEL##_CALL,              \
                          .access = &NATIVE_SELECTION_ACCESS } },
static const native_definition_binding BINDINGS[] = { NATIVE_SWITCH_OPERATIONS(BINDING){
    .canonical_name = NATIVE_SELECT_NAME,
    .implementation = { .call = &NATIVE_SELECT_CALL,
                        .access = &NATIVE_SELECTION_ACCESS } } };
#undef BINDING
const native_definition_pack NATIVE_SELECTION_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = sizeof DEFINITIONS / sizeof *DEFINITIONS,
    .bindings = BINDINGS,
    .binding_count = sizeof BINDINGS / sizeof *BINDINGS,
};
