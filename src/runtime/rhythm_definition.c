#include "runtime/rhythm_definition.h"

#include "runtime/rhythm.h"
static const native_parameter_definition OUTPUTS[] = { GAME_RHYTHM_OUTPUTS(NATIVE_PORT) };
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = GAME_RHYTHM_NAME,
      GAME_RHYTHM_PRESENTATION,
      .source_name = "scene_rhythm",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .outputs = OUTPUTS,
      .output_count = NATIVE_COUNT(OUTPUTS),
      .completion_output = -1 }
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = GAME_RHYTHM_NAME,
      .implementation = { .call = &GAME_RHYTHM_CALL, .access = &GAME_RHYTHM_ACCESS } }
};
const native_definition_pack GAME_RHYTHM_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
