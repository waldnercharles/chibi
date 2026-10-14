# Authored document proposals

Studio document changes use typed events on the application entity. A workflow owns a stable `Owner` string and a monotonic positive `Request`; an acknowledgement must match owner, request, document, and expected revision. Sending an event does not imply that another behavior has finished handling it.

`StudioDocumentEdit` proposes one UTF-8 byte splice. Source typing keeps one proposal in flight and coalesces later input until its matching result. `StudioDocumentEditResult` always echoes the proposal identity. Ordinary edits in an active room are forwarded to Collaboration; accepted replay uses the separate canonical bypass event. Ambient replay state does not authorize ordinary UI writes.

## Source and metadata batches

`StudioDocumentEdits` carries `Document`, `Revision`, `Documents[]`, `Revisions[]`, `Paths[]`, `Texts[]`, `Owner`, and `Request`. The first two fields identify the active source anchor. The arrays contain one to eight distinct full document replacements and must include that exact anchor. Each existing handle must match its expected revision and path. An absent sidecar is represented by handle0, revision-1, and its workspace-relative path.

Preparation first calls `workspace_document_path` to distinguish a genuinely absent file from unsafe or failed IO. Existing disk files are opened and read at an exact revision. A generic open failure must never be interpreted as an absent sidecar.

Acceptance creates an empty unsaved path-associated draft for each absent sidecar, then calls `workspace_document_replace_many` with `Anchor: Document`. If preparation or replacement fails, all fresh empty drafts close; existing bytes and history remain unchanged. The anchor participates in the shared history group even when only metadata changed. An entirely unchanged batch remains a no-op. No disk file is saved by acceptance. Source and sidecar remain canonical open buffers until explicit Save/SaveAll.

`StudioDocumentEditsResult` returns the original anchor identity, success, resolved `Documents[]`, current `Revisions[]`, error, owner, and request. A successful result supplies any newly created sidecar handle. Failure preserves the candidate for review or retry.

## Companion document UX

An open `sourcePath.meta` sidecar belongs to its open source tab. Studio hides the internal sidecar tab, includes its unsaved changes in the source dirty indicator, and reviews source and layout together before closing. Explicit source Save saves both files; SaveAll deduplicates the pair. Disk publication of two files is sequential, so a sidecar failure after source Save is reported explicitly and its draft remains open. An orphan sidecar remains visible rather than losing access to unsaved data. A sidecar disk conflict targets that actual sidecar for recovery, while keeping the primary source tab selected.

The atomic in-memory batch has shared source/metadata history. Existing single-document disk reload does not yet provide an anchored multi-document recovery history; that limitation is separate from batch undo acceptance.

## Visual edit leases

External edit preparation flushes coalesced source input and commits its text gesture before granting `StudioDocumentExternalEditReady`. An authorized owner may submit a correlated single edit or batch using that exact lease owner/request. Foreign or stale producers refuse. Once granted, the lease remains held until matching Complete; cancel requests retain the lock until the owner actually ends its gesture.

An active collaboration room refuses ordinary visual leases. Room batches use explicit `StudioCollaborationEdits` proposals and the separate `StudioDocumentCollaborationEdits` accepted path. The room controller must explicitly refuse unsupported batch families rather than applying source or metadata locally. Room wire acceptance and rendered editor behavior require separate validation from compiling these authored handlers.
