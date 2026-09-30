# Editor refactor progress

The original objective remains the scope. The usability review and agreed Inspector concept establish the direction; they do not complete the native refactor.

## Shared search and node picker

Native changes in progress:

- Shared ImGui search helper now includes the clear action inside one field boundary. Files, Scene, Behavior outline, and add-node search use it. Clear appears only for a nonempty query and restores text-entry focus.
- Search accessories have adjacent hit regions. An initial overlapping implementation prevented an immediate pointer press from focusing the input because ImGui's overlap mode defers hover recognition. The native Files search test reproduced that regression; the adjacent-region implementation passed it.
- Add-node search appears first and receives focus on open. Results scroll in a bounded child region, search remains visible, and keyboard selection requests scrolling into view. Filtering resets the highlighted result.
- Native regression coverage checks empty results, preserved entity selection, the clear control staying within the field, typing immediately after clearing, popup result bounds, and typing/Enter insertion.

Validation: `make test` passed, including native search checks, add-node keyboard insertion, the full existing suite, and the headless resize-pane assertion. The wraparound check exposed insufficient bottom space for the final selection highlight; adding bottom spacing fixed it.

The existing floating Inspector checks failed in some earlier runs, as they did during the baseline review; the final run with screenshot rendering enabled passed them. This is evidence of test-environment sensitivity, not a diagnosed cause. A separate `make run` editor remains open; this work has not closed it or modified its live session.

## Prototype history and Inspector rows

Prototype assets now own independent Undo and Redo stacks. Selection no longer destroys them. Editing an unselected prototype records history on that prototype, Reload clears only its own stack, and freeing the authoring session frees all stacks. The native command route returns prototype history errors rather than ignoring them. A regression test checks two prototypes across navigation, independent edits, multistep Undo/Redo, an edit to the unselected document, and isolated Reload. The final full suite passes.

Shared presentation helpers now render identity headers, semantic asset icons, wrapped label/value rows, and two-axis float rows. Prototypes and instances show an identity icon. Files distinguishes prototypes, behaviors, scenes, systems, gameplay data, and folders. Numeric Inspector fields share one row with their labels; Position uses X and Y with the labels outside red/green value borders. Local override reversion uses a compact icon with a tooltip. The two Appearance paragraphs saying that colors are in Material were removed.

A native geometry check verifies X and Y share a row within the Inspector, while existing edit/persistence and override tests verify the controls still change the authored values. This does not yet verify every narrow-width, scale, or long-value case.

Component headings now use a shared 28-point disclosure row and open by default. The previous `SetNextItemOpen` call did not affect our custom disclosure, whose storage always defaulted to closed. Prototype and scene-instance headers reserve a separate trash hit region beside the title; its glyph appears when the row is hovered or the header/action has keyboard focus. Removal remains available while collapsed. Runtime inspection uses the same expanded header without removal. The old full-width Remove Component buttons are gone.

The native header tests verify mouse collapse, Right-arrow expansion, Left-arrow collapse, removal from a collapsed header, and Undo restoring the edited value. Existing field and color transaction checks pass with expanded defaults; the tests now scroll to fields that were previously visible only because other components were closed. The two GPU coverage failures described below remain; the full suite is not green.

## Prototype attachment pickers and retained values

Prototype Components and Behaviors now use the same searchable checkbox popup, opened by Manage beside the section title. Selection changes apply immediately and leave the popup and filter open. Results shrink to fit, with a maximum height and scrolling for longer lists. Escape explicitly dismisses the popup; Done is a dismissal action. Search and clear share one visual field. There are no Saved badges. Active behavior rows open their graph, and component presentation follows registry order so reattachment does not reorder the Inspector.

Detaching a prototype component moves its authored data into an optional `detached_components` object in the prototype file. It is excluded from spawning and restored on reattachment. Clone and Undo/Redo serialize this data with the document. Tests cover edited values, nested fields, owned strings, save/reload, cloning, runtime absence, and reattachment. Behavior deselection removes the prototype's reference and leaves the shared behavior asset intact.

Scene-instance retention and the shared instance pickers are now integrated as described below. Included prototype documents still merge active data under the existing flattened-save model; retained data is local authoring metadata, and a load rejects a prototype type that is both active and detached.

Native picker checks pass for immediate search focus, filtering, toggling without closing, retained component values, shared behavior asset survival, and Escape dismissal. The test caught a real dismissal bug: Escape left the first popup open, so a click on the other Manage control only dismissed it. The shared helper now handles Escape explicitly.

