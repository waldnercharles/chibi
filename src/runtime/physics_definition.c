#include "runtime/physics_definition.h"

#include "runtime/physics_native.h"

#define PORT(NAME, KIND, TYPE, NOMINAL, CONTAINER, FLAGS, GROUP, REFERENCE, OWNER, VALUE) \
    { .name = NAME,                                                                       \
      .kind = KIND,                                                                       \
      .storage = TYPE,                                                                    \
      .type_name = NOMINAL,                                                               \
      .container = CONTAINER,                                                             \
      .type_flags = FLAGS,                                                                \
      .generic_group = GROUP,                                                             \
      .reference_kind = REFERENCE,                                                        \
      .has_default = true,                                                                \
      .default_owner = OWNER,                                                             \
      .default_value = VALUE },
static const native_parameter_definition RAY_IN[] = { PHYSICS_RAY_INPUTS(PORT) };
static const native_parameter_definition RAY_OUT[] = { PHYSICS_RAY_OUTPUTS(PORT) };
static const native_parameter_definition CONTACT_IN[] = { PHYSICS_CONTACT_INPUTS(PORT) };
static const native_parameter_definition CONTACT_OUT[] = { PHYSICS_CONTACT_OUTPUTS(PORT) };
#undef PORT
#define COUNT(ARRAY) ((int)(sizeof ARRAY / sizeof *ARRAY))
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = PHYSICS_RAY_NAME,
      PHYSICS_RAY_PRESENTATION,
      .source_name = PHYSICS_RAY_SOURCE,
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .completion_output = -1,
      .inputs = RAY_IN,
      .input_count = COUNT(RAY_IN),
      .outputs = RAY_OUT,
      .output_count = COUNT(RAY_OUT) },
    { .canonical_name = PHYSICS_CONTACT_NAME,
      PHYSICS_CONTACT_PRESENTATION,
      .source_name = PHYSICS_CONTACT_SOURCE,
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .completion_output = -1,
      .inputs = CONTACT_IN,
      .input_count = COUNT(CONTACT_IN),
      .outputs = CONTACT_OUT,
      .output_count = COUNT(CONTACT_OUT) },
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = PHYSICS_RAY_NAME,
      .implementation = { .call = &GAME_PHYSICS_RAYCAST_CALL,
                          .access = &GAME_PHYSICS_READ_ACCESS } },
    { .canonical_name = PHYSICS_CONTACT_NAME,
      .implementation = { .call = &GAME_PHYSICS_CONTACTS_CALL,
                          .access = &GAME_PHYSICS_READ_ACCESS } },
};
const native_definition_pack GAME_PHYSICS_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS),
};
