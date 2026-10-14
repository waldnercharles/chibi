# Authored collaboration transport

The room server owns authentication, room membership, roles, ordered revisions,
proposal decisions, checkpoint retention and bounded replay. The envelope remains
protocol version 1. Studio's room workflow is authored in
`projects/studio/content/behaviors/StudioCollaboration.chibi`; the generic SDK
`browser_messages` service owns sockets, bounded opaque message attachments,
HTTP, transient tab storage and archive I/O. Native transport reports unavailable.

## Checkpoint contract

Snapshots use `checkpoint_version: 3`. Their payload contains exactly
`checkpoint_version`, `workspace`, `archive` and `sha256`.

- `workspace` contains `version: 3`, a project-relative `active_path` (or an empty
  string), and a bounded `presentation` object. It contains no native editor
  structures or pointer identities.
- `archive` contains UTF-8-sorted, unique `files` records with `path`, canonical
  `base64` bytes and lowercase `sha256`, plus opaque `attachments`, decoded
  aggregate `bytes` and a digest of `JSON.stringify({files,attachments})`. Paths cannot traverse, use backslashes or refer to
  absolute locations. Binary resources are included alongside authored text.
- `attachments` contains one `workspace-documents-v1` record with canonical
  `base64`, decoded `bytes` and lowercase `sha256`. Its portable format1 binary
  state includes current/saved UTF-8 text, pathless and unsaved drafts, exact
  document handles/revisions/highwater and complete grouped undo/redo. It has no
  runtime execution or physics state. The native decoder validates the complete
  candidate before restoring into a fresh service.
- The outer digest covers `JSON.stringify({checkpoint_version, workspace,
  archive})`, in that property order. The service verifies every file digest,
  byte count and archive digest before accepting a checkpoint.

The server permits at most 4,096 files, 2 MiB per file and 8 MiB decoded per
checkpoint. Workspace metadata is limited to 64 KiB. The generic browser archive
codec has larger independent I/O bounds; a checkpoint outside the room contract
is refused rather than silently truncated. Older checkpoint versions are intentionally unsupported.

Capture retains original disk bytes separately from current/saved document
state, preserving real dirty/save-conflict baselines and untitled drafts. Active
gestures and unsettled source drafts refuse capture. Installation validates into a fresh owned root and
never overwrites the active filesystem. Authored document review precedes normal
project activation. The generic host preflights the replacement, adopts its root,
and only then retires the old session. Per-tab room credentials, revision and
checkpoint identity support reconnect after that application restart; credentials
are not placed in project save data.

## Mutation admission

Source splices and atomic source/metadata replacements are bounded, revision
guarded proposals. Atomic actions use `family: "document-edits"`, a project-
relative `anchor`, and one through eight `{path,text}` replacements; the complete
JSON action remains limited to16KiB. Paths are unique and the anchor must occur
in the replacements. Handles and per-replica document revisions never go on the
wire. New missing sidecars are unsaved named drafts, created only during accepted
application; refusal closes fresh drafts and leaves the complete batch unchanged.
The host preflights a proposal without changing bytes, revisions, draft handles
or undo/redo, publishes its decision, and applies only the canonical service
echo. Failed publication retains the original draft and redo history. Apply
rechecks exact revisions; a committed action that cannot apply blocks recovery
for review. UI acknowledgement follows successful canonical application, with
request owner and request number correlated throughout. Viewers and unsynchronized members cannot mutate.
Remote canonical acknowledgements use a distinct owner from local source drafts.

Operations without a pure proposal mapping remain explicitly unavailable in an
active room: file creation/move/delete/import, undo/redo/revert, semantic source
rename, and visual/model/UI source mutations. Graph and object-property owners
may submit validated candidate text through the same document proposal path.
Local runtime inspection, previews and exports do not grant canonical write
permission. Peer following currently follows the source path and panel.

## Validation scope

The ordinary C collaboration dispatcher includes production server checkpoint
validation cases for canonical bytes, hashes, duplicates, unsafe paths, version
and envelope refusal. The same bounded Node child also evaluates the production
EM_JS archive helper over three tiny mock files: synchronous disk/history freeze,
attachment copy ownership, UTF-8 order, unadopted cleanup, adoption protection and
corruption/path/budget refusal. This is CPU helper evidence, not browser I/O. Browser transport and JSON parsing have CPU ownership and
bounds cases. These do not establish live multi-client browser acceptance,
rendered collaboration UI, reconnect under load or snapshot installation
acceptance; those require separate generic-host browser evidence.

The SDK4 history transport/server implementation is on source. Its checkpoint3
Studio consumer, document-restored startup policy and authored caret/scene view
restore are staged until the matching installed SDK is packaged. SDK3's promoted
redo-safe proposal consumer compiles in isolation; full application/browser room
acceptance remains separate and pending.
