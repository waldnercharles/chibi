#include "runtime/physics3d_definition.h"

#include "runtime/physics3d_native.h"

static const native_parameter_definition RAY_IN[] = { PHYSICS3D_RAY_INPUTS(NATIVE_PORT) };
static const native_parameter_definition CAST_IN[] = { PHYSICS3D_CAST_INPUTS(NATIVE_PORT) };
static const native_parameter_definition HIT_OUT[] = { PHYSICS3D_HIT_OUTPUTS(NATIVE_PORT) };
static const native_parameter_definition CONTACT_IN[] = {
    PHYSICS3D_CONTACT_INPUTS(NATIVE_PORT)
};
static const native_parameter_definition CONTACT_OUT[] = {
    PHYSICS3D_CONTACT_OUTPUTS(NATIVE_PORT)
};
static const native_parameter_definition IMPULSE_IN[] = {
    PHYSICS3D_PUSH_INPUTS(NATIVE_PORT, "Impulse")
};
static const native_parameter_definition FORCE_IN[] = {
    PHYSICS3D_PUSH_INPUTS(NATIVE_PORT, "Force")
};
static const native_parameter_definition THEN[] = { PHYSICS3D_PUSH_OUTPUTS(NATIVE_PORT) };
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = PHYSICS3D_RAY_NAME,
      PHYSICS3D_RAY_PRESENTATION,
      .source_name = PHYSICS3D_RAY_SOURCE,
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .completion_output = -1,
      .inputs = RAY_IN,
      .input_count = NATIVE_COUNT(RAY_IN),
      .outputs = HIT_OUT,
      .output_count = NATIVE_COUNT(HIT_OUT) },
    { .canonical_name = PHYSICS3D_CAST_NAME,
      PHYSICS3D_CAST_PRESENTATION,
      .source_name = PHYSICS3D_CAST_SOURCE,
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .completion_output = -1,
      .inputs = CAST_IN,
      .input_count = NATIVE_COUNT(CAST_IN),
      .outputs = HIT_OUT,
      .output_count = NATIVE_COUNT(HIT_OUT) },
    { .canonical_name = PHYSICS3D_CONTACT_NAME,
      PHYSICS3D_CONTACT_PRESENTATION,
      .source_name = PHYSICS3D_CONTACT_SOURCE,
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .completion_output = -1,
      .inputs = CONTACT_IN,
      .input_count = NATIVE_COUNT(CONTACT_IN),
      .outputs = CONTACT_OUT,
      .output_count = NATIVE_COUNT(CONTACT_OUT) },
    { .canonical_name = PHYSICS3D_IMPULSE_NAME,
      PHYSICS3D_IMPULSE_PRESENTATION,
      .source_name = PHYSICS3D_IMPULSE_SOURCE,
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = IMPULSE_IN,
      .input_count = NATIVE_COUNT(IMPULSE_IN),
      .outputs = THEN,
      .output_count = NATIVE_COUNT(THEN),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = PHYSICS3D_FORCE_NAME,
      PHYSICS3D_FORCE_PRESENTATION,
      .source_name = PHYSICS3D_FORCE_SOURCE,
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = FORCE_IN,
      .input_count = NATIVE_COUNT(FORCE_IN),
      .outputs = THEN,
      .output_count = NATIVE_COUNT(THEN),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = PHYSICS3D_RAY_NAME,
      .implementation = { .call = &PHYSICS3D_RAYCAST_CALL,
                          .access = &PHYSICS3D_READ_ACCESS } },
    { .canonical_name = PHYSICS3D_CAST_NAME,
      .implementation = { .call = &PHYSICS3D_CAST_CALL, .access = &PHYSICS3D_READ_ACCESS } },
    { .canonical_name = PHYSICS3D_CONTACT_NAME,
      .implementation = { .call = &PHYSICS3D_CONTACTS_CALL,
                          .access = &PHYSICS3D_READ_ACCESS } },
    { .canonical_name = PHYSICS3D_IMPULSE_NAME,
      .implementation = { .call = &PHYSICS3D_IMPULSE_CALL,
                          .access = &PHYSICS3D_PUSH_ACCESS } },
    { .canonical_name = PHYSICS3D_FORCE_NAME,
      .implementation = { .call = &PHYSICS3D_FORCE_CALL, .access = &PHYSICS3D_PUSH_ACCESS } },
};
const native_definition_pack PHYSICS3D_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
