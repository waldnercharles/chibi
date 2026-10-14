#include "authoring/workspace_documents_definition.h"

#define DATA(NAME, STORAGE, TYPE)                                              \
    {                                                                          \
        .name = NAME, .kind = NATIVE_PARAMETER_DATA, .storage = STORAGE,       \
        .type_name = TYPE, .container = PIN_CONTAINER_SINGLE,                  \
        .has_default = true, .default_value = {                                \
            .type = STORAGE                                                    \
        }                                                                      \
    }
#define EXEC(NAME)                                                             \
    { .name = NAME, .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW, .container = PIN_CONTAINER_SINGLE }
static const native_parameter_definition OPEN_INPUTS[] = {
    EXEC("Exec"),
    DATA("Path", PIN_STR, "const char *")
};
static const native_parameter_definition MEMORY_INPUTS[] = {
    EXEC("Exec"),
    DATA("Text", PIN_STR, "const char *")
};
static const native_parameter_definition HANDLE_INPUTS[] = {
    EXEC("Exec"),
    DATA("Document", PIN_I32, "int")
};
static const native_parameter_definition REVISION_INPUTS[] = {
    EXEC("Exec"),
    DATA("Document", PIN_I32, "int"),
    DATA("Revision", PIN_I32, "int")
};
static const native_parameter_definition EDIT_INPUTS[] = {
    EXEC("Exec"),
    DATA("Document", PIN_I32, "int"),
    DATA("Revision", PIN_I32, "int"),
    DATA("Start", PIN_I32, "int"),
    DATA("End", PIN_I32, "int"),
    DATA("Text", PIN_STR, "const char *")
};
static const native_parameter_definition CREATE_INPUTS[] = {
    EXEC("Exec"),
    DATA("Path", PIN_STR, "const char *"),
    DATA("Text", PIN_STR, "const char *")
};
static const native_parameter_definition PATH_INPUTS[] = {
    EXEC("Exec"),
    DATA("Document", PIN_I32, "int"),
    DATA("Revision", PIN_I32, "int"),
    DATA("Path", PIN_STR, "const char *")
};
static const native_parameter_definition OUTPUTS[] = {
    EXEC("Then"),
    DATA("Ok", PIN_BOOL, "bool"),
    DATA("Status", PIN_I32, "int"),
    DATA("Error", PIN_STR, "const char *"),
    DATA("Document", PIN_I32, "int"),
    DATA("Revision", PIN_I32, "int"),
    DATA("Text", PIN_STR, "const char *"),
    DATA("Path", PIN_STR, "const char *"),
    DATA("Dirty", PIN_BOOL, "bool"),
    DATA("Transaction", PIN_BOOL, "bool"),
    DATA("CanUndo", PIN_BOOL, "bool"),
    DATA("CanRedo", PIN_BOOL, "bool"),
    DATA("Bytes", PIN_I32, "int"),
    DATA("NewFile", PIN_BOOL, "bool")
};
static const native_parameter_definition HANDLES_INPUTS[] = { EXEC("Exec") };
static const native_parameter_definition HANDLES_OUTPUTS[] = {
    EXEC("Then"),
    DATA("Ok", PIN_BOOL, "bool"),
    DATA("Status", PIN_I32, "int"),
    DATA("Error", PIN_STR, "const char *"),
    { .name = "Documents",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_I32,
      .type_name = "int",
      .container = PIN_CONTAINER_ARRAY }
};
static const native_parameter_definition BATCH_INPUTS[] = {
    EXEC("Exec"),
    { .name = "Documents",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_I32,
      .type_name = "int",
      .container = PIN_CONTAINER_ARRAY },
    { .name = "Revisions",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_I32,
      .type_name = "int",
      .container = PIN_CONTAINER_ARRAY },
    { .name = "Texts",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STR,
      .type_name = "const char *",
      .container = PIN_CONTAINER_ARRAY },
    DATA("Anchor", PIN_I32, "int"),
};
static const native_parameter_definition CANDIDATE_INPUTS[] = {
    EXEC("Exec"),
    { .name = "Documents",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_I32,
      .type_name = "int",
      .container = PIN_CONTAINER_ARRAY },
    { .name = "Revisions",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_I32,
      .type_name = "int",
      .container = PIN_CONTAINER_ARRAY },
    { .name = "Paths",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STR,
      .type_name = "const char *",
      .container = PIN_CONTAINER_ARRAY },
    { .name = "Texts",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STR,
      .type_name = "const char *",
      .container = PIN_CONTAINER_ARRAY },
    DATA("Anchor", PIN_I32, "int")
};
static const native_parameter_definition BATCH_OUTPUTS[] = {
    EXEC("Then"),
    DATA("Ok", PIN_BOOL, "bool"),
    DATA("Status", PIN_I32, "int"),
    DATA("Error", PIN_STR, "const char *")
};
static const native_parameter_definition PATH_STATUS_OUTPUTS[] = {
    EXEC("Then"),
    DATA("Ok", PIN_BOOL, "bool"),
    DATA("Status", PIN_I32, "int"),
    DATA("Error", PIN_STR, "const char *"),
    DATA("Exists", PIN_BOOL, "bool"),
    DATA("Document", PIN_I32, "int"),
    DATA("Revision", PIN_I32, "int")
};
#define DEFINITION(ID, NAME, SOURCE, INPUTS)                                   \
    { .canonical_name = NAME,                                                  \
      .source_name = SOURCE,                                                   \
      .category = "Authoring/Documents",                                       \
      .description = "Revisioned UTF-8 document operation; Ok, Status and "    \
                     "Error report authoring refusals. Returns a copied "      \
                     "current view. Revisions and byte offsets are integers; " \
                     "edits and history commands require the expected "        \
                     "Revision.",                                              \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .inputs = INPUTS##_INPUTS,                                               \
      .input_count = NATIVE_COUNT(INPUTS##_INPUTS),                            \
      .outputs = OUTPUTS,                                                      \
      .output_count = NATIVE_COUNT(OUTPUTS),                                   \
      .completion_output = 0,                                                  \
      .effects = SCRIPT_EFFECT_HOST },
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = "Validate Workspace Document Edit",
      .source_name = "workspace_document_validate_edit",
      .category = "Authoring/Documents",
      .description = "Read-only exact-revision preflight; no bytes, handles or "
                     "history change. Success reserves nothing; apply "
                     "revalidates.",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = EDIT_INPUTS,
      .input_count = NATIVE_COUNT(EDIT_INPUTS),
      .outputs = BATCH_OUTPUTS,
      .output_count = NATIVE_COUNT(BATCH_OUTPUTS),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },

    { .canonical_name = "Validate Workspace Document Candidates",
      .source_name = "workspace_document_validate_candidates",
      .category = "Authoring/Documents",
      .description = "Read-only exact-revision preflight; no bytes, handles or "
                     "history change. Success reserves nothing; apply "
                     "revalidates.",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = CANDIDATE_INPUTS,
      .input_count = NATIVE_COUNT(CANDIDATE_INPUTS),
      .outputs = BATCH_OUTPUTS,
      .output_count = NATIVE_COUNT(BATCH_OUTPUTS),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },

    { .canonical_name = "Inspect Workspace Document Path",
      .source_name = "workspace_document_path",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = OPEN_INPUTS,
      .input_count = NATIVE_COUNT(OPEN_INPUTS),
      .outputs = PATH_STATUS_OUTPUTS,
      .output_count = NATIVE_COUNT(PATH_STATUS_OUTPUTS),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = "Replace Workspace Documents",
      .source_name = "workspace_document_replace_many",
      .category = "Authoring/Documents",
      .description = "Atomic exact-revision replacement with shared bounded "
                     "undo/redo; equal-length arrays of distinct document "
                     "handles, revisions and UTF-8 text.",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = BATCH_INPUTS,
      .input_count = NATIVE_COUNT(BATCH_INPUTS),
      .outputs = BATCH_OUTPUTS,
      .output_count = NATIVE_COUNT(BATCH_OUTPUTS),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
    WORKSPACE_DOCUMENT_OPERATIONS(DEFINITION){
        .canonical_name = "Workspace Document Handles",
        .source_name = "workspace_document_handles",
        .category = "Authoring/Documents",
        .description = "Returns open document handles in creation order. "
                       "Handles "
                       "are never reused during this workspace service "
                       "lifetime.",
        .kind = SCRIPT_CAPABILITY_ACTION,
        .inputs = HANDLES_INPUTS,
        .input_count = NATIVE_COUNT(HANDLES_INPUTS),
        .outputs = HANDLES_OUTPUTS,
        .output_count = NATIVE_COUNT(HANDLES_OUTPUTS),
        .completion_output = 0,
        .effects = SCRIPT_EFFECT_HOST }
};
#undef DEFINITION
#define BINDING(ID, NAME, SOURCE, INPUTS)                                      \
    { .canonical_name = NAME,                                                  \
      .implementation = { .call = &WORKSPACE_DOCUMENT_##ID##_CALL,             \
                          .access = &WORKSPACE_DOCUMENT_ACCESS } },
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Validate Workspace Document Edit",
      .implementation = { .call = &WORKSPACE_DOCUMENT_VALIDATE_EDIT_CALL,
                          .access = &WORKSPACE_DOCUMENT_ACCESS } },
    { .canonical_name = "Validate Workspace Document Candidates",
      .implementation = { .call = &WORKSPACE_DOCUMENT_VALIDATE_CANDIDATES_CALL,
                          .access = &WORKSPACE_DOCUMENT_ACCESS } },

    { .canonical_name = "Inspect Workspace Document Path",
      .implementation = { .call = &WORKSPACE_DOCUMENT_PATH_CALL,
                          .access = &WORKSPACE_DOCUMENT_ACCESS } },
    { .canonical_name = "Replace Workspace Documents",
      .implementation = { .call = &WORKSPACE_DOCUMENT_REPLACE_MANY_CALL,
                          .access = &WORKSPACE_DOCUMENT_ACCESS } },
    WORKSPACE_DOCUMENT_OPERATIONS(BINDING){
        .canonical_name = "Workspace Document Handles",
        .implementation = { .call = &WORKSPACE_DOCUMENT_HANDLES_CALL,
                            .access = &WORKSPACE_DOCUMENT_ACCESS } }
};
#undef BINDING
const native_definition_pack WORKSPACE_DOCUMENTS_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS)
};
