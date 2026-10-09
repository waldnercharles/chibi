#include "runtime/particles_definition.h"

#include "runtime/particles_native.h"

static const native_parameter_definition BURST[] = {
    GAME_PARTICLES_BURST_INPUTS(NATIVE_PORT_FLAGS)
};
static const native_parameter_definition DIRECTED[] = {
    GAME_PARTICLES_DIRECTED_INPUTS(NATIVE_PORT_FLAGS)
};
static const native_parameter_definition THEN[] = { GAME_PARTICLES_OUTPUTS(NATIVE_PORT_FLAGS) };
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = GAME_PARTICLES_BURST_NAME,
      GAME_PARTICLES_BURST_PRESENTATION,
      .source_name = "emit_particles",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = BURST,
      .input_count = NATIVE_COUNT(BURST),
      .outputs = THEN,
      .output_count = NATIVE_COUNT(THEN),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = GAME_PARTICLES_DIRECTED_NAME,
      GAME_PARTICLES_DIRECTED_PRESENTATION,
      .source_name = "emit_directed_particles",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = DIRECTED,
      .input_count = NATIVE_COUNT(DIRECTED),
      .outputs = THEN,
      .output_count = NATIVE_COUNT(THEN),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = GAME_PARTICLES_BURST_NAME,
      .implementation = { .call = &GAME_PARTICLES_BURST_CALL,
                          .access = &GAME_PARTICLES_ACCESS } },
    { .canonical_name = GAME_PARTICLES_DIRECTED_NAME,
      .implementation = { .call = &GAME_PARTICLES_DIRECTED_CALL,
                          .access = &GAME_PARTICLES_ACCESS } },
};
const native_definition_pack GAME_PARTICLES_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
