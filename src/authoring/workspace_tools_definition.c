#include "authoring/workspace_tools_definition.h"

#include "authoring/workspace_css.h"
#include "authoring/workspace_saved.h"
#include "execution/function.h"

#include <stddef.h>
#define FIELD(T, N, K, S)                                                      \
    { .name = #N,                                                              \
      .type = K,                                                               \
      .type_name = S,                                                          \
      .offset = offsetof(T, N),                                                \
      .size = sizeof(((T *)0)->N) }
static const type_field WorkspaceSavedRow_FIELDS[] = {
    FIELD(workspace_saved_row, key, PIN_STR, "const char *"),
    FIELD(workspace_saved_row, value, PIN_STR, "const char *")
};
static const type_desc WorkspaceSavedRow_LAYOUT = {
    .name = "WorkspaceSavedRow",
    .size = sizeof(workspace_saved_row),
    .fields = WorkspaceSavedRow_FIELDS,
    .field_count = NATIVE_COUNT(WorkspaceSavedRow_FIELDS)
};
static const type_field WorkspaceCssRow_FIELDS[] = {
    FIELD(workspace_css_row, path, PIN_STR, "const char *"),
    FIELD(workspace_css_row, file, PIN_STR, "const char *"),
    FIELD(workspace_css_row, selector, PIN_STR, "const char *"),
    FIELD(workspace_css_row, name, PIN_STR, "const char *"),
    FIELD(workspace_css_row, value, PIN_STR, "const char *"),
    FIELD(workspace_css_row, fingerprint, PIN_STR, "const char *"),
    FIELD(workspace_css_row, start, PIN_I32, "int"),
    FIELD(workspace_css_row, end, PIN_I32, "int")
};
static const type_desc WorkspaceCssRow_LAYOUT = {
    .name = "WorkspaceCssRow",
    .size = sizeof(workspace_css_row),
    .fields = WorkspaceCssRow_FIELDS,
    .field_count = NATIVE_COUNT(WorkspaceCssRow_FIELDS)
};
const native_type_definition WORKSPACE_TOOLS_TYPES[2] = {
    { .layout = &WorkspaceSavedRow_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &WorkspaceCssRow_LAYOUT, .storage = PIN_STRUCT }
};
const type_desc *const WORKSPACE_TOOLS_LAYOUTS[] = { &WorkspaceSavedRow_LAYOUT,
                                                     &WorkspaceCssRow_LAYOUT };
const type_catalog_pack WORKSPACE_TOOLS_TYPE_PACK = { .types = WORKSPACE_TOOLS_LAYOUTS,
                                                      .type_count = 2 };
#define P(N, K, T)                                                             \
    { .name = N, .kind = NATIVE_PARAMETER_DATA, .storage = K, .type_name = T }
#define S(N) P(N, PIN_STR, "const char *")
#define I(N) P(N, PIN_I32, "int")
#define B(N) P(N, PIN_BOOL, "bool")
#define E(N) { .name = N, .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW }
#define A(N, T)                                                                \
    { .name = N,                                                               \
      .kind = NATIVE_PARAMETER_DATA,                                           \
      .storage = PIN_STRUCT,                                                   \
      .type_name = T,                                                          \
      .container = PIN_CONTAINER_ARRAY }
static const native_parameter_definition Saved_IN[] = { I("Session"),
                                                        I("First"),
                                                        I("Count") };
static const native_parameter_definition Saved_OUT[] = { B("Ok"),
                                                         A("Rows", "WorkspaceSavedRow"),
                                                         I("Total"),
                                                         S("Revision"),
                                                         S("Error") };
static const native_parameter_definition Save_IN[] = { E("Exec"),
                                                       I("Session"),
                                                       S("Revision"),
                                                       S("Key"),
                                                       S("Value") };
static const native_parameter_definition Save_OUT[] = { E("Then"), B("Ok"), S("Error") };
static const native_parameter_definition Css_IN[] = { I("Document"), I("Revision") };
static const native_parameter_definition Css_OUT[] = { B("Ok"),
                                                       A("Rows", "WorkspaceCssRow"),
                                                       S("Error") };
static const native_parameter_definition Edit_IN[] = {
    E("Exec"), S("Path"),     S("Fingerprint"), I("Start"),
    I("End"),  S("OldValue"), S("Value")
};
static const native_parameter_definition Edit_OUT[] = { E("Then"),
                                                        B("Ok"),
                                                        I("Document"),
                                                        I("Revision"),
                                                        S("Error") };
#define D(NAME, SOURCE, KEY, ACTION, DESCRIPTION)                              \
    { .canonical_name = NAME,                                                  \
      .source_name = SOURCE,                                                   \
      .category = "Authoring",                                                 \
      .description = DESCRIPTION,                                              \
      .kind = ACTION ? SCRIPT_CAPABILITY_ACTION : SCRIPT_CAPABILITY_FUNCTION,  \
      .pure = !ACTION,                                                         \
      .inputs = KEY##_IN,                                                      \
      .input_count = NATIVE_COUNT(KEY##_IN),                                   \
      .outputs = KEY##_OUT,                                                    \
      .output_count = NATIVE_COUNT(KEY##_OUT),                                 \
      .completion_output = ACTION ? 0 : -1 }
const native_capability_definition WORKSPACE_TOOLS_DEFINITIONS[4] = {
    D("Query Runtime Saved Data", "query_runtime_saved_data", Saved, false, "Copies a page of existing keys and exact text from a live runtime session. First>=0, Count=1..256. Revision is an exact decimal token. Read-only presentations permit inspection; stale sessions refuse."),
    D("Edit Runtime Saved Data", "edit_runtime_saved_data", Save, true, "Edits an existing key only when the runtime session and save revision still match. Refuses read-only presentations. Changes the live store without creating keys or committing host backing storage."),
    D("Query Workspace CSS", "query_workspace_css", Css, false, "Reads the explicit document revision UI page, falling back to project UI. Copies at most4096 custom declarations from its rooted imports/page/widgets, with original source spans and fingerprints. Unsaved overlays take precedence; no handles are opened."),
    D("Edit Workspace CSS", "edit_workspace_css", Edit, true, "Rechecks the complete stylesheet fingerprint and original value span, applies a revisioned workspace edit, and saves with conflict detection. Preserves undo/history; a save failure retains the unsaved edit.")
};

static const char *unavailable(void *, const pin_value *, pin_value *)
{
    return "Workspace tool source requires an authoring host";
}
static const node_host_call DECLARE_CALL = { .invoke = unavailable };
static const node_access READ = { .flags = NODE_EFFECT_WORLD_READ },
                         WRITE = { .flags = NODE_EFFECT_HOST_SINK };
#define C(N, A)                                                                \
    {                                                                          \
        .canonical_name = N, .implementation = {                               \
            .call = &DECLARE_CALL,                                             \
            .access = &A                                                       \
        }                                                                      \
    }
static const native_definition_binding BINDINGS[] = {
    C("Query Runtime Saved Data", READ),
    C("Edit Runtime Saved Data", WRITE),
    C("Query Workspace CSS", READ),
    C("Edit Workspace CSS", WRITE)
};
const native_definition_pack WORKSPACE_TOOLS_DECLARATION_PACK = {
    .definitions = WORKSPACE_TOOLS_DEFINITIONS,
    .definition_count = 4,
    .bindings = BINDINGS,
    .binding_count = 4,
    .layouts = WORKSPACE_TOOLS_LAYOUTS,
    .layout_count = 2
};
