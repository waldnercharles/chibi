# Editor design and implementation plan

The September 6 usability review and proposed next refactor are in
[Editor usability review](/Users/cwaldner/Personal/c-blueprints/docs/reviews/editor-ux-2026-09-06/review.md).
That review includes current screenshots, validation results, and an ImGui/Clay
assessment. Its recommendations are not yet implemented.

## RmlUi Studio, September 25, 2026

RmlUi is the editor's UI foundation. LichtFeld Studio and
[Blender](references/blender/README.md) are references for compact desktop
controls, consistent styling and a prominent viewport. Application
models, authoring commands and Undo/Redo remain in C. RML owns structure and RCSS
owns presentation; a pass-through C binding (`src/ui/rml.h`) contains the RmlUi API, and the renderer integration and kit are C.

Studio (`src/editor/studio`), the full-window RmlUi editor, is the only editor.
The ImGui editor, with its docking workspace, panels, graph canvas and retained
menu windows, was removed on September 25, 2026. Later sections that describe
ImGui panes, docking or the ImGui graph canvas record that editor's history.
Studio's direction, component kit and progress are in
[the Studio design](prototypes/studio/DESIGN.md). Studio has Blender-style
areas, a command palette, the Outliner, Properties, the scene viewport, the
behavior graph, Assets and Output. It edits the C authoring model with one undo
step per gesture.

ImGui remains only as the `--play` window host, showing the game image and
supplying input. In-game UI, including the pause and outcome menu, is GameUi
documents drawn over the game image (`src/render/ui.c`).

`src/ui` owns the RmlUi binding, FreeType font rendering and the Cute geometry
backend, shared by the editor and game UI; `src/editor/rmlui` owns the kit and
its resources. The geometry renderer does not depend on ImGui.

Sizes at the C boundary are logical pixels. The RmlUi context and render canvas
use framebuffer pixels, and RCSS uses `dp` for display scaling. Application
deactivation destroys retained documents, context and GPU resources before unloading
the application library. Plain game presentation does not create this UI.

The kit's documents (`studio.rml`, `graph.rml`) live in `src/editor/rmlui/resources`
and load at startup from the source checkout. Their styles are the Studio project's
theme (`projects/studio/content/ui/StudioShell.rcss` and its `theme/` imports, among
them `workspace.rcss` and `graph.rcss`), so Studio edits its own chrome and RCSS edits
apply at once; restart the editor after editing the kit's RML. CMake pins
RmlUi 6.3 and FreeType 2.14.3; no system installation is required. Panels
consume C state and emit C commands rather than expose engine objects to
markup.

`build/test_suite --rmlui-kit` exercises kit reconciliation, number fields,
menus and commands without a GPU. The `--rmlui-render` smoke test renders the
retained UI while preserving queued scene geometry, and under a scissor an
earlier pass left. `--rmlui-render-gles` repeats it on the web build's GLES
backend where a desktop GLES driver exists, and skips otherwise. `make
test-render` includes both.

## Current goal, September 9, 2026

The [arena roadmap](arena-roadmap.md) now prioritizes one beautiful playable
iteration with the existing weapon and fixed arena. Reactive flooring, readable
faceted actors and behavior-authored combat feedback prove the editor's utility.
Dash, extra weapons, transformations, upgrades and bosses are deferred. Reusable
engine capabilities belong in reflected components and ordinary asset workflows.
The [development notes](arena-development.md) record implemented increments and
validation; the [latest reference review](research/arena-reactive-slice.md) defines
the visual comparison and authoring acceptance still required.

## Goal recorded September 5, 2026

The current request expands the earlier slices below. Build a minimal professional
game editor whose visual language can express Neon-Swarm's gameplay. Visual polish,
responsive layout, and interruptible feedback are part of the work. Existing panes
are implementation details, not a required navigation model.

Keep ImGui as the native input, text, and docking implementation for now. The
working graph editor, reflected fields, and native tests make a replacement costly
without yet solving the authoring problems. Build the product's presentation above
those mechanisms. Reconsider this decision if native interaction limitations block
the intended workflow.

Use the Content file tree already present in the code. Consolidate prototype
components and behavior attachments with their properties so users do not have to
visit three panes to edit one object. Preserve simultaneous graph and scene views.
Diagnostics and activation debugging belong on demand, while play controls should
identify the scene or isolated prototype they operate on.

Neon-Swarm's movement and weapon sources establish the next gameplay requirements:
component reads and writes, time, queries, spawning from authored defaults,
destruction, random values, and stateful weapon composition. Keep game rules visible
in graphs. A native node that hides the weapon implementation would defeat this
goal. The agreed gameplay scope is movement, projectiles, enemies and particles.
Shields are excluded. Upgrades are outside this minimal pass. Further graphics
refinement, wobble/pluck and a possible 3D SDF renderer are deferred.

## Scope checkpoint, September 6, 2026

The minimal editor now has a searchable Files/Scene navigator, contextual
Inspectors, document history and local functions, automatic graph arrangement,
component field outputs and single-value conversions. Scene properties, actors
and behavior attachments support Undo/Redo. Play, Pause, Step, Restart and Stop
preserve the authored scene; behavior graphs remain accessible during Play.
Layout transitions, disclosures and graph activity respect reduced motion.

The supplied graphs express movement, facing, weapons, projectiles, enemy pursuit,
contact damage, bounded waves and impact particles. Runtime tests cover both
scene and isolated-prototype hosts. SDF geometry, interior borders, material
buffers and the ported radiance cascades have GPU-readback tests. The renderer
uses the source lighting equations with RGBA16F intermediates, a black background
and a reduced working resolution. Further particle appearance and lighting
refinement belong to the deferred graphics pass.

The sections below record implementation as it happened. Earlier statements that
something remains work describe that stage, not a new requirement. Shields are
excluded throughout the current scope, and the reference screenshots guide
presentation without requiring their pane layouts or 3D features.

## Apple guideline polish, September 6, 2026

The Navigator uses inset rounded selection, consistent row heights, and aligned
icon and label columns. Native tree and selection controls retain keyboard,
context-menu and drag interactions. The Scene no longer displays a control tutorial
or hover help. Inspector labels use a quieter visual
hierarchy, with neutral empty states and fewer empty runtime sections.

