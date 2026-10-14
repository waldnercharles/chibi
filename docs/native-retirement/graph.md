# Graph native retirement map

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

This map separates authored implementation from acceptance. Paths beginning
`src/editor/` identify the original chibi-studio implementation, including files
removed later in the migration. Their Git history remains the parity reference.
Authored paths below are relative to chibi-studio
`projects/studio/content/`; shared capabilities are relative to chibi.

## Initial implementation record

These rows preserve an earlier checkpoint; their missing-feature claims are not
a current inventory. Later source admissions below supersede them.

| Original implementation | Authored owner | Shared capability | Current scope |
| --- | --- | --- | --- |
| `studio/studio_graph_live.c`, `studio_graph_view.c`, `graph/presentation.c` | `behaviors/StudioGraph.chibi`, `ui/StudioGraphCanvas.rml/.rcss` | `document_semantics`, `document_graph` | Source and declarative graph inspection, empty function scopes, node/port/edge presentation and graph selection. Graph composition is mounted once by the application. |
| `studio/studio_graph_menu.c`, `graph/search.c` | `StudioGraph.chibi` searchable node palette | `graph_node_choices` | Bounded visible palette with scrolling; declarative nodes and source capability expressions use semantic candidates. Type-aware completion remains incomplete. |
| `studio/studio_graph_edit.c`, `studio_graph_live.c` | `StudioGraph.chibi` proposal policy | `prepare_source_graph`, `prepare_declarative_graph`, `StudioDocumentEdit` | Revision-guarded pure candidates; source connection, expression edit, flow reorder, disconnect, removal and duplicate; declarative connection, disconnect, literal edit and single-node removal. Source multi-selection removal/duplicate use semantic origin lists. |
| `studio/studio_graph_layout.c`, `graph/selection.c` | `StudioGraph.chibi` pointer and selection policy | `prepare_source_layout`, declarative place/arrange candidates | Single-node placement and arrange; Shift selection, rectangle add/subtract selection, snapped dragging and cancel restoration. Multi-node layout explicitly refuses until an atomic candidate exists. |
| `studio/studio_graph_values.c`, `studio_graph_literals.c`, `studio_graph_properties.c`, `graph/pins.c` | `StudioGraph.chibi`, shared `NumberField` and boolean/text controls | typed inspected ports and literal/expression candidates | Numeric/boolean inputs and editable string/expression controls. Complete vector/color/enum/asset controls and source expression text/completion offsets remain required. |
| `studio/studio_graph_nav.c`, `studio_graph_input.c`, `graph/input_bindings.c` | `StudioGraph.chibi` | generic viewport geometry and authored scroll/zoom state | Frame selected/all, measured viewport fit, wire selection/disconnect, keyboard delete/duplicate/frame, drag cancellation. This is a subset of the original interaction contract. |
| `studio/studio_graph_header.c`, portions of `studio_members.c` | `StudioGraph.chibi` graph header | source declaration and declarative scope candidates | Function creation and rename; complete member/signature editing remains required. |
| `studio/studio_graph_edit.c`, `collab/actions_graph.c` source/history integration | `StudioGraph.chibi`, `StudioDocuments.chibi`, `StudioCollaboration.chibi` | shared external-edit leases and canonical proposals; atomic `StudioDocumentEdits` | Ordinary edits flush the source through a correlated lease. Source plus metadata uses exact open revisions or explicit missing-sidecar drafts. No implicit metadata save. Documents now forwards room batches to authored canonical proposal/replay handlers; live room acceptance remains pending. |
| `graph/eval_view.c`, retained execution inspection | `StudioDebugger.chibi` | `workspace_debug`, retained execution inspector snapshots | Standalone stack/value/source inspection is implemented. Graph port value mapping and flow observation presentation are not yet implemented. Display must never execute a producer. |

The SDK3 authored Graph source is promoted to live Studio. Its matching source
copy remains in `.chibi/studio-authored-migration/graph-next/`. It adds callable signature
replacement, enable/disable, macro creation, result naming, selection extraction
to a function or macro, call inlining and selection wrapping in if/while/sequence.
All reuse existing validated source requests and the same canonical proposal
path. These controls are collapsed sections using the shared panel styles.
Scope rows display authored names rather than graph IDs. This source
also adds canonical document undo/redo controls and bounded find-next across node
labels, port names and values. It passed isolated installed SDK3 authoring
compilation (3,440,204 bytes); rendered interaction acceptance remains pending.

