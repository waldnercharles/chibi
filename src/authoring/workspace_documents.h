#ifndef CHIBI_WORKSPACE_DOCUMENTS_H
#define CHIBI_WORKSPACE_DOCUMENTS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct workspace_documents workspace_documents;
typedef enum
{
    WORKSPACE_DOCUMENT_OK,
    WORKSPACE_DOCUMENT_INVALID,
    WORKSPACE_DOCUMENT_STALE,
    WORKSPACE_DOCUMENT_LIMIT,
    WORKSPACE_DOCUMENT_UTF8,
    WORKSPACE_DOCUMENT_IO,
    WORKSPACE_DOCUMENT_CONFLICT,
    WORKSPACE_DOCUMENT_TRANSACTION,
    WORKSPACE_DOCUMENT_EMPTY_HISTORY,
    WORKSPACE_DOCUMENT_NOT_FILE,
    WORKSPACE_DOCUMENT_UNAVAILABLE,
    WORKSPACE_DOCUMENT_EXISTS,
    WORKSPACE_DOCUMENT_DIRTY,
} workspace_document_status;
enum
{
    WORKSPACE_DOCUMENT_COUNT_MAX = 64,
    WORKSPACE_DOCUMENT_BYTES_MAX = 1024 * 1024,
    WORKSPACE_DOCUMENT_TEXT_BYTES_MAX = 16 * 1024 * 1024,
    WORKSPACE_DOCUMENT_HISTORY_BYTES_MAX = 32 * 1024 * 1024,
    WORKSPACE_DOCUMENT_HISTORY_MAX = 128,
    WORKSPACE_DOCUMENT_PATH_MAX = 1024,
    WORKSPACE_DOCUMENT_SNAPSHOT_BYTES_MAX = 32 * 1024 * 1024,
};
/* Positive handles are never reused within this service, including after close.
 * Revisions never wrap. Views borrow text/path until the next mutation or free.
 * Callers copying a view before a mutation own that copy. A refusal changes no
 * document, history, revision or disk file. Service operations are serial. */
typedef struct
{
    int document, revision;
    const char *text, *path;
    size_t bytes;
    bool dirty, transaction, can_undo, can_redo, new_file;
} workspace_document_view;
/* NULL root creates a memory-only service. Disk access is workspace-relative,
 * rejects links and special files, and never creates missing directories.
 * Disk mode uses portable rooted handles and atomic no-follow publication. */
workspace_documents *workspace_documents_new(const char *root, workspace_document_status *);
void workspace_documents_free(workspace_documents *);
const char *workspace_document_status_text(workspace_document_status);
workspace_document_status workspace_document_open(workspace_documents *, const char *path, int *document);
workspace_document_status workspace_document_memory(
    workspace_documents *,
    const char *text,
    int *document
);
workspace_document_status workspace_document_read(
    const workspace_documents *,
    int document,
    workspace_document_view *
);
/* Reads an owned UTF-8 snapshot by relative path without allocating a handle or
 * changing history. An open document's unsaved text takes precedence over disk.
 * Disk access uses the retained no-follow root and the normal document bound. */
workspace_document_status workspace_document_read_path(
    const workspace_documents *,
    const char *path,
    char **text
);
int workspace_document_handles(const workspace_documents *, int *out, int capacity);
workspace_document_status workspace_document_close(workspace_documents *, int document);
/* Byte offsets and replacement text must be UTF-8 boundaries/valid UTF-8 with
 * no embedded NUL. One edit is an undo step unless a transaction is open.
 * A transaction groups edits; cancel restores its starting text, commit adds
 * one undo step only if different. New committed edits discard redo history.
 * Bounded history evicts oldest snapshots rather than blocking normal editing;
 * active transaction origins and their redo history remain reserved. */
workspace_document_status workspace_document_edit(
    workspace_documents *,
    int document,
    int revision,
    int start,
    int end,
    const char *text
);
/* Read-only preflight. Success reserves nothing; edit revalidates at apply time.
 * No text, revision, handle or undo/redo history changes, even on success. */
workspace_document_status workspace_document_validate_edit(
    workspace_documents *,
    int document,
    int revision,
    int start,
    int end,
    const char *text
);
workspace_document_status workspace_document_begin(workspace_documents *, int document, int revision);
workspace_document_status workspace_document_commit(workspace_documents *, int document, int revision);
workspace_document_status workspace_document_cancel(workspace_documents *, int document, int revision);
workspace_document_status workspace_document_undo(workspace_documents *, int document, int revision);
workspace_document_status workspace_document_redo(workspace_documents *, int document, int revision);
/* Existing-file save: byte-compares disk with the baseline, writes an exclusive
 * sibling temporary, rechecks baseline and atomically renames. Other processes
 * must coordinate writes to eliminate the final check-to-rename race. Save is
 * refused while a transaction is open. Failure retains current dirty/history.
 * Memory documents cannot save; no implicit Save As or overwrite policy. */
