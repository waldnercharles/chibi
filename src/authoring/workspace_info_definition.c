#include "authoring/workspace_info_definition.h"
#define DATA(N, T, S)                                                          \
    { .name = N, .kind = NATIVE_PARAMETER_DATA, .storage = T, .type_name = S }
static const native_parameter_definition QUERY_OUT[] = {
    DATA("Ok", PIN_BOOL, "bool"),
    DATA("Root", PIN_STR, "const char *"),
    DATA("Name", PIN_STR, "const char *"),
    DATA("Content", PIN_STR, "const char *"),
    DATA("StartScene", PIN_STR, "const char *"),
    DATA("Version", PIN_I32, "int"),
    DATA("Error", PIN_STR, "const char *"),
    DATA("Active", PIN_BOOL, "bool"),
    DATA("DocumentsRestored", PIN_BOOL, "bool"),
    DATA("PendingRoot", PIN_STR, "const char *"),
    DATA("PendingVersion", PIN_I32, "int")
};
static const native_parameter_definition REFRESH_IN[] = {
    { .name = "Exec", .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW }
};
static const native_parameter_definition REFRESH_OUT[] = {
    { .name = "Then", .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW },
    DATA("Ok", PIN_BOOL, "bool"),
    DATA("Error", PIN_STR, "const char *")
};
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = "Authoring.query_workspace_info",
      .source_name = "query_workspace_info",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .outputs = QUERY_OUT,
      .output_count = NATIVE_COUNT(QUERY_OUT),
      .completion_output = -1 },
    { .canonical_name = "Authoring.refresh_workspace_info",
      .source_name = "refresh_workspace_info",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = REFRESH_IN,
      .input_count = NATIVE_COUNT(REFRESH_IN),
      .outputs = REFRESH_OUT,
      .output_count = NATIVE_COUNT(REFRESH_OUT),
      .completion_output = 0 },
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Authoring.query_workspace_info",
      .implementation = { .call = &WORKSPACE_INFO_CALLS[0],
                          .access = &WORKSPACE_INFO_READ_ACCESS } },
    { .canonical_name = "Authoring.refresh_workspace_info",
      .implementation = { .call = &WORKSPACE_INFO_CALLS[1],
                          .access = &WORKSPACE_INFO_WRITE_ACCESS } },
};
const native_definition_pack WORKSPACE_INFO_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS)
};
