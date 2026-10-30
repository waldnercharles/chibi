#include "runtime/materials_definition.h"

#include "runtime/materials_native.h"

static const native_parameter_definition DamageMaterial_IN[] = {
    MATERIAL_DamageMaterial_INPUTS(NATIVE_PORT)
};
static const native_parameter_definition DamageMaterial_OUT[] = {
    MATERIAL_DamageMaterial_OUTPUTS(NATIVE_PORT)
};
static const native_parameter_definition ReleaseMaterial_IN[] = {
    MATERIAL_ReleaseMaterial_INPUTS(NATIVE_PORT)
};
static const native_parameter_definition ReleaseMaterial_OUT[] = {
    MATERIAL_ReleaseMaterial_OUTPUTS(NATIVE_PORT)
};
static const native_parameter_definition IsMaterialFragment_IN[] = {
    MATERIAL_IsMaterialFragment_INPUTS(NATIVE_PORT)
};
static const native_parameter_definition IsMaterialFragment_OUT[] = {
    MATERIAL_IsMaterialFragment_OUTPUTS(NATIVE_PORT)
};
static const native_parameter_definition IsPierceableMaterialFragment_IN[] = {
    MATERIAL_IsPierceableMaterialFragment_INPUTS(NATIVE_PORT)
};
static const native_parameter_definition IsPierceableMaterialFragment_OUT[] = {
    MATERIAL_IsPierceableMaterialFragment_OUTPUTS(NATIVE_PORT)
};
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = MATERIAL_DamageMaterial_NAME,
      MATERIAL_DamageMaterial_PRESENTATION,
      .source_name = MATERIAL_DamageMaterial_SOURCE,
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = DamageMaterial_IN,
      .input_count = NATIVE_COUNT(DamageMaterial_IN),
      .outputs = DamageMaterial_OUT,
      .output_count = NATIVE_COUNT(DamageMaterial_OUT),
      .completion_output = 0,
      .pure = false },
    { .canonical_name = MATERIAL_ReleaseMaterial_NAME,
      MATERIAL_ReleaseMaterial_PRESENTATION,
      .source_name = MATERIAL_ReleaseMaterial_SOURCE,
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = ReleaseMaterial_IN,
      .input_count = NATIVE_COUNT(ReleaseMaterial_IN),
      .outputs = ReleaseMaterial_OUT,
      .output_count = NATIVE_COUNT(ReleaseMaterial_OUT),
      .completion_output = 0,
      .pure = false },
    { .canonical_name = MATERIAL_IsMaterialFragment_NAME,
      MATERIAL_IsMaterialFragment_PRESENTATION,
      .source_name = MATERIAL_IsMaterialFragment_SOURCE,
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = IsMaterialFragment_IN,
      .input_count = NATIVE_COUNT(IsMaterialFragment_IN),
      .outputs = IsMaterialFragment_OUT,
      .output_count = NATIVE_COUNT(IsMaterialFragment_OUT),
      .completion_output = -1,
      .pure = true },
    { .canonical_name = MATERIAL_IsPierceableMaterialFragment_NAME,
      MATERIAL_IsPierceableMaterialFragment_PRESENTATION,
      .source_name = MATERIAL_IsPierceableMaterialFragment_SOURCE,
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = IsPierceableMaterialFragment_IN,
      .input_count = NATIVE_COUNT(IsPierceableMaterialFragment_IN),
      .outputs = IsPierceableMaterialFragment_OUT,
      .output_count = NATIVE_COUNT(IsPierceableMaterialFragment_OUT),
      .completion_output = -1,
      .pure = true },
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = MATERIAL_DamageMaterial_NAME,
      .implementation = { .call = &MATERIAL_DAMAGE_CALL, .access = &MATERIAL_WRITE_ACCESS } },
    { .canonical_name = MATERIAL_ReleaseMaterial_NAME,
      .implementation = { .call = &MATERIAL_RELEASE_CALL,
                          .access = &MATERIAL_WRITE_ACCESS } },
    { .canonical_name = MATERIAL_IsMaterialFragment_NAME,
      .implementation = { .call = &MATERIAL_FRAGMENT_CALL,
                          .access = &MATERIAL_READ_ACCESS } },
    { .canonical_name = MATERIAL_IsPierceableMaterialFragment_NAME,
      .implementation = { .call = &MATERIAL_PIERCEABLE_CALL,
                          .access = &MATERIAL_READ_ACCESS } },
};
const native_definition_pack MATERIAL_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS)
};