The Inspector is movable and dockable again. Its local collapse control hides it;
reopening preserves its floating position or dock. The default layout places it on
the right. Navigator resizing and reduced-motion behavior remain available.
This also follows Apple's [panels](https://developer.apple.com/design/human-interface-guidelines/panels)
and [outline views](https://developer.apple.com/design/human-interface-guidelines/outline-views)
guidance on movable auxiliary windows, selection-driven contents and remembered
disclosure state.

`make test` passes, including native Inspector dragging, collapse/reopen position
preservation, redocking, scene selection, asset drag-and-drop, and Navigator
resizing. Startup and populated-Inspector screenshots were visually reviewed.

These choices follow Apple's [sidebars](https://developer.apple.com/design/human-interface-guidelines/sidebars),
[layout](https://developer.apple.com/design/human-interface-guidelines/layout),
[typography](https://developer.apple.com/design/human-interface-guidelines/typography),
and [design principles](https://developer.apple.com/design/human-interface-guidelines/design-principles):
consistent alignment, recognizable controls, contextual continuity, and reversible
interaction. A filesystem can require deeper nesting than Apple's usual sidebar
recommendation; search provides direct access without introducing another pane.

Keep Inter and the existing Font Awesome glyphs for the cross-platform build.
Apple's [downloadable font license](https://developer.apple.com/fonts/) prohibits
embedding those SF font files in software; using the macOS system font through
platform APIs is a separate integration. Apple's system-image license limits
symbols to qualifying Apple-platform applications, with additional restrictions
on branding and particular symbols. See [Xcode agreement section 2.10](https://www.apple.com/legal/sla/docs/xcode.pdf).
[OpenSymbols](https://github.com/OrchardKit/open-symbols) converts third-party icon
sets into custom-symbol format. Each set retains its original license; it does
not provide an open license to Apple's SF Symbols library.

## Presentation foundation delivered, September 5, 2026

Function and macro calls have a visible open control in their header, alongside
field disclosure when applicable. Double-click and the context action remain.
Opening is deferred until the next draw boundary. The navigation row shows the
actual caller path and a graph picker that lists Events and all local functions.
Ancestor clicks restore the caller camera and select its call; they do not arrange
again. Long names truncate and older ancestors use an overflow
menu when space is limited. Caller history stores IDs and camera snapshots,
skips deleted graphs, and clears on document replacement or external navigation.
Native tests click two levels of calls, return to the root breadcrumb, and use
the picker in both directions. Headless tests cover invalid/deleted ancestors.

The host formats entity values using the runtime instance's world. Scene actors
show their names and isolated-preview roots show their prototype names; stale
entities show Deleted entity. Raw core formatting is unchanged. The formatter
survives editor graph/document rebuilds and is cleared during host teardown.
The inspected main capture shows Player_1 and compact numeric values such as 600.
Numeric inputs retain stored precision while displaying fewer trailing zeros.
`make test` passes, including native interaction and pane resizing.

EnemyBehavior now uses a local pure Move Toward function. Its main graph has
9 nodes and its calculation has 8. The latest inspected main-graph capture opens
at 118% zoom, and the calculation at 102%. Calls display their authored names
rather than a Call Pure Function prefix. Native tests double-click the call and
click Back, checking graph identity, preserved camera/positions, and selection.
Headless checks cover nested calls, macros, invalid/deleted targets, and reused
IDs across document replacement/loading. Gameplay tests preserve pursuit behavior
in both runtime hosts, and the full `make test` suite passes.

Extract Function is available from the Graph menu and node context menu. Select
pure calculation nodes, name the function, and extraction replaces them with a
call. Connected inputs and outputs become typed function ports; shared inputs
are deduplicated. The original layout stays in place and Undo restores the
complete asset. Opening the new function uses the default automatic arrangement.
Execution nodes, state reads, existing calls, and split pins are excluded from
this first version. The disabled action explains incompatible selections.
The automatic weapon graph uses the grouped functions described below. Native
tests verify selection, naming, extraction, exact Undo, and Redo. Core tests cover
conversions, arrays, shared inputs, and atomic rejection; the full suite passes.

Single-field components now convert implicitly to their field type. Movement
feeds Speed directly in EnemyBehavior, and Input can feed CF_V2 operations.
Arithmetic inputs unwrap these components automatically; general wildcard pins
retain whole-component values. Containers and unrelated nominal types remain
incompatible. Existing Break nodes remain valid for saved graphs.

AutoWeaponBehavior now has an eight-node event graph, a 12-node Fire Ready
Projectiles function, and an eight-node Fire Projectile function. The main graph
fits at 120% zoom; the burst function fits at 77%. Direction To, Check Firing
Interval, and Offset Position remain pure helpers. Only Elapsed is persistent;
the outer function captures Origin, Direction, and Damage once for the burst.
Both-host gameplay tests hold initialization, change the owner and weapon, and
verify the entire current burst retains its parameters while the next recaptures.

Fit includes exact cubic wire extrema, including loop returns and routed
segments. Executable functions and macros retain child execution across waits
and breakpoints. The debugger can reveal the current node inside a nested graph.
The event graph is now readable at its opening scale. Loop wires inside the burst
function still cross nodes and need better routing.

Components with several fields now retain their whole-value output and expose
field outputs through a header disclosure control or the node context menu.
Collapsing removes unused fields and preserves connected fields and their IDs.
Expanding makes room below the node without rearranging the entire graph.
Each field reads the same aggregate snapshot, including values produced by Set.
The old input Split command is removed from the UI because its metadata-only
implementation did not provide executable field projections.

Field outputs are persisted independently of the old split-pin representation.
They apply to reflected component Get/Set outputs and local function parameters
and results. Function entries with several vectors provide a field menu; labels
such as Direction X identify the source. Expanding fields never adds arguments.
Call signatures ignore projection rows and report stale signatures explicitly.
Fire Projectile now wires Direction X/Y straight from its entry, removing its
last Break node. Nested field expansion and dynamic event signatures are supported.

Health has 10 top-level nodes after its Behavior Expression conversion; Automatic Weapon has eight. Native tests cover
field expansion, connected-pin preservation, and choosing Origin fields without
changing existing Direction fields. Pure, executable, and delayed function calls
pass parameter/result projection and saved roundtrip checks. `make test` passes.
Variable nodes now resolve their names from the asset and display Get Elapsed or
Set Elapsed instead of anonymous Get Variable/Set Variable labels.

Player now has graph-authored automatic targeting and firing. Its tests cover
both runtime hosts, cooldown remainder, varied aim, projectile defaults, and
held initialization after target removal. Manual Fire remains available.

The default workspace now puts Graph above Scene, with roughly 68% of document
height for Graph. Existing saved docking layouts are preserved. View > Reset
Workspace applies the new default. Graph receives space reclaimed from hidden
diagnostics, preventing an accidental 50/50 split. Focus continues to restore
pane visibility and geometry.

Pin labels use readable words consistently in measurement, rendering, and input
positioning. Fit can zoom below the former 35% floor. The automatic-weapon
check verifies that every pin and the complete wire curves remain visible.

Enemy pursuit is now an authored behavior using reusable nearest-entity lookup
and vector normalization. Scene and isolated-preview tests verify target changes,
diagonal speed, range, and arrival. The native test opens the behavior with a settled arranged camera and no preview
errors, then navigates into its Move Toward function and back.

Projectile contact and health response are now authored behaviors. Collider
radius, category, and mask are prototype fields. Contact Begin exposes the other
entity and a world-space Point (`CF_V2`); Send Damage queues an event whose recipient graph subtracts health and
handles death. The event catalog is shared by the app and native tests. Native
code detects contacts and schedules delivery, with no hidden health mutation.
Scene and isolated-preview tests verify the same authored combat rules.
The native test opens ProjectileHitBehavior and verifies its arranged canvas.
Asset-only previews leave instance-dependent values blank, while retaining pure
literal previews. Missing runtime context no longer creates a graph-error banner
or a misleading zero entity handle. The full suite passes and the resulting
contact-graph capture has been inspected.

Playback controls live in the viewport header and name their target, either Scene
or the previewed prototype. Play/Pause remains visible; Step, Restart, and Stop
are in its adjacent menu. Restart resets and runs the same target. Stop resets
the scene while paused, or closes an isolated preview. Inspector controls no
longer duplicate this toolbar. Starting a prototype preview remains available
from its Inspector.

Playback clicks preserve graph inspection. Only interaction with the scene
itself selects the scene document. A running prototype preview keeps its own
stable prototype identity, so browsing another document cannot redirect Restart.
Native tests click every playback action for both targets while WeaponBehavior
remains inspected. Restart invalidates the previous projectile handle, allowing
authored scene entities to be recreated. The full `make test` suite passes,
including viewport resize, and the final toolbar capture has been inspected.

Graph readability now includes opaque entity and name pins, with visible entity
connections. Named events display their trigger, such as `On Fire`. Node titles,
Inspector titles, and the creation menu split C identifiers into readable words;
search accepts both those labels and the stable registry names. Numeric fields on
the canvas use their full width for values instead of clipping them behind step
buttons. A native WeaponBehavior capture verifies the entity connection and the
32.000 spawn offset; the full `make test` suite passes after these changes.

- Embedded Inter with its license, replacing the default monospaced interface font.
- Neutral dark controls and tabs, a dotted graph canvas, and quieter node headers
  with semantic color retained as an accent.
- Graph actions and activation debugging now use compact popups. The former single
  toolbar row clipped most actions in a split workspace.
- Icon-button hover color eases from its current value; press feedback is immediate.
- Normal startup no longer explicitly resets the dock layout. Deterministic test
  scenarios and screenshot runs still reset it. Restart restoration has not yet
  received a native interaction test.
- Screenshot mode now targets the app canvas with ImGui before readback, fixing the
  black captures described below. Inspected the complete rendered editor before
  and after this pass.

`make test` passes with sanitizers and warnings, including native clicks to enter
and leave Focus, restoration of pane widths and hidden-pane choices, access to
the graph arrangement menu, and the existing resize scenario.

This is a foundation, not acceptance of the final design. The captured editor still
has a crowded prototype pane and an overly small initial graph overview. Next:
consolidate selection and properties, improve initial graph framing, persist
workspace preferences, and implement the valid-edit preview contract below. Then
exercise graph-authored gameplay and refine the resulting real authoring workflow.

## Prototype Inspector consolidation

Prototype editing now uses one Inspector. Each component expands to show its
fields and removal action; component creation, attached behaviors, and isolated
preview controls are in the same pane. Removed the separate Components pane and
its selection state. The behavior member navigator remains available for graph
documents, but prototypes no longer use it as a second attachment editor.

Expanding an attached behavior only expands its contents. Opening its graph is an
explicit action. Undo and Redo use the Edit menu instead of duplicate buttons
in a component pane. Content search now has a clear button.

The native integration test reopens a saved prototype, expands GameTransform,
types `73` into its x field, and checks the saved value. It then clicks Document >
Undo and checks that saving restores `42`. `make test` passes with sanitizers,
including the existing scene/graph, Focus, and resize scenarios. Inspected a
complete native capture of the consolidated Inspector.

The app and native test now share the existing screenshot readback function.
`CHIBI_UI_SCREENSHOT=/tmp/inspector.png make test` optionally captures the
Inspector edit, or the last rendered frame if a required UI item is missing.
Captures aid inspection; assertions remain the correctness gate.

Remaining selection work: scene and graph focus should share one Inspector without
hiding an open graph. Persistent preferences, live preview, initial graph framing,
and the broader gameplay authoring work also remain incomplete.

## Shared scene and graph selection

The open graph now has its own document identity, separate from the active
Inspector selection. Inspecting a scene entity or prototype preserves that graph
and its editing state. Clicking the graph restores its document context without
reloading it or resetting the workspace. The navigator draws before the Inspector
so selecting an object updates properties in the same frame.

Removed the duplicate Scene Runtime pane. Scene instance properties and graph
properties share the Inspector. Runtime trace selection for the canvas follows the
open behavior; it no longer overwrites the scene Inspector's chosen attachment.
Opening an attached prototype behavior now selects its graph document explicitly.

Undo and Redo availability follows the active graph or prototype history. Scene
undo is not implemented yet, so those commands are disabled for scene inspection
instead of accidentally undoing a background graph.

Native tests click an authored node, select Player in the outliner, then click
back into the graph. They assert the Inspector's rendered content, unchanged graph
identity and node count, retained node selection, and simultaneous graph/scene
geometry. `make test` passes with sanitizers. The optional UI screenshot now ends
with the scene Inspector beside the retained selected graph node.

Keyboard-only document switching, scene undo, persistent preferences, live preview,
initial graph framing, and the broader gameplay authoring work remain incomplete.

## Live preview implementation — 2026-09-05

Behavior graph revisions now trigger preview at the next scene frame boundary.
Compatible edits replace the running definition in place, preserving instance
identity, variables, and compatible node scratch state. All affected instances
must be idle before replacement; queued executions defer the entire update.
Invalid graphs retain the last valid snapshot and populate compiler diagnostics.
The menu reports live, waiting, error, or restart-required status. Its explicit
restart action applies incompatible state changes and recreates behavior instances.

Unsaved changes are tracked independently from unapplied changes. Save, Save As,
and Save All persist working data without implicitly restarting behavior instances.
Reload uses the same state-preserving preview path. Merely synchronizing identical
graphs does not mark them unsaved.

Scene and isolated prototype previews use the same live-definition owner and
receive working graph revisions independently. Each target keeps its own state
and last valid snapshot. Newly authored behaviors register automatically; launching
a prototype synchronizes its definitions. Graph inspection follows the active
runtime target, including an isolated preview that is paused. Borrowed struct state, variable schema changes, and incompatible stateful nodes
require restart. Busy updates currently recompile on retry; candidate caching is
still needed for large graphs. Runtime tests cover atomic deferral, state retention,
changed execution, invalid edits, recovery, and restart requirements. Native editor
tests verify that an unsaved graph command reaches both snapshots automatically,
preserves the isolated instance, and changes the graph inspected by the debugger.

## Workspace persistence and graph opening — 2026-09-05

The user requested automatic arrangement by default when opening a graph and
parallel subagent work where useful. New editors, asset opens, and switches to
another graph now use the existing Arrange operation once the canvas is measured.
Position changes are authored edits with one undo entry. Reopening the active
graph and changing inspection focus preserve the current arrangement; Undo does
not immediately arrange again. Canvas resizing preserves zoom and the viewed
center. Source-node navigation centers before rebuilding screen-space geometry.

Pane visibility, reduced motion, and Focus mode are saved alongside the existing
ImGui dock settings. Focus saves the underlying pane choices, so leaving Focus
after relaunch restores the expected workspace. Scripted/headless runs disable
settings persistence. Native tests round-trip a settings file through separate
ImGui contexts and verify overlapping-node arrangement, Undo, same-graph reopening,
and switching away and back. The Undo test also exposed and fixed a stale host
pointer: synchronization now reacquires the editor-owned working asset after
history replacement.

## Graph camera and default framing — 2026-09-05

Wheel zoom, explicit Fit, and source-node reveal now ease briefly toward a target.
Canvas presses cancel camera motion immediately; panning and dragging retain direct
control. Reduced motion applies the same target immediately. Opening and automatic
arrangement still frame immediately. Native input tests exercise easing, settling,
reduced motion, and interruption without later drift.

The default dock split assigns more space to the graph and less to sidebars. The
shipped InputBehavior fits at roughly 60% instead of 40% in the 1920×1080 capture.
Scene projection keeps its original world extent and center while canvas sizes
change, extending the visible world to preserve aspect ratio. This avoids placing
the player near the bottom merely because the Scene pane became taller. The editor
still needs further visual refinement; the new screenshot is not acceptance of
the overall product design.

## Authored projectile lifecycle — 2026-09-05

Player now attaches WeaponBehavior. Fire spawns the Projectile prototype, and
ProjectileBehavior owns movement and expiration through graph nodes. Spawn waits
for a host boundary and initializes the entity before attached Begin events run.
Scene and isolated preview share this runtime; newly spawned entities receive
Tick and are rendered without adding them to the authored actor list. Native
collision/damage remains a separate temporary path.

See `gameplay-authoring-plan.md` for the implemented ordering and verification.
The editor's event menu exposes Fire. The optional native screenshot now ends on
WeaponBehavior after exercising play and firing. This establishes a small authored
projectile loop, not complete Neon-Swarm coverage or finished interface design.

## Direction agreed with the user

Build a quiet, document-centered C23 ImGui editor for our own authoring needs.
Synapse influences layout and visual presentation, not the engine architecture.
Use independently written code and our own assets; its AGPL source is not a
source-port dependency. Keep existing graph authoring, reflection, Prototypes,
systems, scenes, and preview capabilities throughout the transition.

The central workspace is not a single exclusive view. Documents remain dockable
and splittable: an event graph and the scene must be usable side by side. Opening
or focusing a document must not destroy the user's arrangement.

## Design references

The [Blender screenshot references](references/blender/README.md) record the
user's desired level of polish: compact aligned controls, restrained chrome,
clear pane boundaries and inline node editing. Adapt the useful patterns to
Chibi while keeping primary actions discoverable and secondary controls
contextual. The supplied images retain their visible version labels; additional
sourced Blender screenshots must show version 5.2.

Loaded the user-supplied
[apple-design skill](https://raw.githubusercontent.com/emilkowalski/skills/refs/heads/main/skills/apple-design/SKILL.md)
directly after it was identified outside the active skill catalog. Also reviewed
the game-ui-ux layout guidance, Cute Framework integration skill, and Apple's
published [Layout](https://developer.apple.com/design/human-interface-guidelines/layout)
and [Sidebars](https://developer.apple.com/design/human-interface-guidelines/sidebars)
guidance (including Apple's documentation JSON).

Apply grouping, alignment, sufficient space for essential content, progressive
disclosure, and discoverable sidebar visibility controls. Keep the navigator
discoverable on first launch. Avoid copying platform materials or introducing
decorative animation before the basic interactions work well.

Translate the skill's web-oriented examples into native ImGui behavior: instant
press feedback with activation on release, precise pointer-following drags and
splitters, predictable restoration, and no input lockouts during transitions.
Keep common controls discoverable rather than hiding everything to appear minimal.
Use consistent typography and spacing to establish hierarchy. Any later motion
must be interruptible and offer a reduced-motion alternative; springs, momentum,
and translucency are not requirements for precision editor controls.

## Workspace

- Navigator on the left: assets and scene contents, searchable and grouped.
- Documents in the center: freely dockable and splittable. Provide a discoverable
  graph-and-scene arrangement in addition to manual tab dragging.
- Inspector on the right: one place for properties of the active selection.
  Document context follows deliberate focus/selection, not mouse hover. A visible
  scene must not steal graph selection just because it renders another frame.
- Diagnostics on demand: show actionable errors and a compact indication when
  hidden; do not permanently spend a quarter of the window on internal state.
- Related controls next to their content. Advanced runtime details are optional.

Every auxiliary pane needs a visible close/hide affordance and a checked View
menu entry to reopen it. Focus mode must restore the previous visibility choices.
Reset Layout is an explicit recovery command, never a side effect of switching
documents. Preserve ImGui docking persistence; separately define persistence for
any new visibility preferences before claiming they survive restart.

## Editing and preview contract

Working edits, disk saves, and the last valid running definition are distinct.
The intended default is automatic preview of valid edits, at a safe frame
boundary, without requiring Save. Incomplete or invalid graphs keep the previous
valid version running and report the error at its source. State-incompatible
edits require an explicit Restart rather than silently resetting the world.

Scene and isolated preview use the same editing commands. Preview controls and
input must identify their target; typing in a graph must not control the scene.
Shader and particle graphs are future document types, not existing capabilities
or requirements to add generic plugin infrastructure now.

## Ordered implementation

1. **Pane usability and simultaneous views.** Quiet defaults; close/reopen panes;
   restore layout; a graph-and-scene split; retain manual docking. Exercise actual
   ImGui interactions and assert both central views have usable, disjoint areas.
2. **Context and consolidation.** Consolidate related properties and document
   controls, remove redundant status displays, and make inspector ownership
   explicit when multiple documents are visible. Preserve document edits and
   selection while moving focus between graph and scene.
3. **Immediate behavior feedback.** Edit a behavior used by a scene entity, see
   the changed behavior without saving, reject a broken graph without replacing
   the valid runtime, recover after fixing it, and distinguish restart-required
   changes. Cover these outcomes through shared commands and native widgets.
4. **Refine the presentation.** Consistent spacing, restrained accent colors,
   readable labels, keyboard focus, useful empty states, and narrow-window
   behavior. Inspect the real rendered editor, not only model tests.
5. **Architecture follow-through.** Assess the paused architecture experiment
   against the working workflow before integrating anything. DLL loading,
   remodule, and additional document types remain separate milestones.

## Verification

Run `make test` with sanitizers, warnings, and ccache; retain graph/model regression
coverage and the native resize scenario. Add behavioral UI assertions for changed
interactions rather than tests that duplicate visibility implementation. Verify
hide/reopen does not lose document state and that switching document focus does
not collapse a custom split. Formatting is required. Distinguish tested behavior
from remaining design intent when reporting progress.

## First slice delivered

Integrated pane close/hide/reopen controls and a checked View menu. Diagnostics
start hidden; graph compilation with errors, warnings or optimization notes reveals
Compiler Results. Diagnostic source rows navigate to the associated graph and node.
New/reset layouts place Scene and Node Canvas side by side. The explicit
`Reset Workspace: Graph + Scene Side by Side` preset rebuilds the entire dock
layout while retaining pane visibility choices except for reopening both central
views. Manual docking remains available and is not reset on document changes.
Pane visibility currently lasts for the process; it is not saved across launches.

`make test` passed in the main working tree with sanitizers, warnings, and ccache,
including native Content hide/reopen interactions, simultaneous pane geometry,
hidden-Scene focus handling, and the resize scenario. The preset test invokes the
shared action after hiding both views; it does not yet drive a manual re-docking
gesture or the title-bar close button. Formatting passed in the implementation
worktree. The screenshot command returned a black canvas due to the existing
capture path omitting the screen-rendered ImGui layer, so visual polish has not
been accepted from that capture.

Context consolidation, focus mode, persistent visibility preferences, immediate
behavior preview, shader/particle graphs, and the broader architecture changes
remain future work. This slice improves workspace controls; it does not complete
the document-centered redesign.


## Remaining presentation work

Collision category editing now uses one host-provided catalog: Player, Enemy,
and Projectile, matching the authored prototype bits. Query inputs, Collider
Make inputs, and actor/prototype Collider properties share the picker. It keeps
unknown bits and displays them in hexadecimal; clearing named choices preserves
unknown values. Each selection is one Undo action. Native tests toggle Player
alongside Enemy and undo to Enemy; catalog tests cover unknown bits, combinations,
and bounded formatting. `make test` passes. The catalog is currently host-defined;
project-authored categories and named read-only output formatting remain work.
The large mostly empty Inspector and loop wires crossing nodes remain visible
gaps. Make property editing contextual and improve loop routing before treating
the editor as a finished professional interface.


The left side uses one full-height Navigator with Files, Scene and Document tabs
in a single row. Scene is directly accessible; Document appears when an asset is
available. There is no second row of navigation choices.
Files retains the existing searchable tree and create/open/save interactions.
Scene selection remains available while a behavior stays open. Behavior outlines list
events, local functions, macros, and variables; prototype outlines list their
components and attached behaviors. Switching navigator views does not replace
the graph or change its selected nodes.

The legacy Files/Outline preference remains readable as NavigatorOutline. Existing pane visibility
and focus keys remain readable. Reset Workspace creates one full-height left
dock; existing layouts without Navigator move the former Content dock to the new
window. Native tests cover file operations, scene-inspection transitions,
full-height geometry in each view, focus restoration, and preference roundtrips.
`make test` passes and the behavior Outline capture has been inspected.

The Outline still uses dense section headers, and the Inspector consumes a large
column for little information when nothing specific is selected. Further work
should reduce empty property chrome, unify typography and spacing, and improve
wire routing. Prioritize movement, enemies, projectiles, particles, and their visual feedback.
Waves, upgrades, and weapon variants are optional extensions.


## Contextual Inspector and contact damage

The graph Inspector is now submitted only for a selected node, link, or variable.
This does not change its saved visibility preference; explicit hiding stays in
force. It does not take focus when appearing. Canvas resize preserves pan and
animation targets, so selecting a node does not shift the target between clicks.
Back restores the saved camera exactly. Behavior settings use the Outline gear
popup, preserving selection; closing a changed name edit commits one Undo action.
Outline headings use title case and omit the empty Macros section. The inspected
weapon graph uses the reclaimed width and fits at 140% with no Inspector.

`make test` passes, including double-click/open/Back, extraction, category picker
interaction, Inspector availability versus visibility, and settings rename/Undo.
PlayerContactBehavior adds graph-authored basic enemy body damage, with source
rules and both-host tests recorded in gameplay-authoring-plan.md.

The user explicitly excludes shields. Focus the remaining work on movement,
visuals, projectiles, enemies, particles, and editor polish. The design target is
a professional IDE with familiar VS Code navigation and Apple-like restraint,
spacing, and responsive feedback. Reproducing every feature of Neon-Swarm is not
required.


## Compact graph command bar

The redundant single-document tab strip is removed. At normal widths the graph
uses one command bar containing document context, Back, caller breadcrumbs, the
graph picker, and Graph/Fit/Debug actions. Narrow layouts wrap the actions to a
second row. Breadcrumb buttons have neutral idle backgrounds and the graph picker
uses a drawn chevron. Navigation IDs, camera anchoring, and saved views remain
consistent with the existing interaction contract.

Validation: `make test` passes, including native graph navigation and resize,
both-host impact emission, paused effects, expiry, and reset. The compact command
bar and authored impact graph screenshots were inspected.


## Color authoring and simpler data access

Particle Color uses a swatch and picker with alpha and eight-digit RGBA Hex input
on the graph and in the Inspector. A picker session creates one Undo entry;
opening it without changes creates none. The host identifies color inputs, so
the graph editor does not depend on gameplay node names.

The event Outline and graph toolbar consistently use Events. Movement reads its
single-value component implicitly. Projectile logic exposes only needed fields
on the original Get/Set outputs, preserving the post-write snapshot used for
expiry. Impact particles consume Transform directly through its vector alias.


`make test` passes after these changes. Native coverage edits a color to
3366CC80, closes and reopens its picker, and restores F0CD69FF with one Undo.
The captured picker was inspected. Remaining visual debt includes Inspector
pin-type/debug labels, redundant empty Outline groups, and canvas clipping when
an Inspector appears. These need product-level treatment before calling the
editor polished.


## Properties-first Inspector

The selected node Inspector starts with data inputs. Connected inputs name their
source and show its available evaluated value; they do not offer an unused
local default. Execution pins are omitted. Outputs and Advanced controls are
collapsed initially. Event signatures, Sequence outputs, disabled state, and
field controls remain available in Advanced. Empty Functions and Variables
sections are omitted from the Outline, with creation available from Add.

Next visual work should make actor appearance authored data. The current renderer
colors all health-bearing entities alike. A small Appearance component with
width, height, and RGBA color would let Player, Enemy, and Projectile choose their
own presentation through the same component workflow. Shape should wait for
usable named choices and appropriate drawing primitives.


Validation: `make test` passes for the properties Inspector and shortened
component labels. Native checks cover read-only connected Position, editable
Color, hidden execution properties, and the Advanced section. The Inspector
screenshot was inspected; right-side canvas clipping remains visible when the
panel opens, so camera behavior still needs a deliberate interaction solution.


## Responsive fitted views

Opening or fitting a graph enables a fitted view. A later canvas resize schedules
one refit after the resize and mouse activity have settled for the double-click
interval. Held buttons, active widgets, popups, and graph gestures defer it. The
existing camera transition handles motion; reduced motion applies the result
immediately after settling.

Manual pan, wheel zoom, and Reveal leave fitted mode. Back restores the saved
camera and suppresses the refit caused by restoring its selected node; later
resizes can adapt again. This keeps click targets stable while allowing a fitted
graph to regain its full framing after the Inspector opens.


Validation: `make test` passes for responsive fitting. Native checks verify the
camera remains fixed within the double-click interval, all pins and wires fit
after the Inspector settles, manual zoom survives pane changes, and reduced
motion retains framing. The resulting screenshot shows the whole impact graph
alongside its properties.

Editable controls inside each node now share a column based on its widest
editable input label. This aligns numeric fields, flags, color, and checkboxes
without changing node dimensions or pin locations.


## Authored appearance

The 2D Appearance component provides Width, Height, and packed RGBA Color. Rendering
requires Transform and 2D Appearance, independently of health or projectile gameplay.
2D fixture prototypes include Appearance for editor coverage; Player is blue, Enemy
coral, and Projectile gold. Missing Appearance deliberately means no visible body.
Nonfinite positions/extents and nonpositive extents are excluded from drawing.

The shared color picker is used by graph inputs, prototype defaults, and scene
instance overrides. A prototype picker session creates one Undo record and
refreshes instances through existing override-preserving updates.

The Cute dependency renders built-in shapes as SDFs and includes automatic
selection of tiled versus instanced batches. No explicit tiled override is set.
Neon-Swarm's custom entity, glow, wobble, and material effects are not yet ported;
plain authored rectangles are the current presentation. Rendering reference:
https://randygaul.github.io/cute_framework/topics/renderer/


Validation: `make test` passes with Appearance. Tests cover distinct defaults,
prototype roundtrip, sparse instance overrides, and both-host reset behavior.
Native color editing updates the scene instance and one Document Undo restores
its original color. Startup now registers the complete generated component list;
previous fixed lists omitted newly authored components and prevented spawning.


## Scene expansion

When Graph and Scene occupy sibling docks, hiding Graph temporarily transfers
the central dock role to Scene. This releases the empty graph region without
rebuilding the layout. Showing Graph restores its central role and the saved
split. Other custom dock arrangements remain unchanged. Temporary origin IDs
are saved with workspace preferences and cleared by Reset Workspace.

Next SDF styling should use Cute's existing shape, outline, and glow calls.
Neon-Swarm's player is a chevron outline; enemies use diamond/circle silhouettes
with dark fill and bright borders. A named Shape choice plus border width/color
and glow radius/color can express these using the Appearance workflow. Zero
values should preserve existing styles. This approximates the visual language;
it does not claim to reproduce Neon-Swarm's complete material/lighting pipeline.

Validation: `make test` passes. Native checks confirm Scene fills the released
document space and showing Graph restores the original split height. The
expanded scene capture was inspected and shows all five authored appearances.


## SDF appearance styles

Appearance now includes a named Shape choice (Box, Circle, Diamond, Chevron),
Border Width/Color, and Glow Radius/Color. The renderer uses Cute's native SDF
shape, outline, and glow calls in both preview modes. Circles use the smaller
of Width and Height as their diameter. Unknown shape values remain visible in
the editor; the renderer falls back to a box. Missing new fields default to zero,
retaining the original box appearance without outlines or glow.

Player and Enemy use dark fills with bright borders and restrained glow; the
projectile uses a gold circle. Health remains gameplay state and is not drawn as a
viewport overlay. These styles approximate Neon-Swarm's silhouettes and emissive
look; its wobble, radiance/material, and complete lighting pipeline are not
implemented. Shape uses shared named-choice controls in the graph,
Inspector, prototype defaults, and scene instance properties.

Validation: `make test` passes for SDF styling, legacy defaults, persistence,
instance overrides, and preview reset. Native tests choose Circle by name and
verify that one Document Undo restores Chevron on the scene instance. The
expanded preview capture was inspected for silhouettes, borders, and glow.

Visual fidelity target: replicate Neon-Swarm's SDF visuals as closely as possible.
The current built-in Cute shapes are a starting point, not the finished visual
design. Compare against the reference shaders and entity data for exact shape
geometry, orientation, fill/border colors, glow, deformation, and animation.
Custom SDF/material rendering should be used where built-in styles cannot match
the reference. Shields remain excluded from the game scope.

## Scene camera navigation

Fit Scene frames visible authored bodies, including their border and glow extents.
Wheel zoom keeps the world point under the pointer fixed; middle-button dragging
pans the view. Reset View in playback options restores the default camera.
Double-clicking an actor in the Scene outline frames that actor without changing
the open behavior graph. Camera gestures consume viewport input so they do not
also control the game.

Scene and each prototype retain independent cameras for the editor session.
The first preview of a prototype frames its visible content automatically;
returning to an existing target restores its center and logical dimensions.
Aspect expansion remains a rendering concern, so repeated switches do not
progressively widen the saved view. Reset View affects only the current target.

While Scene is paused, clicking an authored body selects it and shows its
Inspector. Clicking empty space clears selection. Picking follows the rendered
shape, excluding glow, and chooses the last drawn authored actor at overlaps.
Dragging preserves the initial grab offset, commits one Undo on release, and
restores the original position on Escape. A selection outline supplies feedback.
The viewport reports pointer state in render coordinates; the app owns selection,
world-coordinate conversion, and transform edits. Game input remains inactive
while the viewport is used for editing.

Transform dragging and numeric position editing share a history of before/after
actor transforms. Replay preserves runtime entity identity; stale generation
records cannot modify a replacement entity. Restart and actor removal clear
this history. Scene save finishes the active edit before writing. This history
currently covers transforms, not every scene property or actor lifecycle action.

Validation: `make test` passes, including the app resize scenario. Native checks
cover effects-inclusive Fit, an off-center pointer's world anchor during zoom,
exact middle-drag displacement without a press-frame jump, Reset View, and
double-click actor framing. Pointer tests account for ImGui's pixel rounding.
Additional native checks launch Projectile and Enemy previews, verify first-open
framing, change their cameras independently, and confirm that revisiting
Projectile and returning to Scene restores the exact logical camera dimensions.

Scene-edit validation: `make test` passes for direct picking, screen-to-world
dragging, one-step Undo/Redo, and Escape cancellation without adding history.
Numeric X editing spans several typing frames and one Undo restores the complete
transaction. History tests cover multiple edits and stale entity generations.
These exposed and fixed missing argument parentheses in the shared array last
and removal macros. The native capture was inspected; selection outlines retain
a thin screen-space width at close zoom.

## Neon-Swarm visual fidelity follow-up

The source player uses a white stroked polygon with local points `(16,0)`,
`(-4,-9)`, `(0,0)`, `(-4,9)` and thickness 1. Its origin must stay fixed rather
than recentering this asymmetric geometry. Movement controls orientation, which
persists when idle. A separate orientation component preserves Transform's
existing implicit vector conversion.

The source bullet is a cyan capsule with endpoints `(-6,0)` and `(6,0)`, radius
2 (the weapon configuration overrides it to 3), and explicit antialiasing 2.
The rusher enemy is a radius-14 diamond with dark gold fill and a 2-unit bright
gold interior border. The old blue filled chevron, gold circular projectile,
and coral diamond have now been replaced with these source geometries and
8-bit color equivalents. Border coverage and lighting still need the source
shader treatment.

The actual raster SDF is in `shaders/hrc/entity_raster.shd`, with shared functions
in `shaders/sdf.shd`; `shaders/entity.shd` defines the GPU layout. Match interior
border coverage, then radial wobble and traveling pluck deformation, hit and
beat envelopes, and emission. Cute custom shapes can supply a bounded SDF, but
radial warping needs a conservative distance bound for correct tiled culling.
Local glow is insufficient for the source's radiance, occlusion, bounce, and fog;
the HRC material/radiance/composite path remains part of the fidelity work.

## Source SDF geometry

Appearance adds Chevron Outline and Capsule plus Stroke Width. The player uses
the exact asymmetric four-point outline with width 20, height 18, and stroke 1;
its actor origin is preserved. The projectile uses an 18-by-6 capsule (radius 3,
endpoints at x ±6). Rusher fill rounds the source RGB to `140F00`; its border is
`FFCC00`. Existing filled shapes remain available for authored content.

Pure geometry functions supply polygon points, local bounds, and shape picking.
The renderer, Fit, and selection bounds use the same definitions. A stroked
shape's hollow interior is not selectable; its actual outline is. Capsule caps
and non-square circle extents are respected. Outline/glow padding is added only
for visual framing. Rotation applies consistently to rendering, bounds, and
inverse-transformed hit testing.

Validation covers exact source points, stroke bounds and hollow picking,
capsule endcaps, legacy shapes, invalid geometry, Stroke Width persistence,
prototype overrides, and native selection/drag/Undo with the new outline. The
native preview was inspected for the white outline and cyan capsule. Glow is
still Cute's local SDF glow; this is not yet a full lighting match.

## Authored orientation

Orientation is a separate single-value component storing radians. Transform
retains its vector alias and existing implicit conversions. Missing Orientation
means zero rotation. The player and projectile prototypes start at zero radians.
Their movement graphs call local Face Direction functions using Atan2 of
`(-direction.y, direction.x)`, matching the renderer's Y-up coordinates. A zero
direction retains the previous heading; graphs still move older entities that
lack the optional Orientation component.

Rotated polygon bounds use actual vertices, capsule bounds use endpoints plus
radius, and circle bounds remain unchanged. Picking transforms the pointer back
into local coordinates. Rendered shapes and selection bounds use the same
angle, including the player's asymmetric local origin.

Validation: `make test` passes. Both hosts exercise cardinal, quadrant, and
nonunit player/projectile directions, idle heading retention, and isolated
preview reset. Geometry checks cover rotated asymmetric bounds, capsule caps,
inverse picking, and invalid angles. The native test frames a quarter-turn
player and the capture was inspected. The cramped numeric Inspector rows found
in that capture have since been replaced with the property layout below.

## Numeric property layout and interior borders

Prototype and instance numeric properties use a label above a full-width input.
Instance overrides reserve space for Revert, and numeric inputs no longer pack
step buttons and trailing labels into the same row. Stable widget IDs retain
typing focus as override markers change. `make test` passes, including numeric
Undo/Redo and a native bounds check that input and Revert fit inside Inspector.
The resulting capture was inspected at the default pane width.

The border comparison found a renderer-level difference: Cute's positive outline
extends outside the SDF, while Neon-Swarm divides coverage into an interior fill
and border. Source coverage is `1-smoothstep(0, aa, d)` for the shape and
`1-smoothstep(-border, -border+aa, d)` for the fill; the difference is the border.
Border-over-fill premultiplied compositing matters for translucent materials.

The implementation extends Cute's shared `sdf_effects` path with an explicit
interior-border mode. That path serves both instanced and tiled draws.
A negative outline width is ignored, and the application post-shader hook does
not have the original distance, so neither reproduces this operation. Keep the
dependency change reproducible against the pinned Cute revision rather than
modifying only the generated build checkout.

The checked-in `cmake/patches/cute-interior-border.patch` adds
`cf_draw_push_interior_border`, paired with the existing outline pop. Configure
applies the patch to fresh or populated dependency checkouts, accepts an already
applied patch, and fails on a mismatch. Cute compiles its built-in GLSL at runtime;
rebuilding consumes the patched shader header without an offline compiler.

Appearance Border Width now uses interior coverage without expanding body
bounds. Glow remains an independent local effect. Interior borders conservatively
disable opaque-cover culling for their draw command, since a translucent border
can uncover the pixels underneath. The existing outward-outline API is retained.

Validation: `make test` passes. GPU readback compares every pixel of opaque and
translucent circles and diamonds against the reference premultiplied coverage
equations, verifies actual instanced and tiled batch execution, and compares both
backends. The border capture was inspected. The patch's already-applied configure
path was also verified. Full radiance/material lighting remains unfinished.

## Source deformation

`GameSdfMotion` carries standing-wave amplitude, rounded angular frequency,
temporal frequency, time, and the source shader's counter-propagating pluck
packets. The shader inverse-rotates into local coordinates before applying
Neon-Swarm's radial displacement, then evaluates the original silhouette.
Picking uses the same coordinate warp. Fit and selection bounds include the
maximum displacement: positive standing amplitude plus twice active pluck
amplitude. Two overlapping packets can exceed the source CPU bound of one pluck
amplitude, so the editor uses the conservative bound.

`cmake/patches/cute-custom-coverage.patch` adds an explicit coverage registration
API. Warped fields are not Lipschitz distance bounds; Cute therefore bins them
using their supplied AABB and skips distance-based tile rejection and opaque
cover claims. Fragment distance is unchanged, preserving source antialiasing
and interior-border widths. Coverage shapes cannot participate in smooth CSG
shape groups, whose bounds rely on a true distance bound. Ordinary Cute custom
shapes retain their original contract.

The optional component's clocks advance in both simulation hosts alongside
particles. Pause freezes them; step advances them; restart restores authored
values. This deliberately uses simulation time rather than Neon-Swarm's global
wall clock. Enemy exposes the component with zero amplitude until an authored
signal drives it. The source itself currently disables pluck every update;
pluck is supported by the renderer without inventing an always-on effect.
Audio-driven beat envelopes and the full radiance/material composite remain
unfinished. Local glow is still an approximation of the source lighting.

The next lighting slice should be a viewport-owned renderer with a narrow
render-list interface. Keep gameplay and CPU particle simulation independent.
Neon-Swarm's `src/render_systems.c` extracts scene color, HDR emission,
absorption from material/filter, albedo, and separate border materials.
An optional material component should express these without expanding geometry
or gameplay components with renderer resource handles.

Port the resource and dispatch core from `src/lighting.c`, not its game globals:
entity bin/raster into material buffers, prefilter, four directional
trace/extend/merge solves, gather, blur, temporal bounce, then composite. The
source shaders are `shaders/hrc/{entity_bin,entity_raster,prefilter,trace,extend,
merge,gather,blur,bounce,composite}.shd` and their shared includes. Resource
ownership must cover resize and temporal-history reset. Cute's pinned revision
already provides compute, storage buffers/textures, HDR formats and mipmaps.
The source disables HRC on GLES3; preserve an explicit fallback there.

The composite adds direct HDR emission, ambient scene color, albedo-lit radiance
and radiance fog, applies the shadow floor/background, then converts linear color
to sRGB and dithers. Source defaults (emission intensity 7.5, ambient 0, fog 1)
help explain the luminous result. Replacing that with a larger local halo loses
occlusion, reflected color and temporal bounce.

Particles should enter the same material render list. Source
`shaders/particles/convert.shd` uses age curves, velocity-aligned stretched
capsules, emission, borders and absorption. Our fading squares remain a fidelity
gap. Shape, size/emission curves and velocity alignment can be added to the
existing CPU particles before adopting GPU simulation or pseudo-3D motion.

Deformation validation: `make test` passes, including native editor checks and
resize-pane. GPU readback exercises the production shader at three phases with
strong standing waves and pluck, opaque background preservation, translucent
interior borders, and actual instanced/tiled execution. Every pixel matches the
independent source equations within the stated GPU rounding tolerance, and the
captured stress case was inspected. CPU tests cover frequency rounding, packet
overlap, bounds, invalid inputs, and simulation pause/step/reset. Paused prototype
frames now observe host time without simulating, preventing accumulated pause
time from jumping particles or deformation forward on resume.

## Material and radiance implementation

The native Scene and prototype views now use the source material raster and HRC
solve. `GameMaterial` exposes linear scene, albedo, filter and emission colors,
independent border colors, emission intensity/multiplier and absorption. Colors
use the existing RGBA8 picker and graph field controls; emission coefficients
remain HDR after multiplication. The source's floating color constants are
quantized to RGBA8 on authoring. Appearance now also exposes antialias width;
zero retains the source's automatic pixel-width smoothing.

Player, cyan capsule projectile and yellow rusher Enemy use their source
material settings. Local Cute halos are bypassed on the material path. Assets
without a material receive a simple emissive fallback. The existing appearance
color/glow controls overlap the new material controls and still need an Inspector
consolidation so users cannot edit an inactive color by mistake.

The renderer owns material buffers and temporal radiance resources separately
from ECS state. The app adapter submits both actors and existing particles.
The current working resolution is half viewport pixels, capped at 640 on the
longer edge. Camera/target changes and runtime restart reset history. The
reference's separate space background is currently black. Source particle shape
and age curves remain unfinished. See `render/README.md` for source provenance,
GPU format adaptation and frame-lifetime requirements.

Validation: `make test` passes, including the native editor and resize-pane.
GPU readback checks raw HDR material buffers, source emissive borders, occlusion,
history reset and resize. The receiver region measured 106.859 without a wall
and 1.469 with the opaque wall in 8-bit output units. The lighting test capture
and the native editor capture were inspected.

Current priority update: the user has deferred further graphics work, including
wobble/pluck polish, to a later pass and may move to 3D SDF rendering. Keep the
verified rendering work, but focus upcoming work on editor usability and core
movement, projectile, enemy and particle authoring. Do not continue visual-fidelity
expansion merely because source rendering work is available.

## Inspector clarity

Component headings and the Add Component menu use display labels such as
Transform, Movement and SDF Motion. Registry names and serialized identifiers
stay unchanged. Material swatches now use their actual field labels, including
Base Color, Emission Color and Border Color, with distinct tracked widget IDs.
When an actor or prototype has Material, Appearance hides its inactive fallback
colors and glow controls and points users to Material. The non-compute rendering
fallback also reads material scene/border colors, so the same property remains
authoritative across backends.

`make test` passes. Native coverage edits Material Base Color, verifies propagation
to the scene instance, and undoes the whole picker transaction. The Inspector
capture was inspected. The user has clarified that radiance cascades may remain
our implementation or use Cute's alternatives; further graphics exploration can
wait while editor and gameplay workflows improve.

## Authored encounter and restart

The shipped `content/scenes/NeonSwarm.yaml` contains Player and a nonvisual
Encounter controller. Scene files can be opened from Files. Other prototypes are
available as assets rather than being instantiated automatically. Startup reports
scene load errors instead of silently opening an empty scene. Native interaction
tests use `test/fixtures/editor_scene.json` to retain their multi-actor coverage.

WaveBehavior uses existing nodes and local functions. It waits for a player,
accumulates simulation delta, and spawns eight enemies at 1.5-second intervals,
alternating 260 units to the player's left and right. Its exposed variables are
CountLimit, Interval and SpawnDistance. A bounded loop catches up complete
intervals, with an explicit loop backedge. Each spawn initializes Transform in a
local function before returning. No player freezes the timer. Enemy pursuit,
weapons, projectile cleanup and impact particles remain their existing behaviors.

The first Scene Play or Step captures authored components and attached behavior
names. Pause/resume retains that snapshot. Restart recreates even destroyed
actors from it, resetting behavior state, runtime-spawned entities, contacts and
particles. Stop restores it and then discards the session snapshot, so subsequent
edits become the next starting state. Scene loading invalidates the previous
session and stops both runtimes before replacing actors. Adding an authored actor
during a session first stops/restores the Scene. Inspector binding follows the
recreated entity handles.

Saving during Play or a paused simulation reads the starting snapshot, including
attached behaviors for actors that have since been destroyed. It preserves sparse
component overrides and does not reset the running simulation. The Inspector
labels the simulation state, explains that edits reset on Stop and Save keeps the
starting scene. Stop remains beside Play in the viewport. Native tests save after changing player
position and health, then again after destroying the player; both retain the
authored scene without changing live state. `make test` passes.
Changing prototype components or defaults ends the Scene simulation before
refreshing instances. This prevents old simulation snapshots from becoming
accidental overrides of newly edited defaults. A native test edits a prototype
during Play and verifies the instance inherits the new value and saves no override.

Outline lists live host-owned entities under Spawned during Play, with prototype
names and stable entity identities. Selecting one opens read-only ECS component
values and its behavior runtime/trace inspection. Double-click frames it. Runtime
selection remains separate from authored actor selection, is validated against
the full entity generation, and clears on destruction or reset. Runtime entities
never enter the scene save list. The host enumeration API filters dead handles,
including handles whose slot has since been reused by an unrelated entity.
Paused Scene viewport clicks use the same runtime selection as Outline. Picking
follows renderer order and exact rotated/warped shape containment. Spawned entities
receive the shared selection outline, but pointer movement cannot edit them.
Clicking empty space clears either selection kind. Authored instance dragging
retains its existing transaction and Undo behavior.

Encounter validation: `make test` passes. Both-host tests cover initial cadence,
alternating placement relative to a moved player, actual pursuit, the eight-enemy
cap, no-player timer freeze, pause/step and relaunch. Native tests load the shipped
scene, start its wave, destroy Player, Restart, verify restored health/position and
no old enemies, replay the cadence, then Stop and verify cleanup. The encounter
capture includes the composed UI and was inspected.

Scene Play temporarily expands the Scene into the graph's central space, keeping
Navigator and Inspector available. Pause and Step retain the presentation; Stop
or switching to prototype preview releases it. Stored pane preferences remain
unchanged. Opening a behavior or System explicitly reveals Graph, and manual pane
choices survive Stop. The existing dock split restores without rebuilding the
workspace or resetting its camera. Custom layouts with nonsibling Graph and Scene
docks retain their arrangement. Native checks cover expansion, restored height,
manual graph visibility, settings persistence and preview/document transitions.
The expanded encounter capture was inspected at 1920 by 1080.

Runtime feedback in the viewport names the active target: Scene running/paused or
the named prototype preview running/paused. Successful graph updates no longer
leave a "Preview live" label that can be mistaken for simulation state. Graph
errors, pending updates and required restarts remain separate actionable notices.
Stop is directly beside Play/Pause for active and paused sessions; More contains
Step, Restart and Reset View. In narrow Scene panes, Fit Scene moves into More
so transport controls remain accessible. The target name truncates to available
space and the interaction hint wraps. Native transport tests use the direct Stop
control and exercise Fit Scene through the compact toolbar menu.

Scene instance history now includes editable scalar fields and colors alongside
Transform gestures. Numeric editing creates one entry on deactivation; all edits
within a color popup form one entry when it closes. Selection/document/runtime
transitions and hiding the Inspector finish pending edits. Revert All records one
batch across editable fields, including Transform. No-op edits preserve Redo.
History owns its copied metadata and validates every target before replaying a
batch; missing entities, components or incompatible fields leave values and both
stacks unchanged. Strings and nested structs remain read-only and are excluded
from Revert All until ECS assignment supports owned values. Behavior attachment
history remains unfinished.

The Scene Outline now offers Add with a prototype picker and Delete in an
authored actor's context menu. Both actions enter the same history as property
edits. Lifecycle entries retain owned snapshots of components, actual behavior
attachments, prototype baseline, name and list position. Undo restoration creates
a fresh entity and rebinds only the exact old handle in its history entries, so
earlier field edits still replay. Unrelated stale generations remain invalid.
Restored actors own their backing snapshot independently of history, so clearing
Redo cannot invalidate component values. Creation chooses an unused name suffix;
load and teardown use separate unrecorded operations.

Inspector component groups use full-width 32px disclosure rows with a drawn
chevron, restrained hover/focus feedback and keyboard Left/Right support. Chevron
motion starts from its current angle and settles in roughly 120ms; reduced motion
snaps immediately. Object titles are larger than metadata, runtime metadata is
condensed, and Save/Stop details are available on hover. Divider contrast is lower
without shrinking the resize hit area. Native editing and history tests pass with
the new controls, and the composed Inspector capture was visually inspected.

Authored scene instances now expose a Behaviors section in the Inspector. Select
a behavior name to open its graph, use its trailing menu to remove it, or use Add Behavior to
choose from unattached assets. These changes affect the selected instance and
participate in Scene Undo/Redo alongside property and actor lifecycle edits.
Each attachment occupies one row with a readable name and a trailing actions
menu. History owns ordered before/after names and follows actor handle replacement.
Attachment editing requires a stopped Scene. Runtime-spawned entities remain
read-only.

Ordered attachment replacement preserves unchanged behavior instances and their
state. Scene authoring defers Begin, so adding actors, changing attachments and
Undo/Redo do not execute gameplay. First Play or Step captures the authored state
before starting pending behaviors. Pause/resume preserves initialization; Restart
prepares every actor before starting Begin, and Stop restores authored state
without running it. Loading a scene returns to deferred setup; prototype behavior
changes end an active session before editing. Runtime spawns and isolated
prototype previews still initialize immediately. Failed initialization remains an
error until the affected attachment is replaced or the Scene is stopped/restarted.
Runtime tests exercise order, state retention, deferred start and failure; native
tests exercise attachment history and Scene initialization across transport actions.

Scene navigation includes a search field shared by authored and runtime-spawned
instances. Matching ignores letter case and searches authored names/prototypes
and spawned labels. Filtering preserves selection and Inspector context; Clear
restores the full list. Empty results have an explicit message, and the spawned
section appears only when it has matching rows. Native checks cover mixed-case
queries, hidden selection preservation, empty results and clearing the filter.

Material properties use progressive disclosure across prototype, instance and
runtime Inspectors. Base Color, Border Color, Emission Color and Emission Intensity
remain visible; Advanced Lighting contains albedo, filtering, absorption, emission
multiplier and border-lighting controls. The advanced group starts collapsed and
uses the same animated, keyboard-accessible disclosure as components. Property
values, serialization and Undo semantics are unchanged.

Scene Play uses the app's input mapping for gameplay: WASD/arrows move and Space
fires. Native checks cover the Play/Stop transition. A settled startup
capture also confirms the shipped InputBehavior graph fits the opening canvas.

Behavior attachment names remain navigable during Scene Play and Pause. Opening
a graph preserves the running session and the selected instance for inspection;
only attachment mutation controls are disabled. Native interaction checks cover
a disabled actions menu, opening InputBehavior during Play and stopping afterward.

Viewport projection follows the requested display aspect immediately, while GPU
canvas allocation waits for the existing resize settling interval. Geometry and
pointer mapping therefore retain their proportions during pane transitions. The
scene renders once after viewport navigation, avoiding a second temporal-lighting
update on pan/zoom frames. Empty display regions cancel pointer interaction. Tests
cover camera aspect before allocation changes and the first narrow-pane frame
with an image border. Lighting resolution still settles with the canvas.

Graph execution feedback scales its wire and marker weight with zoom, with a
one-pixel line floor and bounded scaling. Overview markers no longer grow relative
to nodes. Activity brightens the source type color; the time-based heat envelope,
reduced-motion marker behavior and independent link hit area remain intact.

Scene Save/Reload and System Reload now copy incoming paths before replacing
their owned paths. Save All includes the active System working graph. Scene loads
validate prototype components, saved overrides and behavior owner compatibility
in an isolated world before replacing actors or clearing runtime/history state.
Invalid data preserves the open scene, selection, exact entity handles, Undo and
the original save path. Native regressions use temporary content and verify the
serialized System edit plus repeated ordinary Save/Reload commands.

Renderer shader-directory setup runs once after creating the Cute app, before
creating scene renderers. Replacing an authoring session therefore does not repeat
Cute's one-time setup. The full `make test` run passed after these changes,
including runtime/GPU tests, native editor interactions and the resize-pane check.


## Interaction and gameplay corrections, September 6, 2026

User testing exposed reversed directional input and duplicate speed/time scaling.
The app now sends one normalized movement vector per frame. Opposing keys cancel,
diagonals retain cardinal speed, and the graph applies speed and delta time once.
MovementTrailBehavior expresses movement-only rear emission with fractional rate
carry and Neon-Swarm's particle settings. It is attached in both the Player
prototype and the explicit saved startup Scene. Material edges and composition
now use viewport pixel resolution; only the radiance work grid is reduced.

Graph and Scene remain visible during Play. The Navigator and Inspector have
local collapse controls, persistent reopen rails and interruptible width changes.
A reduced-motion preference snaps their geometry. The menu bar no longer carries
pane-toggle buttons. Selecting a behavior preserves the entity Inspector; selecting
a node switches its contents to node details. An empty Inspector shows guidance.
Scene camera navigation during Play requires Option with middle-drag or scrolling.

Scene and Graph have floating Font Awesome transport buttons for the same active
Scene or isolated preview. Play/Pause, Stop and Step use matching states and
labels. Graph-specific actions remain separate; the Scene's optional
view controls move into overflow only when the viewport is narrow. Icon buttons
share hover transitions and immediate pressed feedback. Node rows reserve actual
widget height, with larger data rings and distinct execution ports.

Outlined chevrons can be selected through their enclosed silhouette. Dragging
empty scene space selects an area. Delete removes the selected authored actors,
and one Undo restores a batch deletion. Files supports dragging a prototype into
the scene at the drop position; creation history preserves that position. Files
uses a compact create control and refreshes its tree automatically. Contextual
Open and Add to Scene actions remain available on prototype files.

Instance Inspectors can add and remove components, with owned component history
and serialized overrides for additions and removals. Revert All operates on the
remaining inherited fields. Changing a prototype preserves instance-added
components. Runtime component and attachment mutation stays disabled during Play.

Validation: `make build` and the final full `make test` passed. Native checks
exercise both transport toolbars, sidebar interruption and reduced motion,
outlined interior picking, marquee selection, grouped Delete Undo, component
Undo/Redo, Files drag placement, and node input containment/spacing. Runtime tests
cover cardinal/diagonal movement and movement trails in both hosts; GPU checks
cover the full-resolution material output with the reduced radiance grid.
The composed startup view was captured and visually inspected.


## Contextual Scene and Graph transport

Scene and Node Canvas show floating Play/Pause, Stop, Step, and Restart controls
for the same selected runtime. The menu bar has no transport buttons or runtime
target selector.

Each toolbar appears while its pane is hovered or focused, including child
controls and editing focus. Pointer entry and exit animate over 200 ms without
reserving layout space. Controls reject input immediately when both hover and
focus leave. Keyboard focus reveals controls immediately.

Narrow Scene panes use Play/Pause and More; short or narrow graph canvases do
the same so controls remain reachable. Scene keeps Fit and Reset Camera; Graph
keeps its local actions and Fit. Debug target selection and Pin are inside the
floating Debug popup. There is no separate graph header or breadcrumb row.
Navigator retains Back/Forward and its Files, Scene, and Document tabs.

Navigator and Inspector are movable, dockable windows. Their separate collapsed
rails preserve saved docking and floating geometry when reopened.

## Behavior Expressions

Add-node search includes Behavior Expression. Creating one selects it and opens its
source in Inspector. Enter regenerates an ordinary function graph;
invalid expressions leave the current source, graph, and history unchanged.
Named scalar Real inputs are inferred from identifiers. Unchanged input names
keep their identities, defaults, and compatible connections across edits.

Arithmetic, powers, parentheses, common scalar functions, and `rand()` are
supported. Angles are radians. The source field is intentionally self-contained.
Inspect generated graph opens a read-only graph with normal operator/function
nodes. Editing the source is the supported way to change that graph. Undo and
Redo restore the expression and its generated graph together.

When the canvas is in Fit mode, applying a longer expression refits its changed
bounds. Manual camera framing is preserved.

See [Behavior Expression reference](math-expressions.md) for syntax, supported functions,
System graph use, generated-definition deletion, and the converted sample graphs.

## Real-to-integer conversion

Explicit conversion edges now support scalar Real values feeding Byte, Integer,
and Integer64 pins. Conversion truncates fractional values toward zero, saturates
out-of-range values and infinities to the destination bounds, and maps NaN to
zero. The bounds are checked before casting, including the Integer64 upper bound,
so narrowing does not invoke undefined C behavior. The registered conversion
nodes are To Byte, To Integer, and To Integer64; integer widening remains
available through To Integer64. This supersedes the earlier TYPE-7 policy that
excluded lossy Real-to-Integer conversion.


## Workspace navigation and shader node authoring

The left activity rail contains Project, Scene Hierarchy, and Document Outline,
with Quick Open below them and Performance at the bottom. Clicking the active
navigation destination collapses the sidebar; selecting another opens it without
changing the active document. The Project sidebar uses its full height for Files
or Asset catalog. Graph / Scene / Split mode controls have been removed. Docking
and View remain the layout controls, and Reset Layout places Performance in a
bottom dock, initially hidden.

Asset and node search share local ranking: exact names, word prefixes, CamelCase
initials, reordered terms, and small spelling errors. Names rank ahead of paths
and keywords. Node search retains compatible-pin filtering and authored aliases.
Quick Open keeps keyboard navigation in the result list and selects the best
result after query changes.

Compatible component getters dropped on or selected from an edge fold into the
destination binding.

Behavior event graphs also expose their typed event outputs in the same input
binding picker. Choosing an event output stores the event and pin identity on
the target input, so the value can be used anywhere in that event's graph
without another data edge. `Delta Time` remains a compact context node; when
the active event declares a real `DeltaSeconds` or `Delta Seconds` output, its
payload supplies that context automatically.

Material, Shape, and Shader Library nodes stack outputs above options and inputs.
Inputs use the node width for labeled numeric values, vector axes, range endpoints,
colors, context defaults, and binding controls. Normalized shader properties use
sliders. Parameter names and types, constants, gradient modes, ramp interpolation
and stops, and image selection/filtering/wrapping are editable directly on nodes.
Inspector remains available for previews, diagnostics, and document properties;
shader node editing does not require it. Execution graphs use the same data-pin
layout with execution pins paired above it. Native tests cover editing with
Inspector hidden, Undo/Redo, image drop/picker replacement, serialization, and
live shader output changes.

## Blender references and Color Ramp

For node controls with a direct Blender equivalent, use Blender's layout and
interaction conventions as the default reference. Check the official manual and
its images before inventing a different arrangement. Apply the reference to the
capabilities the editor actually supports; shader authoring stays on the node.

Color Ramp follows Blender's
[Color Ramp widget](https://docs.blender.org/manual/en/latest/interface/controls/templates/color_ramp.html):
compact add/remove and tools controls above the strip, color-filled handles below
it, a dashed marker for the active stop, a zero-based stop selector beside
Position, and a full-width color swatch. Ctrl-click inserts a sampled stop;
Add inserts beside the selected stop. The tools menu supports Flip, both stop
distribution modes, and Reset, with Undo/Redo. RGB is the current color mode;
interpolation offers Linear, Constant, and Ease. Shader sliders use a filled
track with the label on the left and value on the right, retaining keyboard and
numeric entry.

## Graph opening and dock context

New graph views open at 85–100% zoom. Large graphs start near an event/function
entry or shader output, with room for the floating toolbar. Explicit Fit still
frames every node and wire. Revealing a node brings an overview back to at least
85% zoom. Re-entering a local function restores its camera instead of arranging
and fitting it again.

A sole root graph pane hides its duplicate dock tab when the global document bar
already identifies it. The dock's corner affordance can reveal the tab for
undocking. Shared docks retain their tabs, detached panes retain their title,
and nested graphs show both asset and function names. Standalone graph editors
without the global document bar keep their title.

## Shared node controls

Behavior, function, and shader graphs share the same node measurements, data-pin
ordering, and value controls. Execution inputs and outputs occupy paired rows at
the top. Data outputs follow, then inline options and full-width data inputs.
Math operators use the common titled node shape with their symbol in the header.
Connecting values preserves node width. Behavior and shader arithmetic retain
their separate runtime and shader lowering paths. The native arithmetic check
covers glyph/control separation at 65%, 100%, and 135% zoom, vector and bound
inputs, wired inputs, selection, and opening the source picker.

Numeric fields put labels on the left and values on the right. Dragging and direct
numeric entry use native ImGui input handling. Vectors use labeled axes; ranges
use endpoint labels; strings, dropdowns, colors, and bound sources get a full-width
row beneath their pin label. Node controls retain separate Undo steps. Particle
colors use explicit pin metadata and existing assets carry that metadata.

Nodes with optional advanced inputs show an Advanced inputs disclosure on the
node itself. Bound and connected inputs remain visible when it is collapsed.
Document properties and function/event signature editing remain in Inspector;
ordinary input and shader-option authoring stays on the canvas.

## Copy and vector input polish

Keep editor copy to names, actions, states, and actionable errors. Asset
descriptions remain metadata and are no longer displayed as paragraphs in
Inspector or event/library overviews. Source tooltips show the source name;
the pause menu omits its control tutorial. Performance notes are compact and
available on hover.

Vector components occupy separate full-width rows, with X/Y/Z/W on the left and
values on the right. Fields use the shared neutral background, rounded corners,
and padding, with no colored axis borders. Range endpoints retain Min/Max labels.

Each component can use a literal or an inline source. The vector's source action
applies to the whole value. Component sources are owned by an inline vector
constructor, using ordinary graph nodes and connections; save/load, expansion,
replacement, deletion, and Undo follow the existing graph ownership rules.
Assigning a scalar to a whole vector evaluates that scalar once and broadcasts it
to every component. Shader lowering uses the same broadcast behavior. Inline
scalar constants remain editable on the node.

Pure sources are drawn inside their consumer's input by default. A node folds
when it is pure and enabled; is not a flow node such as a pure function's
Function Entry, a function call, reroute, or local source; has no setting pins
besides a component node's chosen Component; has exactly one outgoing wire, a
data wire into a non-setting input; and each input is wired from a folded node
or holds a value (a literal, binding, local, or implied Self).
`graph_node_is_inline` decides this for the canvas, arrangement, extraction,
copying, and saving. Trees of up to three nodes fold into one chip written in
Behavior Expression grammar with the typographic operators node titles use,
such as `elapsed ≥ Self.GameWeapon.cooldown`. Literal nodes read as their
value, named sources such as shader parameters as their name, and other nodes
as calls, such as `multiply(time, Glow)`. Nodes with breakpoints or watches, and
generated expression graphs, keep their boxes.
Folded nodes remain ordinary nodes and wires, so evaluation, compilation, and
debugging are unchanged. They have no box, wires, or saved position, and
arrangement skips them.

Each node stores a presentation: `auto` (the default, omitted from files),
`inline` (fold whenever legal, including larger trees), or `expanded` (always
draw the box). **Expand source** sets `expanded` and places the node left of its
consumer, below any box it would overlap. The node context menu's
**Inline** folds an eligible node and stores `inline` only when `auto` would keep
its box. Deleting a node deletes the sources drawn inside it. A wire dropped on
an input replaces its folded source, as it would a literal.

Node rows use four pixels of extra spacing. Only literal outputs are editable;
runtime-written outputs do not reserve empty control rows. Entity, aggregate,
and unresolved inputs show their value or default in the pin row, with the
source button beside it. Clicking the value also opens the source picker. A
default entity target reads Self.

The focused native `--ui-node-controls` check covers the Set Visual Motion
Rotation layout, runtime output ownership, Target alignment and source picking,
and scalar/integer/vector authoring with independent Undo/Redo. It also checks
stacked component bounds, component and whole-vector sources, scalar broadcast,
source replacement, and shader source editing with Inspector hidden.

## Auto-arrange and wire lanes

Arrange places nodes in layered columns and leaves a lane beside the nodes of
every column a wire skips. The canvas draws those wires through their lanes
instead of as one curve across the columns, so they no longer cut through
nodes. `arrange_route` recomputes the lanes from node positions alone: it
re-runs the layered layout with every node pinned in place, so it finds the
lanes Arrange reserved and still gives hand-placed nodes reasonable lanes.
Routes are never saved. The canvas rebuilds them when positions, sizes, wires,
or the wire style change. Authored reroutes and custom handles take
precedence.

Studio's Graph Wires preference picks one of four styles, shared by Arrange,
routing, drawing, and scoring through `arrange_route_style`; the ImGui canvas
draws curved wires:

- Curved: cubics through the lanes. Backward wires loop around their nodes on
  round corners.
- Orthogonal: level and vertical runs with rounded corners. Every vertical run
  gets a track in the gap between two columns (channel routing): tracks are
  ordered for the fewest crossings, runs that overlap vertically never share
  one, and a staircase of wires that no order can separate doglegs. Arrange
  widens a gap to fit its tracks.
- Straight: line segments between the waypoints, leaving and entering pins
  level.
- Subway: the orthogonal tracks with 45 degree diagonals for corners.

`arrange_path` turns a wire's waypoints into cubic pieces in canvas units, so
every renderer draws, hit-tests, and bounds wires the same way and the score
measures what is drawn. Studio's `kit_wires` element reaches it through
`kit_wire_shape` once each wire's ends resolve after layout; changing the style
arranges automatic nodes again, since gaps depend on it. Wires from one output share a trunk: they share lanes
through the columns they all skip, turn on one track in each gap, and curved
and straight fan-outs leave the pin along one short level run. Crossings count
where drawn wires cross, so a trunk crossed once counts once, and the score's
ink length counts shared runs once.

Turns are scored too: every change of height costs two, and the score keeps
the turns on execution wires apart. Editors mark execution wires, and a node
lining up with its neighbours counts each as three data wires, which
straightens execution chains without bending data curves into more crossings;
heavier weights did. When two inputs still pull a node equally it follows the
one on its higher pin, and a straightening pass then moves each run of lanes to one height
wherever its column has room, so a long wire changes height once instead of at
every column. A pin drawn a few units off the height its route planned slides
the vertical run beside it rather than jogging, and a subway drop barely longer
than two diagonals becomes one.

A backward (cycle) wire leaves its output rightward, passes its own node on
the side it heads for, runs left through the lanes its columns reserve,
passes its target the same way, and enters from the left, so it never
crosses the nodes between its ends. `graph_layout`, which MCP edits use,
measures pin rows in the order Studio draws them: execution pins, then data
outputs, then data inputs, counting only drawn pins.

Arrange stacks unconnected parts of a graph as horizontal bands: Begin, Tick,
other host events such as input and contact, custom events, End, then
everything else (`graph_layout_band`). Event chains that share nodes form one
band.

`test_suite --arrange-bench [repeats] [filter] [style]`, run from the
repository root, arranges and routes every graph under `content/` and
`projects/*/content/` in every style, or in the one named. It prints layout and
routing time, crossings, wire-node hits, node and wire overlaps, waypoint
count, length, ink, turns, cost, and a hash of the placed positions per graph and
style, then totals per style. `--graph-arrange` runs the focused scoring,
routing, and determinism checks, and scores the arena2d behaviors against a
ceiling per style.

Across the 201 content graphs, arranged from zeroed seeds as `graph_layout`
does, Arrange once produced 4672 crossings and 774 wire-node hits. Counted as
drawn, lanes without trunks produce 3340 crossings and none; shared trunks
bring curved wires to about 2190 crossings and cut ink by about a third,
orthogonal wires cross about 1900 times and subway wires about 1730, and
straightening cut turns from 8944 to 7826. A Release build lays out and routes all of them in about
6 ms curved and 8 ms orthogonal; the largest graph (BasaltCave, 425 nodes)
takes about 3 ms.