The 2× capture exposed popup clipping at the right edge. Offscreen framebuffer dimensions were normalized only for docking, leaving ImGui's main viewport in pixel coordinates. The shared framebuffer setup now supplies logical dimensions to the viewport as well. Tests independently compare popup bounds against canvas pixels and scale. Three older input/layout checks also mixed framebuffer pane rectangles with logical item/input coordinates; those conversions are now explicit.

Latest validation: all native UI interaction checks and retention checks pass in the picker test log, but `make test` still exits unsuccessfully. Two pre-existing GPU coverage tests fail at 2×: interior border expects blue 9.23 at pixel 28,15 and reads 0; SDF coverage expects red 162.94 at pixel 64,23 and reads 24. Their root cause remains unresolved. These failures occur in render validation before the UI interaction sequence. The runner now reports render-validation results and the first failing UI frame instead of returning an unexplained failure. Do not describe the full suite as passing.

`make build` passes. The headless resize-pane scenario also passes, run separately because the two GPU coverage failures stop `make test` before that scenario.

## Scene-instance retention

Build and the new merge tests pass. Validation log also records passing UI interactions; the same two GPU coverage checks keep the full suite red.

The instance mutation path now captures removed component values and the prototype defaults they came from. `scene_retention` owns these snapshots separately from active ECS data. Scene files serialize them under optional `retained_components` metadata. Component history owns before/after metadata snapshots; actor Delete/Undo preserves the retained state as well. A first-time attachment uses prototype defaults when available.

The first backend operation is implemented in `game_prototype_rebase_component_json`. It accepts a retained single-component value object, the defaults from detachment time, and current defaults. It preserves changed fields and adopts new defaults for unchanged fields, recursively through nested structures. An absent current base preserves the complete retained values. An absent previous base treats existing values as local customizations. Tests cover Health 73 retaining its edited value while maximum follows 100 to 200, independent nested X/Y inheritance, strings, new default fields, missing bases, and invalid/mismatched component snapshots.

Instance reattachment now calls this merge. Prototype refresh captures instance overrides before removal, and restores them when the prototype restores the component. Metadata distinguishes this automatic removal from a manual detachment, which remains absent across prototype changes. Scene loading enforces that distinction even when prototype availability changed between saves. Retained payloads are validated against the component registry before the open scene is mutated. Saving during Play uses authored component snapshots and retained metadata, excluding runtime additions.

Native instance retention tests pass for actor Delete/Undo, component Undo/Redo, default changes while detached, scene save/reload, reattachment of Health 73 with a new maximum of 200, automatic prototype restoration, continued manual detachment, saving during Play after a runtime component addition, and invalid retained field data leaving the open scene untouched. All other UI interaction checks pass; the two GPU coverage checks still fail. First-addition Undo across later prototype changes, component identity migration, and broader field-type coverage remain in the final audit. Shared behavior definitions remain independent of attachment state.

## Instance pickers

Scene-instance Components and Behaviors now use the same Manage/search/checkbox control as prototypes. The previous Add menus are removed. Toggling leaves the popup and filter open. Component toggles use the retained-value mutation path; behavior toggles change the instance attachment without deleting the shared asset.

Native picker tests pass for immediate filtering, repeated component and behavior toggles, Health 73/200 restoration, and shared behavior asset survival. The earlier behavior Undo/Redo tests also pass with explicit Escape dismissal before opening the document menu. `make build` passes. `make test` and the capture run pass all UI checks but still fail the two GPU coverage checks documented above.

## Render validation and shared transport

The two Retina GPU coverage failures are resolved. Cute Framework divides shape antialiasing width by the window pixel scale; these fixed-size test canvases use one world unit per output pixel. Requesting the window scale cancels that compensation and restores the one-pixel band assumed by the CPU reference. No expected pixels or tolerances changed. Focused render checks pass for interior borders, SDF coverage through both renderers, and radiance.

The native UI harness now always renders ImGui to its offscreen canvas. Previously only screenshot runs selected that render target, although every run used framebuffer coordinates. The ordinary full suite now passes, including floating Inspector interaction and the headless resize scenario. This resolves the previously documented failures for this configuration; final scale and narrow-layout review remains.

The intermediate design replaced duplicate Graph and Scene controls with workspace transport; the later contextual Scene revision below supersedes that presentation. Graph retains local actions/Fit/debugging; Scene retains Fit/Reset Camera. The workspace target chooser selects Scene or the selected prototype; ordinary asset navigation no longer changes the target. Running or paused sessions reject target switches and prevent simultaneously launching Scene and Preview. The prototype choice retains its asset index through graph navigation. Stop ends the session and permits another target. Full validation verifies shared command clicks, Play/Pause/Step/Restart/Stop, both runtime targets, duplicate-control absence, paused-session exclusion, preserved Graph/Scene split, and narrow Scene camera controls. Direct target-menu coverage now passes for locked running sessions and selecting Preview after Stop. The test reproduced Escape dismissal leaving the next Stop click ineffective; explicitly closing the menu on Escape fixed it.

