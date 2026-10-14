#ifndef CHIBI_WORKSPACE_FILE_HISTORY_H
#define CHIBI_WORKSPACE_FILE_HISTORY_H
#include "authoring/workspace_documents.h"

#include <stdbool.h>
#include <stddef.h>
#define WORKSPACE_FILE_HISTORY_TOKENS_MAX 8
#define WORKSPACE_FILE_HISTORY_FILES_MAX 64
#define WORKSPACE_FILE_HISTORY_FILE_BYTES_MAX (16u * 1024u * 1024u)
#define WORKSPACE_FILE_HISTORY_BYTES_MAX (32u * 1024u * 1024u)
typedef struct workspace_file_history workspace_file_history;
typedef struct
{
    bool ok;
    int token, changed;
    const char *state, *error;
} workspace_file_history_result;
typedef struct
{
    workspace_file_history_result result;
    const char *const *paths, *const *recovery;
    int count;
    size_t bytes;
    bool can_undo, can_redo;
} workspace_file_history_view;
/* Owns a portable rooted handle; borrows optional documents only to refuse
 * destroying dirty/transaction source. No selection or document-close policy.
 * File batches use exact-byte preflight and exclusive same-root tombstone
 * moves. A failed rollback retains every recovery file and its manifest.
 * External writers must coordinate mutations; filesystem batches are not crash
 * atomic. Destruction preserves recovery bundles. Explicit close permanently
 * discards a fully deleted bundle; partial bundles must first be recovered. */
workspace_file_history *workspace_file_history_new(const char *root, workspace_documents *);
void workspace_file_history_free(workspace_file_history *);
workspace_file_history_result workspace_files_delete(
    workspace_file_history *,
    const char *const *paths,
    int count
);
workspace_file_history_result workspace_files_restore(workspace_file_history *, int token);
workspace_file_history_result workspace_files_redo(workspace_file_history *, int token);
workspace_file_history_result workspace_files_forget(workspace_file_history *, int token);
bool workspace_file_history_read(workspace_file_history *, int token, workspace_file_history_view *);
#endif
