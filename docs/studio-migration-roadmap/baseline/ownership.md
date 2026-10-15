# Studio ownership and parity boundaries

Snapshot: Studio `78cb4d4c47242db9f7af2b9573653d9756050e99`, paired source-only
engine `64183a1c4ba5b367bf35604ddae45fa616fd3bd0`. This records current contracts
from central registries, selected bodies and public/internal declarations. It
does not claim all 155 product sources were audited function by function.

## 1. Command and input authority

`commands.csv` accounts for all 123 command enum members and string IDs. The
registry is shared by authored UI, menus, palette and shortcuts. Its macros
expand fourteen asset-creation commands, four scene tools and seven code
commands; none is omitted as an implicit command. Enum ordinals index the
in-memory `prefs.shortcuts[]` array; saved JSON uses stable command string IDs.
The [preference reader](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_prefs.c#L312-L325)
accepts the two old workspace command names when a current key is absent;
the [writer](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_prefs.c#L474-L479)
writes overrides by `studio_command_id(c)`. Unknown shortcut keys survive saving,
while retired workspace aliases and removed known overrides do not. Preserve
that actual ID/override/load-save behavior. An enum reorder alone is not evidence
that the persisted preferences require migration.

| Boundary | Exact current owner and contract | Migration proof required |
| --- | --- | --- |
| Authored event routing | `studio_panel_route` maps `studio.<id>` to `studio_command_named` and `studio_command_run_with`; another `<source>.<action>` prefix goes to that registered source's action callback. A known command remains routed even when its enable guard refuses execution. | Preserve known/unknown event results, argument conversion/default `-1`, command enable checks and one execution owner. |
| Enable/execute | `enabled_with` inspects document state, scene selection/gizmo, host/guest/runtime, collaboration, platform and indexed argument. `studio_command_run_with` calls it before dispatch. | Preserve guards/error order and downstream authority. A new generic host function that performs the entire existing command is not a policy migration. |
| Presentation | `studio_command_describe`, `studio_command_checked`, binding/conflict helpers and registry defaults feed the UI. | Preserve dynamic label, checked/disabled state, override/alias rules, context conflicts and platform chord display. CSV defaults are not the complete dynamic UI. |
| Keyboard arbitration | `studio_shortcuts` first respects modal gizmos, selects keyboard/hover area, gives focused fields their edit keys, filters repeat, prioritizes paused debugger, protects gameplay input, then handles early commands, Designer, play, Members, Assets, area UI, Output and editor commands. | Preserve this order, same-batch event state, focus loss, capture/release, Space-hold navigation and remapped chords. Calling the command directly cannot prove arbitration. |

Sources: [event routing](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_panels.c),
[guards and dispatch](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_commands.c#L1329-L1771),
[keyboard arbitration](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_commands.c#L1831-L1909).

The dispatch table's downstream calls are an audit boundary, not a claim they
are interchangeable. Save/undo, hierarchy, code, viewport, runtime, collaboration,
export and preference commands retain their own storage/effect contracts below.

## 2. Panels, shared state and view instances

`panel-sources.csv` records all 44 registrations in their actual sync order.
`studio_sources_new` adds every variable before the Rml data model exists.
`sync_source` dirties only the variable whose callback reports a change.

- A source with `panel == NULL` syncs each refresh. Otherwise its model-local
  `shown_at[]` gates normal sync to mounted panels.
- A binding write triggers resync, but hidden/stale bindings do not gain
  write-back authority: `wrote && shown` is passed to the callback. This prevents
  hidden values undoing newer state.
- `studio_sources_mounted` synchronizes a newly mounted source before marking
  it visible; `studio_sources_acted` synchronizes the matching action prefix.
- Preview `studio_sources_show` sets `preview=true`, synchronizes with
  `wrote=false`, and must apply no product changes. Do not run both a native
  and authored mutation callback to compare results.

Sources: [common synchronization](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_sources.c#L1332-L1442),
[44-entry registry](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_sources.c#L2382-L2457),
[callback contract](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_internal.h#L2840-L2867).

The [current state declarations](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_internal.h#L1063-L1310)
distinguish these owners:

| State | Current storage/lifetime | Consequence for duplicate views |
| --- | --- | --- |
| Scene/documents/runtime | `studio.scene` points to application scene-authoring state; `project_root` is borrowed from the application. | A second mounted panel is not a second scene or document authority. |
| Preferences/layout | One `studio.prefs`, applied copy, revisions and saved scene-view state per Studio instance. | Theme, density, shortcuts and persisted layout remain coordinated; mounted controls cannot independently save competing copies. |
| Areas and focus | `studio.areas[STUDIO_AREA_COUNT]`, hover/focus/key area and maximized state; area headers/controls have per-area mounts. | Separate mount/focus geometry exists; it does not imply every editor filter/selection is per area. |
| Assets | Shared cache, kind/filter, selected/marked/anchor/reveal and thumbnail owner in `studio`. | Preserve the actual shared selection/filter behavior before proposing per-instance state. |
| Outliner | Shared filter and stable actor-identity range anchor/active values in `studio`. | Filter/range operations must use identity, not visible row positions retained across structural changes. |
| Graph/Members/code | One `studio.graph`, `studio.members`, `studio.code`, presentation, shown member/node and current document references. | Rendering two views must share source/history and retain the existing active-document behavior. |
| Output/Properties/Inspector | Shared output filter/references/calls/row window, property search/tab/gesture context, lazily owned Inspector. | Do not mistake temporary model JSON or a mounted field for authoritative data. |
| Model-local synchronization | Each `studio_sources` owns its seen write/save revisions, tokens/page, settings/keymap caches, visibility stamps and preview flag. | These are model/preview bookkeeping, distinct from shared product state. |
| Queued input | Studio owns queued SDL events and per-frame keys/modifiers/wheel/Space state, replayed in order. | Consuming from two implementation paths duplicates effects or changes ordering. |

[Area mount declarations](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_internal.h#L2797-L2802)
and [model-local fields](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_internal.h#L1458-L1494)
support those distinctions. Complete callback reads/writes, transient caches and
hidden-input cleanup still require feature-local body review. These declarations
do not establish arbitrary duplicate-view behavior beyond the recorded fields.

## 3. Document and history authority

| Domain | Current owner and lifetime contract | Evidence and remaining check |
| --- | --- | --- |
| Executable source and graph view | `asset_code` owns exact editable text and the last valid overlay. Invalid typing cannot replace that overlay. Source and metadata share one history; checked refactors reject stale source. Gesture coalescing ends on ordinary edits/replay; execution location requires unchanged text for the paused generation. | [asset_code.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/asset_code.h). Header contract; actual save/overlay teardown and every caller still need body review. `studio-source-edit` and `studio-debugger-source-history` cover limited integration, not all generation replacement. |
| Graph document navigation | `ed_keep_document` commits then moves key/saved snapshot/undo/redo into a document record; it clones the source presentation or asset and retains location/link selection. `visit_document` validates the target and restores the saved record. `release_history_asset` frees presentation or asset according to ownership. | [document.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/document.c#L15-L142). Selected bodies read; all capture/restore/reload paths remain an explicit function-level audit. Do not create a second graph-side executable-source history. |
| Scene mutation history | `scene_history` owns cloned values/strings/type names; app code owns actor/prototype replay. Group scopes cannot nest; no-op/empty steps preserve redo; failed grouped replay reverses completed entries only. Peeked values are borrowed until mutation; complete only after successful replay, rebinding new entity handles. | [scene_history.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/scene_history.h). Declaration-level contract; all mutation/replay callers need audit. Scene/outliner move selectors cover bounded identity/checkpoint/replay cases. |
| RML/RCSS and Designer/Text | `ui_buffer` owns both text parts, saved bytes/hash, revisions, publication and shared undo/redo. `ui_buffer_join_edit` combines a multi-part change into one step; typing coalesces by part/time. Remote collaboration replay may leave local stacks unchanged. | [ui_buffer.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/ui_buffer.h). Do not substitute copied JSON as the source or separate Designer/Text histories. |
| UI code-editor projection | `ui_code` uses the buffer's single history; the code widget keeps none. Reconciliation maps edits from its last shared text into the current buffer; undo/redo steps the buffer. | [ui_code.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/ui_code.h#L11-L65). Header contract only; exact stale edit/error/part switching and one-undo Format behavior are still required. |
| Disk and saved state | Text, graph metadata and dirty/history states are distinct. `asset_code_save` rejects diagnostics/conflicts; `ui_buffer` separately records saved bytes and overlay state. | Save/reload/failure/retry/atomicity and browser durability are not proved by an in-memory source-edit pass. Their owning implementations remain to audit. |

## 4. Asynchronous and deferred work

These are current owners, not permission to move host policy into opaque native
controllers. Header-described work is not assumed to run on a worker thread.

| Work | Verified owner/effect/lifetime | Required preservation or unresolved boundary |
| --- | --- | --- |
| Native export child | `studio.export_game` owns process handle, destination folder and toast. Availability rejects hosted/guest/own-project/missing-root/already-running states. Unsaved edits refuse export. `studio_export_game` launches the SDK `chibi export` with redirected log; `studio_export_poll` observes completion; `finish` reports log/outcome and releases the handle. | The existing close path `studio_export_free` destroys its handle and deliberately lets an export finish on its own. Do not claim existing cancellation or introduce kill-on-close as parity. Browser export is unavailable in this implementation. [studio_export.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_export.c). |
| Thumbnail queue | Studio owns `thumbs`; requests retain/cache/queue identities. `studio_thumbs_frame` runs after emission before UI draw with a frame budget; pending work wakes frames. Renderers can return busy/partial; cancellation drops a busy job. Saved-content disk images are keyed by content; previews read authored state without mutating it. | [studio_thumbnails.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_thumbnails.h). Interface contract only. Audit stale revision checks, close/free, eviction, retained worlds and all kind renderers before migrating; no stale-arrival proof is claimed. |
| Collaboration presentation | `studio_collab_receive` copies peer client/name/presentation, removes/frees departed peers and unfollows their saved view. Follow saves local preferences; unfollow restores them. `studio_collab_after_frame` publishes owned captured JSON then frees it, throttled by the current 0.08-second gate. `studio_collab_free` releases peer strings/array. | [studio_collab.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_collab.c#L325-L476). Current presence/UI ownership only; transport queues, thread affinity, reconnect and accepted document mutations remain separate body audits. A shadow path must record outbound effects rather than send twice. |
| Collaboration command replay | `studio_collab_apply` permits its known presentation/view-command families; host undo/redo and stale tab-key resolution are explicit. The `text` family at this layer reports initialization rather than pretending to apply. UI-buffer collaboration has its separate history contract. | [studio_collab_apply](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_collab.c#L526-L579). This selected function is not the complete collaboration feature; audit `src/editor/collab/*` separately. |
| Filesystem observation | `tree_watch` owns root and sorted path/mtime/size records plus next due time. First poll records a root; scans occur at most once a second; changes are owned strings/array released by `tree_watch_changes_free`. `tree_watch_note` records a self-written file. | [tree_watch.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/tree_watch.h). This is polled deferred work, not evidence of concurrent callbacks. Check write/observe ordering and conflict prompts in callers. |
| Source publication/runtime | `asset_code_set_source_world` binds project preparation/publication while retained immutable generations keep diagnostics/navigation safe. Source/graph invalid drafts and runtime-held state remain separate. | Asset-code interface plus engine publication tests only; owner close/reload/cancel and last-generation reclamation across the actual application need targeted integration. Do not duplicate the separately owned capture/schema/query/read foundations or B06 audit. |
| Browser persistence/import/retry | Source tree and published build provenance are identified, but browser bootstrap/storage callbacks were not audited here. | Browser dirty-versus-durable state, unload protection, failure/retry, concurrent saves and revision isolation remain explicit gates. Build/deploy success does not satisfy them. |

## 5. Feature-to-evidence map

Every row needs identical visible state, action/document/history outputs and
deterministic same-platform pixels where applicable. The selectors named below
are only narrow available contracts; none was run in this report. See
`selectors.csv` for admitted commands, missing routes and exclusions.

| Feature family | Available narrow evidence surface | Still missing for conversion acceptance |
| --- | --- | --- |
| Shell/menus/palette/settings/shortcuts | Complete command registry; source-edit/source-presentation prerequisites; some source-edit shell assertions | First-open, enabled/checked transitions, remaps, navigation/focus, state persistence and every platform |
| Outliner | `studio-outliner-move`, `studio-scene-move`; exact shared filter/range fields | Full filter/range/rename/hide/lock/spawn, duplicates, large scroll, error traces and rendered goldens |
| Assets | `studio-asset-pointer`; partial panel-idle work counts | Open/delete/import/batch undo/stale thumbnails, large virtualization and cache/retry ownership |
| Properties/Members | `studio-source-edit`, `studio-panel-idle`; source edit/presentation CPU contracts | Mixed/computed/override fields, every scrub/type/cancel edge, identity/reacquisition and all types |
| Text/code | Source edit, publication/navigation/format prerequisites; paused source-history selector | Code/KDL keys and follow-up selectors are unadmitted; complete IME/Unicode/Vim/typing groups and native platform input |
| Graph | Graph visibility/pointer/source-edit plus graph model/presentation prerequisites | Complete wire/search/refactor/fold/context/pointer workflows, hidden input and large live-debug graphs |
| Debugger | `studio-debugger-source-history` | Valid live generation replacement, alias IDs, all pause/step/error/owner states |
| Designer/Inspector/UI buffers | Shared buffer/history declarations; UI-document/authoring/format prerequisites; bounded Inspector reads in panel-idle | Live edit/slots/styles, stale targets, tree/multi-part history, editor arbitration and pixels |
| Scene/viewport/gizmos/colliders | Scene move/camera/gizmo routes; collider route has 26/28 mismatch | Repair admission independently, complete gesture matrix, transforms, projection, source-vs-runtime selection and pixels |
| Runtime/playback/keep changes | Engine execution/publication prerequisites | Actual Studio play/pause/step/stop, world/task lifetime, keep-changes, source/runtime changes and cancellation |
| Document authority/save/reload/history | Header contracts and limited scene/source integration | All active/inactive tabs, invalid drafts, external writes, conflicts, failed saves, multi-file transactions and checkpoints |
| Export/platform dialogs | Export body and historical CI game export | Current-pair process/persistence failures, folder action, cancellation disposition, native dialogs and browser limitations |
| Async jobs/thumbnails/watch | Selected ownership declarations/bodies above | Stale arrivals, removal/reload/close, failure/retry, retained resources and precise effect/thread contract |
| Collaboration | Selected presence/view-replay body; scene-move replay subset | Session/reconnect/authority/order, rejection/retry, shared document history, browser durability and single outbound effect owner |
| Idle/performance | Existing two-actor panel-idle contract only | Actual small/typical/stress traces, cold/warm separation, allocation/copy/retention/peak, host calls and accepted budgets |
| Final shipping/cross-platform | Exact source/package identities only | Fresh original/candidate/authored three-way acceptance, per-platform goldens, browser serial/threaded where supported and removal of old policy paths |

## Remaining function-level inventory

The complete path register is reconciled, but the 44 callbacks' transitive
function bodies, all commands' downstream targets, 155 sources/62 headers and
all ten browser-host files are not yet an exhaustive ownership audit. Before a
feature migrates, record each touched function's inputs, owned/borrowed data,
document/history authority, effects and thread contract, invalidation/close/reload
behavior, evidence selector and native/authored disposition. Resolve ambiguous
shared-versus-instance state using actual callers and rendered behavior.

No temporary whole-file native label is approved here. A retained mechanism
still needs an exact OS/foreign ABI/crash or measured-kernel justification; its
portable policy remains in migration scope. The next audit should start at the
Outliner command/callback consumers and scene/document identities, after the
original native oracle can be exercised. This inventory is ready to support
that work without treating a source-name mapping as a finished conversion.