A subagent fixed missing nested execution traces: each traversal is retained in its originating run and ancestors with its original timestamp, including across waits and completed child disposal. Focused immediate/delayed direct/nested tests cover exact counts, retained identity/timestamps, and pooled reuse. The tests fail against the original core implementation and pass with the fix under sanitizers. Execution-only presentation also passes durable tests: data values/traversal details remain available without heat or markers, untaken Branch edges remain quiet despite a whole-graph pulse, and active/retained traces use the newest timestamp even when completion order differs. Explicit instance binding still needs implementation.

Gameplay-data backend tests now cover all nine shipped schemas, safe numeric edits preserving extra top-level data, validated names/ranges, separate saved and applied baselines, and failed reload isolation. Native gameplay Files selection/Open now routes to a reflected Inspector with a data icon. Kind comes from JSON rather than filename. Path-keyed documents retain independent history across navigation and integrate Save, Save As, Save All, and Reload without pretending to apply these currently unconsumed data assets to simulation. Invalid opens preserve the previous document/selection and show an error. Gameplay has its own document outline rather than the previous behavior outline.

Full gameplay/transport validation passes, including ordinary build, GPU checks, native interactions and headless resize. New native coverage verifies Files single-click on Alpha.json correctly opens Player-kind data, real numeric typing with grouped Undo/Redo, independent two-document history, Save/Reload, explicit-path Save As, composition preservation, and failed reload/open isolation. Save All and double-click/context Open share implemented routes but still need dedicated gameplay interaction checks.

Graph breadcrumbs now use subdued icon-font chevrons. Native pane/tab close glyphs are 75% of their previous size with their hit regions unchanged, through `cute-imgui-close-glyph.patch` in the existing idempotent dependency-patch mechanism. Existing graph navigation, pane closure, resize, and floating Inspector checks pass after applying the patch. Final transport target-label truncation and icon-size review remain.

The subsequent debug-binding implementation and its validation are recorded below.

## Preview, navigation, and node refinements

The latest `make test` run passes, including native UI interactions, GPU coverage, and headless resize. Raw review transcripts are local diagnostics, ignored by Git under `docs/reviews/**/*.log`; this document records the durable results.

Prototype selection now renders an automatic still in an isolated world without running behaviors. Simulate starts the explicit preview session. Projectile previews default to camera Follow and a two-second Loop, with an origin marker for motion context. Native checks cover the still image, moving camera, recycled preview entity, return to the authored still, and byte-identical authored Scene saves before and after simulation. Paused cameras retain manual framing. Refresh after edits, Loop disabled, and broader lifecycle checks remain for the final audit.

Workspace Back/Forward restores document locations and retains independent graph edits and history. Core checks cover copied locations, forward-history truncation, invalid entries, the 128-location bound, and explicit Reload replacing cached edits. Native checks cover Behavior-to-Prototype navigation, independent Undo, and a new visit clearing Forward. Unloaded Scene files are skipped; cross-Scene restoration and deleted entity/nested graph cases remain to be evaluated.

Math and Branch nodes have tighter geometry and semantic title icons. Generated field setters display friendly names and an unwired target as Self. Native checks verify visible node bounds, compact Branch pins, and the Self label. Safe scalar and pointer-free structured component fields have native setters with explicit target access. Tests cover unrelated-field preservation and rejecting invalid targets. HealthBehavior now sets only current health; AutoWeapon's Direction To function uses the native DirectionTo node. Vector equivalence, serialization, field mutation, and combat checks pass. Fields that own strings or contain pointers still require a separate ownership contract.

The text audit confirms no persistent Saved badge or Inspector Undo/status footer. Successful document navigation now clears stale gameplay errors. Instance behavior detachment uses an unlink icon and a Detach tooltip; runtime node labels use the same friendly names as graph titles.

The subsequent native run verifies that still previews refresh after field edits and Undo, and disabling Loop lets a projectile expire without restarting. Nested graph navigation now retains caller breadcrumbs and each caller's view. Core regressions pass for cross-document return, deleted callers, and invalid node/graph locations.