workspace_document_status workspace_document_save(workspace_documents *, int document, int revision);
/* Exclusive creation/Save As never overwrite. Save As retains document identity,
 * adopts the new path/baseline and preserves undo history; the old file remains.
 * Rename/remove require a clean document and matching on-disk baseline. Rename
 * atomically refuses an existing destination; unsupported platforms report
 * Unavailable. Remove closes the handle only after successful unlink.
 * As with save, independent disk writers must coordinate mutations to eliminate
 * the final baseline-check-to-mutation race. */
/* Reserves a currently absent path as an unsaved owned draft. No disk file is
 * created. A first Save creates exclusively; any intervening file causes
 * refusal. Closing discards the draft without touching disk. */
workspace_document_status workspace_document_draft(
    workspace_documents *,
    const char *path,
    const char *text,
    int *document
);
workspace_document_status workspace_document_create(
    workspace_documents *,
    const char *path,
    const char *text,
    int *document
);
workspace_document_status workspace_document_save_as(
    workspace_documents *,
    int document,
    int revision,
    const char *path
);
workspace_document_status workspace_document_rename(
    workspace_documents *,
    int document,
    int revision,
    const char *path
);
workspace_document_status workspace_document_remove(workspace_documents *, int document, int revision);
/* Explicit disk reload adopts a checked UTF-8 snapshot as current/saved text,
 * preserves the handle and retains the replaced draft as one undo step. */
workspace_document_status workspace_document_reload(workspace_documents *, int document, int revision);
/* Atomically replaces up to COUNT_MAX distinct exact-revision documents.
 * A refusal changes no bytes, revisions or history. Targeted active gestures
 * refuse. Unchanged documents retain their revision and existing redo history.
 * Changed documents form one bounded history group: undo/redo from any member
 * restores all members only when every member is at that group history head.
 * New branches, close and pruning remove complete groups. No disk files save. */
typedef struct
{
    int document, revision;
    const char *text;
} workspace_document_replacement;
workspace_document_status workspace_document_replace_many(
    workspace_documents *,
    const workspace_document_replacement *,
    int count
);
/* Anchor must be zero or one of the exact-revision replacement handles. If
 * another member changes, an unchanged anchor joins the shared undo group.
 * An entirely unchanged batch stays a no-op. */
workspace_document_status workspace_document_replace_anchored(
    workspace_documents *,
    const workspace_document_replacement *,
    int count,
    int anchor
);
/* Read-only batch preflight, sharing the exact replacement validation. */
workspace_document_status workspace_document_validate_replace_anchored(
    workspace_documents *,
    const workspace_document_replacement *,
    int count,
    int anchor
);
typedef struct
{
    int document, revision;
    const char *path, *text;
} workspace_document_candidate;
/* Path-aware preflight also admits absent named drafts (Document=0, Revision=-1).
 * Existing paths must identify the supplied owned document. Missing paths are
 * checked for safe absence and capacity without opening/reserving/creating them.
 * Anchor is zero or an existing member. Subsequent apply must revalidate. */
workspace_document_status workspace_document_validate_candidates(
    workspace_documents *,
    const workspace_document_candidate *,
    int count,
    int anchor
);
/* Pure path inspection, preferring an existing owned handle (including a draft).
 * Missing paths are successful Exists=false; unsafe paths and IO are refusals. */
workspace_document_status workspace_document_path(
    const workspace_documents *,
    const char *path,
    bool *exists,
    int *document,
    int *revision
);
/* Opaque deterministic format1 document-state checkpoint. Capture refuses open
 * transactions and owns its returned buffer (free after use). The 32MiB bound
 * is independent of the larger live text/history budgets; oversized snapshots
 * report Limit. Disk bytes are separate: never replace saved baselines with
 * current dirty overlays when transporting this state.
 * Restore only admits a fresh service (no previous handles, even closed ones).
 * It validates the complete candidate before adoption, preserving the configured
 * root, handles/revisions/highwater and grouped undo/redo. No files are written. */
workspace_document_status workspace_documents_snapshot(
    const workspace_documents *,
    unsigned char **owned,
    size_t *bytes
);
workspace_document_status workspace_documents_restore(
    workspace_documents *empty,
    const void *bytes,
    size_t size
);
#endif
