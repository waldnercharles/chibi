#include "runtime/behaviors/scene_flow_definition.h"

#include "runtime/behaviors/scene_flow.h"

#define PORT(NAME, KIND, STORAGE, TYPE_NAME, CONTAINER, FLAGS, GROUP, REFERENCE, OWNER, VALUE) \
    { .name = NAME,                                                                            \
      .kind = KIND,                                                                            \
      .storage = STORAGE,                                                                      \
      .type_name = TYPE_NAME,                                                                  \
      .container = CONTAINER,                                                                  \
      .type_flags = FLAGS,                                                                     \
      .generic_group = GROUP,                                                                  \
      .reference_kind = REFERENCE,                                                             \
      .has_default = true,                                                                     \
      .default_owner = OWNER,                                                                  \
      .default_value = VALUE },
static const native_parameter_definition LOAD_IN[] = { SCENE_LOAD_INPUTS(NATIVE_CSTRING_PORT) };
static const native_parameter_definition EXEC[] = { SCENE_EXEC(PORT) };
static const native_parameter_definition THEN[] = { SCENE_THEN(PORT) };
static const native_parameter_definition CURRENT_OUT[] = { SCENE_CURRENT_OUTPUTS(PORT) };
#undef PORT
#define COUNT(ARRAY) ((int)(sizeof ARRAY / sizeof *ARRAY))
#define ACTION(KEY, NAME, SOURCE, INPUTS)                                      \
    { .canonical_name = NAME,                                                  \
      KEY##_PRESENTATION,                                                      \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .inputs = INPUTS,                                                        \
      .input_count = COUNT(INPUTS),                                            \
      .outputs = THEN,                                                         \
      .output_count = COUNT(THEN),                                             \
      .completion_output = 0,                                                  \
      .effects = SCRIPT_EFFECT_HOST }
static const native_capability_definition DEFINITIONS[] = {
    ACTION(SCENE_LOAD, SCENE_LOAD_NAME, "load_scene", LOAD_IN),
    ACTION(SCENE_RELOAD, SCENE_RELOAD_NAME, "reload_scene", EXEC),
    { .canonical_name = SCENE_CURRENT_NAME,
      SCENE_CURRENT_PRESENTATION,
      .source_name = "current_scene",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .outputs = CURRENT_OUT,
      .output_count = COUNT(CURRENT_OUT),
      .completion_output = -1,
      .effects = SCRIPT_EFFECT_READ },
};
#define BIND(NAME, CALL, ACCESS)                                               \
    {                                                                          \
        .canonical_name = NAME, .implementation = {                            \
            .call = &CALL,                                                     \
            .access = &ACCESS                                                  \
        }                                                                      \
    }
static const native_definition_binding BINDINGS[] = {
    BIND(SCENE_LOAD_NAME, SCENE_LOAD_CALL, SCENE_REQUEST_ACCESS),
    BIND(SCENE_RELOAD_NAME, SCENE_RELOAD_CALL, SCENE_REQUEST_ACCESS),
    BIND(SCENE_CURRENT_NAME, SCENE_CURRENT_CALL, SCENE_READ_ACCESS),
};
const native_definition_pack SCENE_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS),
};