Keyboard navigation is enabled and shared icon buttons use native disabled and focus handling. Focused Tab, Enter, Space, disabled-button skipping, inherited-disabled, and disclosure checks pass. The first full native run exposed competing native/custom arrow handling in the node picker and component disclosure. Explicit key ownership fixes both regressions; the new full `make test` run passes, including immediate filtered insertion, arrow disclosure, shared icon controls, and headless resize.

The graph header now offers an explicit debug target with actor/prototype names and a Pin control. Pinned identity includes runtime session, full entity generation, behavior asset, and attachment sequence; it resolves each frame without retaining runtime pointers. Native tests verify selecting either of two instances sharing HealthBehavior, preserving a pinned target across selection/navigation, unavailable state after detachment, no silent replacement after reattachment, and explicit reselection. Core tests cover session changes and entity reuse. Semantic comparison blocks runtime values, traces, and debug execution when the authored and applied definitions differ, while ignoring layout/comment/routing changes. Tests cover stale definitions and same-pointer applied edits. Comparison is currently uncached; large-graph cost remains to be measured.

Additional navigation tests pass for skipping deleted entity generations and preserving a playing Scene and its component values during Back/Forward to a prototype. First-time component attachment history now handles later prototype additions: Undo returns to normal inheritance, Redo restores the original authored values, and another Undo adopts current defaults. Manual detachment remains a distinct absence override. These sequences and retained string values through stable-key component renaming pass.

A fresh capture run also passes the full suite. Visual review confirms the new target names and compact axis rows. Collider flag controls still place labels on the right, and the graph debug header uses an extra status row; both remain in the final consistency pass.

Cross-Scene history remains unresolved. Loading another Scene currently stops simulation, clears the single Scene history, and recreates actor handles. Correct restoration needs document-owned authored state/history and actor identity; preserving a separately running Scene also requires runtime ownership independent of the viewed document. Current navigation safely skips unloaded Scene locations rather than loading them destructively.

## Contextual Scene controls

The latest user direction restores duplicate transport in Scene. Its floating Play/Pause, Stop, Step, Restart, Fit, and Reset controls appear while Scene is hovered or focused, including editing and child-control focus. They hide when both hover and focus leave the pane; workspace transport remains available. Narrow panes use Play/Pause plus More. This supersedes the intermediate removal of Scene transport.

`make test` passes, including hover-only reveal, focus-only reveal during editing, hiding after focus/hover leave, all five transport actions, narrow overflow/Fit, existing camera interactions, and headless resize.

## September 6 follow-up verification

Scene and Graph controls now animate pointer entry and exit over 200 ms and reject input immediately when neither
hover nor focus belongs to their pane. Keyboard focus reveals them immediately.
The native suite passes hover-only and focus-only visibility, noninteractive
fade, complete disappearance, and shared transport actions. Updated visible
and hidden captures above were inspected at native resolution.

The same run passes all four WASD keys through SDL and the production gameplay
input route. ImGui navigation focus no longer blocks gameplay keys; text fields
and active widgets still capture them. Navigator passes actual undock, drag,
collapse/reopen, and redock checks.

The graph header and breadcrumb row are removed. Debug target selection and Pin
are in the floating Debug popup, and graph transport shares the workspace
runtime. The menu bar has no transport buttons; the Scene menu selects Scene or
Prototype Preview and exposes runtime status. Operator nodes use a symbol body without a titlebar or repeated pin
labels. These supersede earlier breadcrumb and duplicate-control descriptions.
The final `make test` passes all 4,104 native frames, the core suite under
sanitizers, and the headless resize scenario. Graph transport clicks and short,
narrow graph overflow pass. Focused motion tests verify fade reversal, immediate
keyboard reveal, text entry during a pointer popup's fade, and immediate opacity
on keyboard navigation. All four sidebar glyphs resolve in the merged font.
The icon sweep also corrected component removal from Nerd Fonts' Joget codepoint
to its verified trash codepoint.

Math Expression is available through native Add-node search. Source editing
infers Real inputs and generates an inspectable, read-only function graph.
Tests cover precedence, functions, random sampling, stable pin identities and
wires, scalar normalization, atomic invalid edits, serialization, Undo/Redo,
and generated-view edit guards. Native checks type the example expression,
confirm its two inputs, verify Fit after the node grows, and open its generated
graph. The generated graph and expression editor were visually inspected.

## Remaining original scope

