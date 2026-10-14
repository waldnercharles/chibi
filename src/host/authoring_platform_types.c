#include "host/authoring_platform_definition.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>
#define FIELD(T, F, N, P, S)                                                   \
    { .name = N,                                                               \
      .type = P,                                                               \
      .type_name = S,                                                          \
      .offset = offsetof(T, F),                                                \
      .size = sizeof(((T *)0)->F) }
#define R(F, N, P, S) FIELD(authoring_platform_result, F, N, P, S)
static const type_field RESULT_FIELDS[] = {
    R(ok, "Ok", PIN_BOOL, "bool"),
    R(request, "Request", PIN_I32, "int"),
    R(state, "State", PIN_STR, "const char *"),
    R(status, "Status", PIN_STR, "const char *"),
    R(message, "Message", PIN_STR, "const char *"),
    R(value, "Value", PIN_STR, "const char *"),
    R(log, "Log", PIN_STR, "const char *"),
    R(progress, "Progress", PIN_F64, "double"),
    R(exit_code, "ExitCode", PIN_I32, "int")
};
#define E(F, N, P, S) FIELD(authoring_platform_environment, F, N, P, S)
static const type_field ENV_FIELDS[] = {
    E(native, "Native", PIN_BOOL, "bool"),
    E(focused, "Focused", PIN_BOOL, "bool"),
    E(web, "Web", PIN_BOOL, "bool"),
    E(clipboard, "Clipboard", PIN_BOOL, "bool"),
    E(file_picker, "FilePicker", PIN_BOOL, "bool"),
    E(folder_picker, "FolderPicker", PIN_BOOL, "bool"),
    E(compile, "Compile", PIN_BOOL, "bool"),
    E(export, "Export", PIN_BOOL, "bool"),
    E(platform, "Platform", PIN_STR, "const char *"),
    E(theme, "Theme", PIN_STR, "const char *"),
    E(width, "Width", PIN_I32, "int"),
    E(height, "Height", PIN_I32, "int"),
    E(close_request, "CloseRequest", PIN_I32, "int"),
    E(scale, "Scale", PIN_F64, "double")
};
const type_desc AUTHORING_PLATFORM_RESULT_LAYOUT = {
    .name = "PlatformRequestResult",
    .size = sizeof(authoring_platform_result),
    .fields = RESULT_FIELDS,
    .field_count = sizeof RESULT_FIELDS / sizeof *RESULT_FIELDS
};
const type_desc AUTHORING_PLATFORM_ENVIRONMENT_LAYOUT = {
    .name = "PlatformEnvironment",
    .size = sizeof(authoring_platform_environment),
    .fields = ENV_FIELDS,
    .field_count = sizeof ENV_FIELDS / sizeof *ENV_FIELDS
};
static const type_field CONTROLLER_FIELDS[] = {
    FIELD(authoring_physical_controller, slot, "Slot", PIN_I32, "int"),
    FIELD(authoring_physical_controller, buttons, "Buttons", PIN_I32, "int"),
    FIELD(authoring_physical_controller, connected, "Connected", PIN_BOOL, "bool"),
    FIELD(authoring_physical_controller, left_x, "LeftX", PIN_F64, "double"),
    FIELD(authoring_physical_controller, left_y, "LeftY", PIN_F64, "double"),
    FIELD(authoring_physical_controller, right_x, "RightX", PIN_F64, "double"),
    FIELD(authoring_physical_controller, right_y, "RightY", PIN_F64, "double"),
    FIELD(authoring_physical_controller, left_trigger, "LeftTrigger", PIN_F64, "double"),
    FIELD(authoring_physical_controller, right_trigger, "RightTrigger", PIN_F64, "double")
};
const type_desc AUTHORING_PHYSICAL_CONTROLLER_LAYOUT = {
    .name = "PhysicalControllerState",
    .size = sizeof(authoring_physical_controller),
    .fields = CONTROLLER_FIELDS,
    .field_count = sizeof CONTROLLER_FIELDS / sizeof *CONTROLLER_FIELDS
};
static const type_desc *const TYPES[] = { &AUTHORING_PLATFORM_RESULT_LAYOUT,
                                          &AUTHORING_PLATFORM_ENVIRONMENT_LAYOUT,
                                          &AUTHORING_PHYSICAL_CONTROLLER_LAYOUT };
const type_catalog_pack AUTHORING_PLATFORM_TYPE_PACK = {
    .types = TYPES,
    .type_count = sizeof TYPES / sizeof *TYPES
};
const native_type_definition AUTHORING_PLATFORM_TYPE_DEFINITIONS[3] = {
    { .layout = &AUTHORING_PLATFORM_RESULT_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &AUTHORING_PLATFORM_ENVIRONMENT_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &AUTHORING_PHYSICAL_CONTROLLER_LAYOUT, .storage = PIN_STRUCT }
};