Source layout metadata is a canonical companion document. Existing sidecars are
opened at an exact revision; absence is established only by the rooted document
path query. Source and metadata replacement is a grouped history operation,
including unchanged source as the anchor. The Documents owner implements
companion save/close/dirty grouping. An empty sidecar restored by undo means
default layout. In-memory source without a stable sidecar path currently refuses
persistent layout rather than inventing a file destination.

## SDK4 authored implementation

The pure `document_graph_selection` service is implemented with eight capabilities
and two reflected presentation row types. It provides bounded declarative
selection clone/delete/place, source metadata placement, comments, boxes,
reroutes, lexical source copy and declarative copy/paste. Candidates own their
text, metadata and created identities; they do not mutate canonical documents.
The integration owner verified five focused selection cases, including owned
presentation rows after document retirement, in the fresh workspace-services
run (61 cases, 1,416 assertions, 0.430 seconds). The authoring group also passed
(76 cases, 1,765 assertions). These are CPU checks, not editor acceptance.

The matching Graph behavior, canvas styles/markup and presentation-number event
are promoted to live Studio; matching source remains in
`.chibi/studio-authored-migration/selection-next/`. They add
atomic multi-node drags, selection duplication/deletion, editable comments/group
frames, draggable reroutes, and asynchronous clipboard requests with revision
checks. Metadata companions are opened and read before candidate preparation,
and adopted through the canonical grouped proposal. New group identities are
retained after acceptance. The installed SDK4 isolated authoring compiler accepted Graph
(4,273,060 bytes) and Debugger (385,352 bytes); real interaction acceptance
remains pending. Clone
placement for nodes without persisted coordinates has an implemented SDK5 repair
with the clone-coordinate regression admitted in SDK5. Custom group color
controls and shared keyboard routing are also promoted in SDK5; rendered
interaction acceptance remains pending.
The promoted selected-port inspector joins retained debugger values through exact
execution identities and requires frozen source text/path to match the canonical
document. Scoped source completions use the exact original expression range.
Both passed the same SDK4 source compile and still require real application acceptance.

## SDK5 implementation and remaining work (historical)

The following records the SDK5 checkpoint. Current GraphCanvas has structured
input/output pin controls (name, type, default, add/remove and reorder), backed by
`StudioGraphMemberAction` and authored callable-pin edits. Their presence does not
establish rendered interaction acceptance. Other gaps below require current
source verification before being treated as outstanding work.

SDK5 source admission now covers source member/binding inventory, state creation,
rename/retype/initializer edits, typed scalar/vector/enum/color defaults,
comments/groups/reroutes, canonical multi-selection edits, clipboard, exact source
completion/navigation, viewport retention and selection keyboard routing. These
are implemented; they still require rendered interaction acceptance.

Implementation gaps remain:

- Source callable signatures are validated source text; structured per-input and
  per-output controls and every declaration creation form are not yet authored.
- Independent simultaneous graph panes are not implemented. Current authored
  retention stores bounded document/function pan and zoom, with one active graph.
- Clipboard validation may refuse declarative nodes whose local callable
  dependencies cannot be reconstructed. This remains an explicit refusal.
- SDK6 flow activity and diagnostic reveal pass packaged source compilation.
  Their rendered interaction and runtime acceptance remain pending.
- Graph parameter watch policy beyond the retained selected-port inspector still
  needs parity review. Source semantic origins and execution watch occurrences
  remain separate identities.

The authored room batch transport is implemented in the current Documents and
Collaboration sources. The actual SDK3 collaboration controller preflights
without mutation, publishes a decision, and applies only the service echo before
acknowledging the correlated UI request. Its isolated SDK3 compile passed
(2,086,007 bytes). Live-room acceptance of atomic source/metadata proposals,
refusal, replay and preservation of preexisting redo remains pending.
Metadata birth accepts an empty initial draft; nonempty artifact baselines are
explicitly refused. Checkpoint/history protocol consumers were promoted for SDK4; live recovery acceptance remains pending.
file mutations and unmapped visual operations remain disabled in rooms.

## Validation evidence and gates

The installed SDK3 isolated authoring compiler accepted the current
`StudioGraph.chibi` live source, producing a 3,440,204-byte prepared fixture. The
fixture and artifact are under engine
`.chibi/studio-authored-migration/validation/preview_authoring/StudioGraph-fixture`
and `StudioGraph.chibip`. SDK build identity is
`d862a268337dcae8d6a842d34d3f099135e89e96e1bacaa31f118815223e51a3`.
This proves source syntax, typed capability admission
and lowering against that SDK; it does not prove rendered UI or interaction.