| Work | State and required evidence |
| --- | --- |
| Apple/Emil review and ImGui versus Clay | Review complete; keep ImGui. See review.md and apple-inspector-review.md. |
| Shared runtime and contextual Scene transport | Floating Scene and Graph controls share the selected runtime; no menu-bar transport remains. Controls animate on hover or focus. Explicit pinned debug-instance binding is implemented. Final visual and large-graph cost audit remain. |
| Execution bubbles | Nested traversal retention fixed and regression-tested. Execution-only presentation and untaken-Branch/value tests pass; still verify reported Input sequence, held markers, Branch, multiple instances, and reduced motion. |
| Compact nodes and title icons | Implemented; native geometry and pin checks pass. Narrow widths, long labels, and final captures remain. |
| Inline input editing and other-entity access | Field setters and Self target presentation implemented and tested. Final interaction audit remains. |
| Add-node search bounds, focus, scrolling | Implemented and verified by native typing/insertion, first/last wraparound, bounds checks, and capture. Narrow-window and scale checks remain in the final pass. |
| Integrated search clear | Implemented and verified by native filtering, clear bounds, retained selection, focus restoration, and capture. |
| Workspace Back/Forward | Breadcrumb strip removed by later user direction; core/native history, nested callers, deleted generations, and navigation during Scene Play pass. Cross-Scene document ownership remains unresolved. |
| Smaller close glyphs and consistent asset icons | Semantic Files icons and smaller native close glyphs implemented; cross-surface consistency/scale checks remain. |
| Prototype identity icon | Shared native identity control implemented for prototypes and instances; verified in instance capture. |
| Compact Inspector, axis rules, expanded defaults, header trash | Numeric rows, X/Y layout, expanded defaults, and header trash implemented. Mouse/arrow disclosure and collapsed removal/Undo pass. Other field kinds, narrow widths, scale audit, and disclosure retention across document changes remain. |
| Searchable Components/Behaviors multiselect | Shared native picker implemented for prototypes and instances. Filtering, repeated toggles, retained values, and shared asset survival pass. Final keyboard/scale audit remains. |
| Retained detached settings | Prototype and instance persistence, inheritance, component Undo/Redo including first-addition/prototype changes, actor Delete/Undo, runtime absence, Play-save isolation, malformed-load rejection, and stable-key rename covered. Broader field/history audit remains. Shared behavior assets remain intact. |
| Remove Saved badges and status/Undo footer | Agreed design constraint. Do not introduce these in the native Inspector. Use document Undo/Redo. |
| Editor-created component definitions | Feasibility evaluated in inspector-refinements.md. Schema authoring remains a separate engine feature proposal, not an implemented capability. |
| Automatic still preview and bounded projectile simulation | Implemented; still, Follow, bounded Loop, Loop disabled, edit/Undo refresh, and authored-data isolation checks pass. Broader lifecycle and visual audit remain. |
| Remove tutorial/hint paragraphs | Text audit and identified label/stale-error cleanup complete. Final visual review remains. |
| Gameplay asset selection/open routing | Native reflected Inspector, document commands, independent history, and validation/persistence implemented; single-click/edit/save/reload/failure-isolation tests pass. Dedicated double-click/context Open and Save All checks remain. |
| Component field setters and Direction To | Implemented and tested; shipped Health and AutoWeapon graphs simplified. String/pointer ownership is outside the current safe field-setter contract. |
| Per-document history retention | Prototype history implemented and verified across two documents, independent Redo, nonselected edits, and Reload. Other asset-kind history still needs final audit. |
| Final usability verification | Pending all workflows, narrow widths, display scaling, keyboard, focus, missing assets, and fresh before/after captures. |

Continue from the six implementation slices in review.md, incorporating both Inspector follow-up documents. The goal stays active until native behavior and acceptance evidence cover the intended refactor.

## MathExpression reference and sample conversions

[MathExpression reference](../../math-expressions.md) documents supported syntax
and functions, scalar Real boundaries, System pin-drag creation, and deleting a
generated definition with its calls through Undo. Projectile direction magnitude
and MovementTrail frame emission now use generated expressions. Their focused
float-equivalence checks pass against the existing runtime boundary behavior.
The subsequent conversion update adds explicit Real-to-Byte/Integer/Integer64
edges with truncation toward zero, saturated bounds, and NaN-to-zero handling. Full-suite evidence for the latest combined changes is recorded by
the final validation pass.

Health now combines its two clamps and subtraction in one expression, retaining
the integer setter through a To Integer edge. Actual Damage-event tests cover
integer limits, damage bounds, the living-state guard, and destruction at zero.

The combined `make test` pass succeeds under ASan/UBSan, including the native
4386-frame sequence and headless resize-pane scenario. Native checks create a
MathExpression from Get Movement through search and the featured menu item,
delete its generated definition in Documents, and restore the call and wire
with Undo. Rendered vertex-alpha checks verify the Graph and Scene fades for
both hover and focus.
