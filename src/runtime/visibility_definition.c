#include "runtime/visibility_definition.h"

#include "runtime/visibility.h"

static const native_parameter_definition INPUTS[] = { VISIBILITY_SAMPLE_INPUTS(NATIVE_PORT) };
static const native_parameter_definition OUTPUTS[] = { VISIBILITY_SAMPLE_OUTPUTS(NATIVE_PORT) };
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = VISIBILITY_SAMPLE_NAME,
      VISIBILITY_SAMPLE_PRESENTATION,
      .source_name = VISIBILITY_SAMPLE_SOURCE,
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .effects = SCRIPT_EFFECT_READ,
      .completion_output = -1,
      .inputs = INPUTS,
      .input_count = NATIVE_COUNT(INPUTS),
      .outputs = OUTPUTS,
      .output_count = NATIVE_COUNT(OUTPUTS) },
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = VISIBILITY_SAMPLE_NAME,
      .implementation = { .call = &VISIBILITY_SAMPLE_CALL,
                          .access = &VISIBILITY_READ_ACCESS } },
};
const native_definition_pack VISIBILITY_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
