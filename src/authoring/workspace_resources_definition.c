#include "authoring/workspace_resources_definition.h"
#define PORT(NAME, STORAGE, TYPE, CONTAINER, VALUE)                            \
    { .name = NAME,                                                            \
      .kind = NATIVE_PARAMETER_DATA,                                           \
      .storage = STORAGE,                                                      \
      .type_name = TYPE,                                                       \
      .container = CONTAINER,                                                  \
      .has_default = true,                                                     \
      .default_value = VALUE }
#define INTEGER(NAME, VALUE)                                                   \
    PORT(NAME, PIN_I32, "int", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_I32, .i = VALUE }))
#define TEXT(NAME)                                                             \
    PORT(NAME, PIN_STR, "const char *", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_STR, .str = "" }))
#define EXEC(NAME)                                                             \
    { .name = NAME, .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW }
#define RESULT                                                                 \
    PORT(                                                                      \
        "Status",                                                              \
        PIN_STRUCT,                                                            \
        "WorkspaceResourceResult",                                             \
        PIN_CONTAINER_SINGLE,                                                  \
        ((pin_value){ .type = PIN_STRUCT })                                    \
    )
static const native_parameter_definition QUERY_IN[] = { EXEC("Exec"),
                                                        INTEGER("First", 0),
                                                        INTEGER("Count", 64),
                                                        TEXT("Kind"),
                                                        TEXT("Query") };
static const native_parameter_definition QUERY_OUT[] = {
    EXEC("Then"),
    RESULT,
    PORT(
        "Rows",
        PIN_STRUCT,
        "WorkspaceResourceRow",
        PIN_CONTAINER_ARRAY,
        ((pin_value){ .type = PIN_STRUCT, .container = PIN_CONTAINER_ARRAY })
    )
};
static const native_parameter_definition REFRESH_IN[] = { EXEC("Exec") };
static const native_parameter_definition REFRESH_OUT[] = { EXEC("Then"), RESULT };
static const native_parameter_definition LOOKUP_IN[] = { EXEC("Exec"), TEXT("Key") };
static const native_parameter_definition LOOKUP_OUT[] = {
    EXEC("Then"),
    RESULT,
    PORT("Row", PIN_STRUCT, "WorkspaceResourceRow", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_STRUCT }))
};
#define COUNT(A) ((int)(sizeof A / sizeof *A))
#define DEF(NAME, SOURCE, INPUTS, OUTPUTS, DESCRIPTION)                        \
    { .canonical_name = NAME,                                                  \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .category = "Workspace",                                                 \
      .description = DESCRIPTION,                                              \
      .inputs = INPUTS,                                                        \
      .input_count = COUNT(INPUTS),                                            \
      .outputs = OUTPUTS,                                                      \
      .output_count = COUNT(OUTPUTS),                                          \
      .completion_output = 0,                                                  \
      .effects = SCRIPT_EFFECT_HOST }
static const native_capability_definition DEFINITIONS[] = {
    DEF("Query Workspace Resources",
        "query_workspace_resources",
        QUERY_IN,
        QUERY_OUT,
        "Reads a sorted immutable inventory page; no file IO. First >= 0; "
        "Count 0..256. Kind is exact; Query searches names/paths with ASCII "
        "case folding. Status reports scan completeness and filtered total."),
    DEF("Refresh Workspace Resources", "refresh_workspace_resources", REFRESH_IN, REFRESH_OUT, "Explicit bounded disk snapshot rooted at the host workspace; never follows descendant symlinks. Returns typed status, revision and completeness. Does not mutate document buffers."),
    DEF("Lookup Workspace Resource",
        "lookup_workspace_resource",
        LOOKUP_IN,
        LOOKUP_OUT,
        "Reads one inventory row by its stable opaque Key, without file IO. "
        "Missing keys return NotFound status and an empty row."),
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Query Workspace Resources",
      .implementation = { .call = &WORKSPACE_RESOURCE_QUERY_CALL,
                          .access = &WORKSPACE_RESOURCE_READ_ACCESS } },
    { .canonical_name = "Refresh Workspace Resources",
      .implementation = { .call = &WORKSPACE_RESOURCE_REFRESH_CALL,
                          .access = &WORKSPACE_RESOURCE_REFRESH_ACCESS } },
    { .canonical_name = "Lookup Workspace Resource",
      .implementation = { .call = &WORKSPACE_RESOURCE_LOOKUP_CALL,
                          .access = &WORKSPACE_RESOURCE_READ_ACCESS } },
};
static const type_desc *const TYPES[] = { &WORKSPACE_RESOURCE_ROW_LAYOUT,
                                          &WORKSPACE_RESOURCE_RESULT_LAYOUT };
const native_definition_pack WORKSPACE_RESOURCE_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS),
    .layouts = TYPES,
    .layout_count = COUNT(TYPES)
};
