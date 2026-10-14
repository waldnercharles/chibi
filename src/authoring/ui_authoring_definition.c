#include "authoring/ui_authoring_definition.h"
#define PORT(N, P, T, C, V)                                                    \
    { .name = N,                                                               \
      .kind = NATIVE_PARAMETER_DATA,                                           \
      .storage = P,                                                            \
      .type_name = T,                                                          \
      .container = C,                                                          \
      .has_default = true,                                                     \
      .default_value = V }
#define INT(N, V)                                                              \
    PORT(N, PIN_I32, "int", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_I32, .i = V }))
#define TEXT(N, V)                                                             \
    PORT(N, PIN_STR, "const char *", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_STR, .str = V }))
#define BOOL(N)                                                                \
    PORT(N, PIN_BOOL, "bool", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_BOOL }))
#define EXEC(N)                                                                \
    { .name = N, .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW }
#define STATUS                                                                 \
    PORT("Status", PIN_STRUCT, "UiSourceStatus", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_STRUCT }))
static const native_parameter_definition QUERY_IN[] = { EXEC("Exec"),
                                                        INT("Document", 0),
                                                        INT("Revision", 0),
                                                        TEXT("Language", "rml"),
                                                        TEXT("Kind", "element"),
                                                        INT("Parent", -1),
                                                        INT("First", 0),
                                                        INT("Count", 64) };
static const native_parameter_definition QUERY_OUT[] = {
    EXEC("Then"),
    STATUS,
    PORT(
        "Rows",
        PIN_STRUCT,
        "UiSourceRow",
        PIN_CONTAINER_ARRAY,
        ((pin_value){ .type = PIN_STRUCT, .container = PIN_CONTAINER_ARRAY })
    )
};
static const native_parameter_definition EDIT_IN[] = { EXEC("Exec"),
                                                       INT("Document", 0),
                                                       INT("Revision", 0),
                                                       TEXT("Operation", "attribute"),
                                                       INT("Element", -1),
                                                       INT("Target", -1),
                                                       TEXT("Place", "inside"),
                                                       TEXT("Name", ""),
                                                       TEXT("Value", ""),
                                                       BOOL("Remove") };
static const native_parameter_definition EDIT_OUT[] = { EXEC("Then"), STATUS };
#define COUNT(A) ((int)(sizeof A / sizeof *A))
#define DEF(N, S, I, O, D)                                                     \
    { .canonical_name = N,                                                     \
      .source_name = S,                                                        \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .category = "UI",                                                        \
      .description = D,                                                        \
      .inputs = I,                                                             \
      .input_count = COUNT(I),                                                 \
      .outputs = O,                                                            \
      .output_count = COUNT(O),                                                \
      .completion_output = 0,                                                  \
      .effects = SCRIPT_EFFECT_HOST }
static const native_capability_definition DEFS[] = {
    DEF("Query UI Source", "query_ui_source", QUERY_IN, QUERY_OUT, "Bounded revision-checked source hierarchy, attributes, RCSS rules/declarations or syntax diagnostics. Language rml/rcss; Kind element/attribute/rule/declaration/diagnostic; Parent selects attribute/declaration owner. <=256 rows. Element Name is tag, Value is id, Extra is class list. Valid describes syntax separately from query success."),
    DEF("Edit UI Source", "edit_ui_source", EDIT_IN, EDIT_OUT, "Atomically edits current RML through the owning document transaction/history. Operation attribute/class/style/insert/move/delete/text/duplicate for RML; declaration/selector/remove-rule for RCSS (Element rule, Target declaration or -1 to append). Place inside/before/after. Remove removes attribute/class/style. Stale indexes, invalid source or invalid resulting markup leave the document intact.")
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Query UI Source",
      .implementation = { .call = &UI_AUTHORING_QUERY_CALL,
                          .access = &UI_AUTHORING_ACCESS } },
    { .canonical_name = "Edit UI Source",
      .implementation = { .call = &UI_AUTHORING_EDIT_CALL, .access = &UI_AUTHORING_ACCESS } }
};
static const type_desc *const TYPES[] = { &UI_AUTHORING_ROW_LAYOUT,
                                          &UI_AUTHORING_STATUS_LAYOUT };
const native_definition_pack UI_AUTHORING_DEFINITION_PACK = {
    .definitions = DEFS,
    .definition_count = COUNT(DEFS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS),
    .layouts = TYPES,
    .layout_count = COUNT(TYPES)
};