The integration owner separately owns current whole-application compile,
bounded native checks, SDK packaging and actual editor acceptance. Their latest
logs are authoritative for those scopes; this map is not a substitute for them.

Before declaring graph migration accepted, close the implementation gaps and
exercise the real authored application: RML event routing; literal scrub commit
and cancel; source flush and lease refusal; undo/redo including first missing
sidecar; save/close companion behavior; layout persistence; stale revisions;
selection and clipboard; compatible wires; exact source navigation; accepted
and refused room proposals; retained values without evaluation. Capture actual
rendered evidence using the installed SDK. No GPU/browser/UI acceptance is
claimed by the isolated compilation result.

The graph viewport has the required `id` and `data-measure` tag. A scale audit
found that presenter width/height are reported in dp while graph canvas geometry
and pointer payloads use physical RML pixels. Frame must convert measured sizes
using the actual context ratio. SDK4 promoted the `get_ui_view_width.PixelRatio` consumer into the actual
Graph behavior. Material and Model orbit consumers likewise divide physical
deltas by the actual ratio. Their measured preview IDs are present in live RML. Non-1 ratio
framing, hit testing and orbit acceptance remain required.

## SDK5 authored source admission

Studio `members-next/` contains searchable seven-row source member and binding
inventories, declaration rename/state type controls, state creation, compatible
input bindings, group ColorField/theme reset, bounded document/function viewport
retention, selection keyboard commands and measured edge autopan. Matching
packed RGBA helpers and clone-coordinate repair are promoted in engine for SDK5;
owning CPU checks and immutable SDK5 packaging pass. Authored consumers were promoted and admitted by the installed SDK5 compiler; rendered interaction remains pending.
Reflected scalar/vector/enum/color port controls, safe string literals, atomic
color edits, initializer expressions, Cut and shared definition/reference/call
inspection are also promoted. Exact flow activity mapping remains in progress.
Viewport retention is not independent simultaneous graph panes.

The installed SDK5 isolated Graph compile produced a 5,163,803-byte artifact at
`.chibi/studio-authored-migration/validation/preview_authoring/StudioGraph-SDK5.chibip`
using build `cef077585ca28f1edfb80ccc00d6344026bec1bf347104974b1303d08d210a21`.
The fixture copies every current event schema. Typed member/field controls,
packed group colors, exact initializer origins, navigation and focus routing
pass compiler admission; their user interactions remain unaccepted.

Preparation profiling found the expanded `Publish` function alone required
67,694,495 bytes of verifier scratch. Canvas and Properties publication now run
as separate authored event handlers guarded by document/revision; collaborative
candidate preparation retains a correlated request token. The source compiler
accepted the result without changing the runtime preparation limit.
Graph descendants publish pane focus before stopping pointer propagation;
text fields mark text editing so graph deletion/navigation commands are refused.

SDK6 native activity snapshots and source-node execution mapping pass the
coordinator's owning checks, required groups and immutable package. They expose retained generation,
owner and canonical execution keys without executing graph producers. Native
shader diagnostics also append source-local graph/node IDs, empty when linker
provenance cannot be proved. SDK6 authored activity and diagnostic navigation
consumers are promoted after installed compiler admission; no runtime evidence
is claimed for them.

Installed SDK6 source admission used build
`5db13b8a022cda3512c8333ebe7cd9715c00a03a3ba38d1b5493e2ad5d2ac711`,
manifest `38af19419f05aceb26628a3bbe13b89ed2cfe800e0b0127d435a8d78809bcea7`.
Isolated artifacts are Graph 5,550,181 bytes, Debugger 419,590 bytes and Visual
712,423 bytes under `validation/preview_authoring/` in the task scratch.
`source6-promotion.json` records the nine promoted source files and hashes before
root-owned canonical formatting. Existing behavior/view mounts are unchanged.

Activity matches retained generation, owner, query tuple and canonical bulk node
execution keys. Frozen source path/text must match the current document. Initial
snapshots prime counters; subsequent exact count changes drive heat on mapped
nodes and actual predecessor flow edges. Only active indices animate, and Reduce
Motion suppresses those effects. Retired observations are labeled. Diagnostic
Show node verifies the canonical document revision and exact graph/node, selects
the graph pane and frames after a bounded wait for viewport measurement.
These are source/compiler assertions; real editor tasks and rendered evidence
remain the acceptance gate.

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

