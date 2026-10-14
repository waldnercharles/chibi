# Text and document native retirement map

## Current candidate status

See the [canonical migration evidence](../studio-rml-migration.md#current-candidate-evidence-2026-10-14)
for current SDK/source identities, formatting, package, startup and interaction
results. Source retirement is complete; complete workflow parity is not certified.

## Candidate source retirement, 2026-10-14

Studio's native implementation and obsolete native build dependencies have been
removed from this candidate. The manifest `chibi-studio/NATIVE_RETIREMENT.json`
records 347 retired files, including 318 C/C++ source/header files, with baseline
`34f13325c3fb4e1888dbd64bf45d809e07c782d3` and original blob identities. Recover
reference code with `git show 34f13325:<path>` in the Studio repository.

The mappings below distinguish authored replacements from parity and acceptance
gaps. Removal of source is complete; validation of every former workflow is not.
Earlier SDK4 whole-application compilation passed; Home failed before UI
because inactive AssetBrowser required `workspace_resources`. SDK5 optional
service contracts subsequently passed CPU/package checks. Current results are recorded in the canonical migration evidence linked above. References to SDK2–SDK6 below
retain their original evidence scope and do not certify rendered acceptance.

Original paths below are relative to chibi-studio. Authored replacements live in
`projects/studio/content/`; shared services live in the engine. Original native files have been retired from the candidate checkout. Their
verified reference is chibi-studio commit
`34f13325c3fb4e1888dbd64bf45d809e07c782d3`; inspect originals with
`git show 34f1332:<path>`. Git history remains the parity reference. Source compilation does not establish
rendered editor acceptance.

| Native source | Authored replacement | Shared engine service |
| --- | --- | --- |
| `src/editor/rmlui/kit_code.c` | `behaviors/StudioText.chibi`, `ui/StudioTextEditor.rml/.rcss` | `ui/controls/code_control`, generic RmlUi textarea |
| `src/editor/text/zep_bridge.cpp/.h` | StudioText keyboard, Vim and selection policy | Generic code control, `text_editing` UTF8 byte operations |
| `src/editor/studio/studio_code.c` | StudioText, StudioDocuments | `workspace_documents`, `document_semantics` |
| `src/editor/studio/studio_find.c`, `studio_refactor.c` | StudioText, StudioOutput | Project reference/call/rename services; atomic document history |
| `src/editor/documents/ui_code.c/.h`, `asset_code.c/.h` | StudioText, StudioDocuments | Dialect-aware Chibi/KDL/RML/RCSS inspection |
| `src/editor/documents/ui_buffer.c/.h` | Authored draft coalescing, correlated acknowledgments and flush barriers | Revision-guarded document edits/transactions |
| `src/editor/documents/navigation.c`, `commands.c`, `document.c` | StudioDocuments, StudioText navigation, authored dialogs | Rooted document IO, grouped undo/redo and explicit batch birth |
| `src/editor/documents/scene_history.c/.h`, `scene_retention.c/.h`, `scene_disk.c`, `scene_document.c`, `scene_documents.c` | StudioDocuments, StudioScene | Document history and scene preview snapshots; scene-owner acceptance required |

The Zep dependency also comprises `cmake/zep_dependencies.cmake` and patches
`zep-host-view`, `zep-select-range`, `zep-indicator-dot`, `zep-change-inner-word`,
`zep-flat-underline`, `zep-wheel-scroll`, `zep-wrapped-line-numbers`,
`zep-undo-pop`, `zep-integer-types`, `zep-marker-self-clear` and `zep-cursor-info`.
Removing these dependency paths must follow acceptance of their behavior, rather
than treating a successful link without Zep as parity evidence.

## Implemented contracts

Source edits preserve exact UTF8 bytes, CRLF and tabs. One request is in flight;
new drafts coalesce until the exact owner/request/document/revision result
arrives. Failed drafts remain recoverable. Save, close and navigation flush
before changing documents. External semantic gestures retain their lease until
matching completion; ambient collaboration replay never authorizes UI writes.

Source and `.meta` candidates adopt atomically with grouped history. Empty
metadata birth has an undoable default-layout baseline; persistent artifact birth
may provide valid initial text. No candidate preflight allocates a draft or writes
to disk. Source Save includes its companion, reports partial disk-save failure,
and does not publish newly undone empty companions.

## Acceptance still required

- Whole-editor routing still needs acceptance even though the generic bridge
  now carries the complete seven-field delegated key and five-field selection
  events. The previous four-argument bound and automatic Key injection were
  repaired; the real code-control/Rml ownership regression passes below.
- Rendered rapid typing followed by save/close/undo, rejected draft recovery,
  cross-file navigation/refactor, stale event refusal and long-gesture cancel.
- Actual collaboration canonical echo, rollback and checkpoint restoration.
- Real IME, RTL and grapheme behavior. CPU motions count code points; offsets
  are UTF8 bytes. This is not full Unicode grapheme navigation.
- Zep-specific Vim text objects, named registers, f/t/% motions, macros,
  visual-block mode and extended ex commands. Current authored Vim covers
  normal/insert/visual/line modes, counted basic motions/operators, unnamed
  yank/paste, repeat, undo/redo and basic write/quit commands.
- Line-number/breakpoint gutters, block caret, inline diagnostic markers and
  anchored completion/hover geometry need generic view primitives plus authored
  policy. Logical Ln/Col and Problems presentation exist.
- Windows execution, locked-file/permission behavior and historical Unicode
  content migration remain distinct from MinGW compilation.

## Historical evidence scope

Installed SDK3 `d862a268337dcae8d6a842d34d3f099135e89e96e1bacaa31f118815223e51a3`
accepted isolated Docs (1,711,261 bytes) and Text (1,413,804 bytes).
Coordinator SDK3 code-control checks pass 10 cases/53 assertions, and rooted
storage/document/authoring checks pass 67 cases/1619 assertions. These receipts
are compiler and bounded native checks; whole-editor rendered acceptance and
quarantined full native/render suites are not established by them.

The SDK5 candidate's coordinated code-control selector passes 11 cases/54
assertions in 0.178s, including the real seven-field delegated key event, typed
positional decode and retained Unicode buffer after the control changes.
`workspace-authoring-retry1` passes 89 cases/2383 assertions, including all 23
absent document operations and 31 absent semantic operations. The final SDK5 workspace-services selector passes 73 cases/1738 assertions.
The SDK5 package superseded that interim candidate receipt; current validation
is recorded in the canonical migration evidence linked above. Rendered whole-editor acceptance remains pending.

## Historical SDK5 validation record, 2026-10-14

The following records the earlier package before later source promotions and
current validation. Pending/staged statements here describe that checkpoint only.

SDK5 packaging and bounded CPU validation now pass. Installed build
`cef077585ca28f1edfb80ccc00d6344026bec1bf347104974b1303d08d210a21`
has manifest SHA256
`4aab3609c818da3813741e160ab972068e9540e0206a3773af8253921c230baa`
and engine source snapshot
`5cb31f58592436059bd0ed49e44013551d2063eb045e6f17bf3128b0f0346ba9`.
All five required groups passed, along with owning workspace-authoring,
workspace-services, host-services, authored-surface, code-control, event-payload
and random-isolation selectors. Strict installed-SDK packaging also passed.
Whole-application SDK5 compilation and Home startup remain pending at this
update. Future SDK6 work is staged and unvalidated. These CPU/package results do
not establish rendered interaction or browser acceptance.

