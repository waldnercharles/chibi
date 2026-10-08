#include "runtime/particles_definition.h"

#include "runtime/particles_native.h"

#define PORT(NAME, KIND, STORAGE, TYPE_NAME, CONTAINER, FLAGS, GROUP, REFERENCE, OWNER, VALUE, GRAPH_FLAGS) \
    { .name = NAME,                                                                                         \
      .kind = KIND,                                                                                         \
      .storage = STORAGE,                                                                                   \
      .type_name = TYPE_NAME,                                                                               \
      .container = CONTAINER,                                                                               \
      .type_flags = FLAGS,                                                                                  \
      .generic_group = GROUP,                                                                               \
      .reference_kind = REFERENCE,                                                                          \
      .has_default = true,                                                                                  \
      .default_owner = OWNER,                                                                               \
      .default_value = VALUE },
static const native_parameter_definition BURST[] = { GAME_PARTICLES_BURST_INPUTS(PORT) };
static const native_parameter_definition DIRECTED[] = {
    GAME_PARTICLES_DIRECTED_INPUTS(PORT)
};
static const native_parameter_definition THEN[] = { GAME_PARTICLES_OUTPUTS(PORT) };
#undef PORT
#define COUNT(ARRAY) ((int)(sizeof ARRAY / sizeof *ARRAY))
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = GAME_PARTICLES_BURST_NAME,
      GAME_PARTICLES_BURST_PRESENTATION,
      .source_name = "emit_particles",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = BURST,
      .input_count = COUNT(BURST),
      .outputs = THEN,
      .output_count = COUNT(THEN),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = GAME_PARTICLES_DIRECTED_NAME,
      GAME_PARTICLES_DIRECTED_PRESENTATION,
      .source_name = "emit_directed_particles",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = DIRECTED,
      .input_count = COUNT(DIRECTED),
      .outputs = THEN,
      .output_count = COUNT(THEN),
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
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS),
};
