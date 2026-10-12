#include "runtime/input_definition.h"

#include "runtime/engine_input.h"

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
static const native_parameter_definition INPUTS[] = { INPUT_ACTION_INPUTS(PORT) };
#define ARRAYS(KEY, NAME, OUTPUTS)                                             \
    static const native_parameter_definition KEY##_outputs[] = { OUTPUTS(PORT) };
INPUT_OPERATIONS(ARRAYS)
#undef ARRAYS
static const native_parameter_definition CONTROLLER_INPUTS[] = {
    INPUT_CONTROLLER_CONNECTED_INPUTS(PORT)
};
static const native_parameter_definition CONTROLLER_OUTPUTS[] = {
    INPUT_CONTROLLER_CONNECTED_OUTPUTS(PORT)
};
static const native_parameter_definition POINTER_OUTPUTS[] = {
    INPUT_POINTER_RAY_OUTPUTS(PORT)
};
static const native_parameter_definition DEVICE_OUTPUTS[] = {
    INPUT_DEVICE_STATE_OUTPUTS(PORT)
};
static const native_parameter_definition DEVICE_ACTION_INPUTS[] = {
    INPUT_DEVICE_INPUTS(PORT)
};
static const native_parameter_definition DEVICE_ACTION_OUTPUTS[] = {
    INPUT_DEVICE_OUTPUTS(PORT)
};
static const native_parameter_definition VIBRATE_INPUTS[] = { INPUT_VIBRATE_INPUTS(PORT) };
#undef PORT
#define COUNT(A) (sizeof(A) / sizeof *(A))
#define DEFINITION(KEY, NAME, OUTPUTS)                                         \
    { .canonical_name = NAME,                                                  \
      INPUT_##KEY##_PRESENTATION,                                              \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                      \
      .pure = true,                                                            \
      .effects = SCRIPT_EFFECT_READ,                                           \
      .completion_output = -1,                                                 \
      .inputs = INPUTS,                                                        \
      .input_count = COUNT(INPUTS),                                            \
      .outputs = KEY##_outputs,                                                \
      .output_count = COUNT(KEY##_outputs) },
#define DEVICE_ACTION(KEY, SOURCE, INPUTS)                                     \
    { .canonical_name = INPUT_##KEY##_NAME,                                    \
      .source_name = SOURCE,                                                   \
      INPUT_##KEY##_PRESENTATION,                                              \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .effects = SCRIPT_EFFECT_HOST,                                           \
      .inputs = INPUTS,                                                        \
      .input_count = COUNT(INPUTS),                                            \
      .outputs = DEVICE_ACTION_OUTPUTS,                                        \
      .output_count = COUNT(DEVICE_ACTION_OUTPUTS),                            \
      .completion_output = 0 }
static const native_capability_definition DEFINITIONS[] = {
    INPUT_OPERATIONS(DEFINITION){ .canonical_name = INPUT_CONTROLLER_CONNECTED_NAME,
                                  .source_name = "controller_connected",
                                  INPUT_CONTROLLER_CONNECTED_PRESENTATION,
                                  .kind = SCRIPT_CAPABILITY_FUNCTION,
                                  .pure = true,
                                  .effects = SCRIPT_EFFECT_READ,
                                  .completion_output = -1,
                                  .inputs = CONTROLLER_INPUTS,
                                  .input_count = COUNT(CONTROLLER_INPUTS),
                                  .outputs = CONTROLLER_OUTPUTS,
                                  .output_count = COUNT(CONTROLLER_OUTPUTS) },
    { .canonical_name = INPUT_POINTER_RAY_NAME,
      .source_name = "get_pointer_ray",
      INPUT_POINTER_RAY_PRESENTATION,
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .effects = SCRIPT_EFFECT_READ,
      .completion_output = -1,
      .outputs = POINTER_OUTPUTS,
      .output_count = COUNT(POINTER_OUTPUTS) },
    { .canonical_name = INPUT_DEVICE_STATE_NAME,
      .source_name = "get_device_state",
      INPUT_DEVICE_STATE_PRESENTATION,
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .effects = SCRIPT_EFFECT_READ,
      .completion_output = -1,
      .outputs = DEVICE_OUTPUTS,
      .output_count = COUNT(DEVICE_OUTPUTS) },
    DEVICE_ACTION(REQUEST_MOTION, "request_device_motion", DEVICE_ACTION_INPUTS),
    DEVICE_ACTION(DISABLE_MOTION, "disable_device_motion", DEVICE_ACTION_INPUTS),
    DEVICE_ACTION(RECENTER_MOTION, "recenter_device_motion", DEVICE_ACTION_INPUTS),
    DEVICE_ACTION(VIBRATE, "vibrate_device", VIBRATE_INPUTS),
};
#undef DEFINITION
#undef DEVICE_ACTION
#define BINDING(KEY, NAME, OUTPUTS)                                            \
    { .canonical_name = NAME,                                                  \
      .implementation = { .call = &INPUT_##KEY##_CALL, .access = &INPUT_READ_ACCESS } },
static const native_definition_binding BINDINGS[] = {
    INPUT_OPERATIONS(BINDING){ .canonical_name = INPUT_CONTROLLER_CONNECTED_NAME,
                               .implementation = { .call = &INPUT_CONTROLLER_CONNECTED_CALL,
                                                   .access = &INPUT_READ_ACCESS } },
    { .canonical_name = INPUT_POINTER_RAY_NAME,
      .implementation = { .call = &INPUT_POINTER_RAY_CALL, .access = &INPUT_READ_ACCESS } },
    { .canonical_name = INPUT_DEVICE_STATE_NAME,
      .implementation = { .call = &INPUT_DEVICE_STATE_CALL, .access = &INPUT_READ_ACCESS } },
    { .canonical_name = INPUT_REQUEST_MOTION_NAME,
      .implementation = { .call = &INPUT_REQUEST_MOTION_CALL,
                          .access = &INPUT_DEVICE_ACCESS } },
    { .canonical_name = INPUT_DISABLE_MOTION_NAME,
      .implementation = { .call = &INPUT_DISABLE_MOTION_CALL,
                          .access = &INPUT_DEVICE_ACCESS } },
    { .canonical_name = INPUT_RECENTER_MOTION_NAME,
      .implementation = { .call = &INPUT_RECENTER_MOTION_CALL,
                          .access = &INPUT_DEVICE_ACCESS } },
    { .canonical_name = INPUT_VIBRATE_NAME,
      .implementation = { .call = &INPUT_VIBRATE_CALL, .access = &INPUT_DEVICE_ACCESS } },
};
#undef BINDING
const native_definition_pack INPUT_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS),
};
#undef COUNT
