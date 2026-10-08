#include "runtime/materials_definition.h"

#include "runtime/materials_native.h"

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
#define COUNT(A) (sizeof(A) / sizeof *(A))
static const native_parameter_definition DamageMaterial_IN[] = {
    GAME_MATERIAL_DamageMaterial_INPUTS(PORT)
};
static const native_parameter_definition DamageMaterial_OUT[] = {
    GAME_MATERIAL_DamageMaterial_OUTPUTS(PORT)
};
static const native_parameter_definition ReleaseMaterial_IN[] = {
    GAME_MATERIAL_ReleaseMaterial_INPUTS(PORT)
};
static const native_parameter_definition ReleaseMaterial_OUT[] = {
    GAME_MATERIAL_ReleaseMaterial_OUTPUTS(PORT)
};
static const native_parameter_definition IsMaterialFragment_IN[] = {
    GAME_MATERIAL_IsMaterialFragment_INPUTS(PORT)
};
static const native_parameter_definition IsMaterialFragment_OUT[] = {
    GAME_MATERIAL_IsMaterialFragment_OUTPUTS(PORT)
};
static const native_parameter_definition IsPierceableMaterialFragment_IN[] = {
    GAME_MATERIAL_IsPierceableMaterialFragment_INPUTS(PORT)
};
static const native_parameter_definition IsPierceableMaterialFragment_OUT[] = {
    GAME_MATERIAL_IsPierceableMaterialFragment_OUTPUTS(PORT)
};
#undef PORT
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = GAME_MATERIAL_DamageMaterial_NAME,
      GAME_MATERIAL_DamageMaterial_PRESENTATION,
      .source_name = GAME_MATERIAL_DamageMaterial_SOURCE,
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = DamageMaterial_IN,
      .input_count = COUNT(DamageMaterial_IN),
      .outputs = DamageMaterial_OUT,
      .output_count = COUNT(DamageMaterial_OUT),
      .completion_output = 0,
      .pure = false },
    { .canonical_name = GAME_MATERIAL_ReleaseMaterial_NAME,
      GAME_MATERIAL_ReleaseMaterial_PRESENTATION,
      .source_name = GAME_MATERIAL_ReleaseMaterial_SOURCE,
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = ReleaseMaterial_IN,
      .input_count = COUNT(ReleaseMaterial_IN),
      .outputs = ReleaseMaterial_OUT,
      .output_count = COUNT(ReleaseMaterial_OUT),
      .completion_output = 0,
      .pure = false },
    { .canonical_name = GAME_MATERIAL_IsMaterialFragment_NAME,
      GAME_MATERIAL_IsMaterialFragment_PRESENTATION,
      .source_name = GAME_MATERIAL_IsMaterialFragment_SOURCE,
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = IsMaterialFragment_IN,
      .input_count = COUNT(IsMaterialFragment_IN),
      .outputs = IsMaterialFragment_OUT,
      .output_count = COUNT(IsMaterialFragment_OUT),
      .completion_output = -1,
      .pure = true },
    { .canonical_name = GAME_MATERIAL_IsPierceableMaterialFragment_NAME,
      GAME_MATERIAL_IsPierceableMaterialFragment_PRESENTATION,
      .source_name = GAME_MATERIAL_IsPierceableMaterialFragment_SOURCE,
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = IsPierceableMaterialFragment_IN,
      .input_count = COUNT(IsPierceableMaterialFragment_IN),
      .outputs = IsPierceableMaterialFragment_OUT,
      .output_count = COUNT(IsPierceableMaterialFragment_OUT),
      .completion_output = -1,
      .pure = true },
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = GAME_MATERIAL_DamageMaterial_NAME,
      .implementation = { .call = &GAME_MATERIAL_DAMAGE_CALL,
                          .access = &GAME_MATERIAL_WRITE_ACCESS } },
    { .canonical_name = GAME_MATERIAL_ReleaseMaterial_NAME,
      .implementation = { .call = &GAME_MATERIAL_RELEASE_CALL,
                          .access = &GAME_MATERIAL_WRITE_ACCESS } },
    { .canonical_name = GAME_MATERIAL_IsMaterialFragment_NAME,
      .implementation = { .call = &GAME_MATERIAL_FRAGMENT_CALL,
                          .access = &GAME_MATERIAL_READ_ACCESS } },
    { .canonical_name = GAME_MATERIAL_IsPierceableMaterialFragment_NAME,
      .implementation = { .call = &GAME_MATERIAL_PIERCEABLE_CALL,
                          .access = &GAME_MATERIAL_READ_ACCESS } },
};
const native_definition_pack GAME_MATERIAL_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS)
};
