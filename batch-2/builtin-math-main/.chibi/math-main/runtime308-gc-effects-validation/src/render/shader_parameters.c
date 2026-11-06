#include "render/shader_parameters.h"

#include "render/shader_catalog_internal.h"
#include "runtime/component_catalog.h"
static const char *material(void *world, const pin_value *inputs, pin_value *)
{
    return shader_catalog_write_parameter_value(world, inputs, false);
}
static const char *shape(void *world, const pin_value *inputs, pin_value *)
{
    return shader_catalog_write_parameter_value(world, inputs, true);
}
const node_host_call RENDER_MATERIAL_PARAMETER_CALL = { .service = "ecs.world",
                                                        .invoke = material };
const node_host_call RENDER_SHAPE_PARAMETER_CALL = { .service = "ecs.world",
                                                     .invoke = shape };
const node_access RENDER_SHADER_PARAMETER_ACCESS = {
    .reads = &COMPONENT_Shader.id,
    .read_count = 1,
    .flags = NODE_EFFECT_PARALLEL | NODE_EFFECT_ENTITY_SCOPED | NODE_EFFECT_HOST_SINK,
    .target_input = 0
};
#define PORT(NAME, KIND, STORAGE, TYPE_NAME, FLAGS, GROUP, INITIAL, C_STRING)  \
    { .name = NAME,                                                            \
      .kind = KIND,                                                            \
      .storage = STORAGE,                                                      \
      .type_name = TYPE_NAME,                                                  \
      .type_flags = FLAGS,                                                     \
      .generic_group = GROUP,                                                  \
      .c_string = C_STRING,                                                    \
      .has_default = true,                                                     \
      .default_value = INITIAL },
static const native_parameter_definition INPUTS[] = { RENDER_SHADER_PARAMETER_INPUTS(PORT) };
static const native_parameter_definition OUTPUTS[] = {
    RENDER_SHADER_PARAMETER_OUTPUTS(PORT)
};
#undef PORT
#define COUNT(A) (sizeof(A) / sizeof *(A))
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = "Set Material Parameter",
      .source_name = "set_material_parameter",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = INPUTS,
      .input_count = COUNT(INPUTS),
      .outputs = OUTPUTS,
      .output_count = COUNT(OUTPUTS),
      .completion_output = 0 },
    { .canonical_name = "Set Shape Parameter",
      .source_name = "set_shape_parameter",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = INPUTS,
      .input_count = COUNT(INPUTS),
      .outputs = OUTPUTS,
      .output_count = COUNT(OUTPUTS),
      .completion_output = 0 }
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Set Material Parameter",
      .implementation = { .call = &RENDER_MATERIAL_PARAMETER_CALL,
                          .access = &RENDER_SHADER_PARAMETER_ACCESS } },
    { .canonical_name = "Set Shape Parameter",
      .implementation = { .call = &RENDER_SHAPE_PARAMETER_CALL,
                          .access = &RENDER_SHADER_PARAMETER_ACCESS } }
};
const native_definition_pack RENDER_SHADER_PARAMETER_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS)
};
