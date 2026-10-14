#include "runtime/rhythm_definition.h"

#include "runtime/rhythm.h"
static const native_parameter_definition OUTPUTS[] = { RHYTHM_OUTPUTS(NATIVE_PORT) };
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = RHYTHM_NAME,
      RHYTHM_PRESENTATION,
      .source_name = "scene_rhythm",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .outputs = OUTPUTS,
      .output_count = NATIVE_COUNT(OUTPUTS),
      .completion_output = -1 }
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = RHYTHM_NAME,
      .implementation = { .call = &RHYTHM_CALL, .access = &RHYTHM_ACCESS } }
};
const native_definition_pack RHYTHM_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
