#include "runtime/visibility_definition.h"

#include "runtime/visibility.h"

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
static const native_parameter_definition INPUTS[] = { VISIBILITY_SAMPLE_INPUTS(PORT) };
static const native_parameter_definition OUTPUTS[] = { VISIBILITY_SAMPLE_OUTPUTS(PORT) };
#undef PORT
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = VISIBILITY_SAMPLE_NAME,
      VISIBILITY_SAMPLE_PRESENTATION,
      .source_name = VISIBILITY_SAMPLE_SOURCE,
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .effects = SCRIPT_EFFECT_READ,
      .completion_output = -1,
      .inputs = INPUTS,
      .input_count = sizeof INPUTS / sizeof *INPUTS,
      .outputs = OUTPUTS,
      .output_count = sizeof OUTPUTS / sizeof *OUTPUTS },
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = VISIBILITY_SAMPLE_NAME,
      .implementation = { .call = &GAME_VISIBILITY_SAMPLE_CALL,
                          .access = &GAME_VISIBILITY_READ_ACCESS } },
};
const native_definition_pack GAME_VISIBILITY_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = sizeof DEFINITIONS / sizeof *DEFINITIONS,
    .bindings = BINDINGS,
    .binding_count = sizeof BINDINGS / sizeof *BINDINGS,
};
