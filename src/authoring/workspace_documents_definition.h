#ifndef CHIBI_WORKSPACE_DOCUMENTS_DEFINITION_H
#define CHIBI_WORKSPACE_DOCUMENTS_DEFINITION_H
#include "execution/host.h"
#include "runtime/native_definitions.h"

/* Optional application-selected pack. Bind an owned workspace_documents to
 * service "workspace_documents" before executing the application's Begin.
 * All refusals are Ok=false plus Status/Error; no runtime exception. Every
 * operation returns a copied current document view when the handle still
 * exists. The snapshot remains valid after later service edits. */
#define WORKSPACE_DOCUMENT_OPERATIONS(X)                                          \
    X(OPEN, "Open Workspace Document", "workspace_document_open", OPEN)           \
    X(MEMORY, "Memory Workspace Document", "workspace_document_memory", MEMORY)   \
    X(READ, "Read Workspace Document", "workspace_document_read", HANDLE)         \
    X(INFO, "Workspace Document Info", "workspace_document_info", HANDLE)         \
    X(CLOSE, "Close Workspace Document", "workspace_document_close", HANDLE)      \
    X(EDIT, "Edit Workspace Document", "workspace_document_edit", EDIT)           \
    X(BEGIN, "Begin Workspace Document", "workspace_document_begin", REVISION)    \
    X(COMMIT, "Commit Workspace Document", "workspace_document_commit", REVISION) \
    X(CANCEL, "Cancel Workspace Document", "workspace_document_cancel", REVISION) \
    X(UNDO, "Undo Workspace Document", "workspace_document_undo", REVISION)       \
    X(REDO, "Redo Workspace Document", "workspace_document_redo", REVISION)       \
    X(SAVE, "Save Workspace Document", "workspace_document_save", REVISION)       \
    X(CREATE, "Create Workspace Document", "workspace_document_create", CREATE)   \
    X(DRAFT, "Draft Workspace Document", "workspace_document_draft", CREATE)      \
    X(SAVE_AS, "Save Workspace Document As", "workspace_document_save_as", PATH)  \
    X(RENAME, "Rename Workspace Document", "workspace_document_rename", PATH)     \
    X(REMOVE, "Remove Workspace Document", "workspace_document_remove", REVISION) \
    X(RELOAD, "Reload Workspace Document", "workspace_document_reload", REVISION)
#define DECLARE(ID, NAME, SOURCE, INPUTS)                                      \
    extern const node_host_call WORKSPACE_DOCUMENT_##ID##_CALL;
WORKSPACE_DOCUMENT_OPERATIONS(DECLARE)
#undef DECLARE
extern const node_host_call WORKSPACE_DOCUMENT_PATH_CALL;
extern const node_host_call WORKSPACE_DOCUMENT_HANDLES_CALL;
extern const node_host_call WORKSPACE_DOCUMENT_REPLACE_MANY_CALL;
extern const node_host_call WORKSPACE_DOCUMENT_VALIDATE_EDIT_CALL;
extern const node_host_call WORKSPACE_DOCUMENT_VALIDATE_CANDIDATES_CALL;
extern const node_access WORKSPACE_DOCUMENT_ACCESS;
extern const native_definition_pack WORKSPACE_DOCUMENTS_DEFINITION_PACK;
#endif
