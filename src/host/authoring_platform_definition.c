#include "host/authoring_platform_definition.h"

#include "execution/function.h"
#define DATA(NAME, STORAGE, TYPE, VALUE)                                       \
    { .name = NAME,                                                            \
      .kind = NATIVE_PARAMETER_DATA,                                           \
      .storage = STORAGE,                                                      \
      .type_name = TYPE,                                                       \
      .has_default = true,                                                     \
      .default_value = VALUE }
#define TEXT(NAME)                                                             \
    DATA(NAME, PIN_STR, "const char *", ((pin_value){ .type = PIN_STR, .str = "" }))
#define NUMBER(NAME)                                                           \
    DATA(NAME, PIN_F64, "double", ((pin_value){ .type = PIN_F64 }))
#define INTEGER(NAME)                                                          \
    DATA(NAME, PIN_I32, "int", ((pin_value){ .type = PIN_I32 }))
#define EXEC(NAME)                                                             \
    { .name = NAME, .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW }
#define COUNT(A) ((int)(sizeof A / sizeof *A))
static const native_parameter_definition REQUEST_IN[] = {
    EXEC("Exec"),     TEXT("Operation"), TEXT("Value"),
    NUMBER("Number"), NUMBER("Other"),   INTEGER("Source")
};
static const native_parameter_definition HANDLE_IN[] = { EXEC("Exec"),
                                                         INTEGER("Request") };
static const native_parameter_definition NONE_IN[] = { EXEC("Exec") };
static const native_parameter_definition RESULT_OUT[] = {
    EXEC("Then"),
    DATA("Result", PIN_STRUCT, "PlatformRequestResult", ((pin_value){ .type = PIN_STRUCT }))
};
static const native_parameter_definition RELEASE_OUT[] = {
    EXEC("Then"),
    DATA("Released", PIN_BOOL, "bool", ((pin_value){ .type = PIN_BOOL }))
};
static const native_parameter_definition ENV_OUT[] = {
    EXEC("Then"),
    DATA("Environment", PIN_STRUCT, "PlatformEnvironment", ((pin_value){ .type = PIN_STRUCT }))
};
static const native_parameter_definition CONTROLLERS_OUT[] = {
    EXEC("Then"),
    { .name = "Rows",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STRUCT,
      .type_name = "PhysicalControllerState",
      .container = PIN_CONTAINER_ARRAY }
};
#define DEF(NAME, SOURCE, IN, OUT, DESC)                                       \
    { .canonical_name = NAME,                                                  \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .category = "Host",                                                      \
      .description = DESC,                                                     \
      .inputs = IN,                                                            \
      .input_count = COUNT(IN),                                                \
      .outputs = OUT,                                                          \
      .output_count = COUNT(OUT),                                              \
      .completion_output = 0,                                                  \
      .effects = SCRIPT_EFFECT_HOST }
const native_capability_definition AUTHORING_PLATFORM_DEFINITIONS[6] = {
    DEF("Request Authoring Platform", "authoring_platform_request", REQUEST_IN, RESULT_OUT, "Starts one fixed platform operation. Maximum eight retained requests. File/clipboard/browser operations may remain pending or be denied. compile/export use only host-selected tools; import-file copies a selected Source handle exclusively into workspace-relative Value. Browser import-project/export-project preserve project ZIP workflows."),
    DEF("Poll Authoring Platform", "authoring_platform_poll", HANDLE_IN, RESULT_OUT, "Pumps bounded asynchronous jobs and returns copied current request state, value and retained diagnostic log."),
    DEF("Cancel Authoring Platform", "authoring_platform_cancel", HANDLE_IN, RESULT_OUT, "Cancels a pending/running request. Native dialogs may stay visible; late results are ignored. Native job cancellation signals its process group and escalates after two seconds."),
    DEF("Release Authoring Platform", "authoring_platform_release", HANDLE_IN, RELEASE_OUT, "Releases a terminal request and its retained result; running requests must first be cancelled."),
    DEF("Query Physical Controllers", "query_physical_controllers", NONE_IN, CONTROLLERS_OUT, "Reads exactly four physical controller slots from the current host input frame. Buttons use PadControl bits; disconnected axes/buttons are zero. Stick axes are normalized [-1,1] without authored dead-zone/focus policy; triggers [0,1]. Does not drive simulation or record input."),
    DEF("Authoring Platform Environment", "authoring_platform_environment", NONE_IN, ENV_OUT, "Reports OS theme/window/scale and available native/browser backends. Availability does not grant browser permission or guarantee a dialog/download succeeds."),
};
static const type_desc *const TYPES[] = { &AUTHORING_PLATFORM_RESULT_LAYOUT,
                                          &AUTHORING_PLATFORM_ENVIRONMENT_LAYOUT,
                                          &AUTHORING_PHYSICAL_CONTROLLER_LAYOUT };
static const char *declaration_only(const behavior_function_context *, void *, const pin_value *, pin_value *)
{
    return "Platform operations require a runtime host adapter";
}
static const node_host_call DECLARATION_CALL = {
    .service = "authoring_platform",
    .missing_service = "Platform operations require a runtime host adapter",
    .context = declaration_only
};
static const node_access DECLARATION_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
#define DECLARE(NAME)                                                          \
    {                                                                          \
        .canonical_name = NAME, .implementation = {                            \
            .call = &DECLARATION_CALL,                                         \
            .access = &DECLARATION_ACCESS                                      \
        }                                                                      \
    }
static const native_definition_binding DECLARATION_BINDINGS[] = {
    DECLARE("Request Authoring Platform"),
    DECLARE("Poll Authoring Platform"),
    DECLARE("Cancel Authoring Platform"),
    DECLARE("Release Authoring Platform"),
    DECLARE("Authoring Platform Environment"),
    DECLARE("Query Physical Controllers")
};
const native_definition_pack AUTHORING_PLATFORM_DECLARATION_PACK = {
    .bindings = DECLARATION_BINDINGS,
    .binding_count = COUNT(DECLARATION_BINDINGS),
    .definitions = AUTHORING_PLATFORM_DEFINITIONS,
    .definition_count = 6,
    .layouts = TYPES,
    .layout_count = 3
};
