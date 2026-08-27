# Unreal-style Blueprint editor plan

Status: Phase 1 implementation in progress
Date: 2026-08-26

## Goal

Make the editor read like an Unreal Blueprint editor without copying features
that do not fit this project. The Prototype is the Blueprint-like asset. Its
component defaults appear in Components, attached behavior modules appear in My
Blueprint, and graph editing remains a focused scripting surface.

The visual reference is `~/Downloads/unreal-event-graph.png`. The current
baseline capture is `/tmp/c-blueprints-screenshot.png`, 1920x1080. An
event-focused capture is `/tmp/c-blueprints-event-screenshot.png`, also
1920x1080.

## Baseline comparison

The current graph already has the useful visual pieces: a dark grid, colored
nodes, typed pins, and a central canvas. The shell differs in these ways:

- Content Drawer occupies the full bottom region by default.
- Scene hierarchy, Blueprint, Prototype Details, Details, Runtime inspection,
  and the graph are submitted as separate persistent windows.
- The Blueprint panel mixes graphs, variables, event rows, and every node in the
  active graph.
- Details edits blueprint metadata, node metadata, event identity, event
  parameters, pin schemas, pin defaults, and topology controls in one long
  panel.
- The top bar lacks an asset tab, document context, mode grouping, and a
  menu-bar command surface.
- Compiler Results and Output Log are not currently submitted as usable bottom
  panels.

The target shell has one menu bar, one active asset tab, a Components surface,
a My Blueprint surface, a central mode and graph area, one universal Inspector,
and a quiet bottom tab host.

## Decisions

### Asset and ownership model

- A Prototype is the Blueprint shell.
- Prototype Components show a flat root row and the prototype's component
  templates/defaults. Child-entity hierarchy is deferred.
- A Prototype owns its component set and attached behavior asset names. It does
  not own runtime entities.
- Behavior assets remain separate reusable modules. Each module owns its graphs,
  variables, functions, and macros.
- Behavior variables remain private to each behavior instance. Behaviors share
  state through Prototype-owned ECS components. Direct custom-event messaging is
  deferred.
- My Blueprint nests behavior modules and their graph members. It does not
  flatten their variables into a Prototype-wide namespace.
- Direct Content Drawer opens may still edit a standalone Behavior document.
  Opening a behavior through My Blueprint keeps the Prototype shell active.
- Only one Prototype or standalone Behavior document is active at first. Nested
  Preview and graph tabs belong to that document. A multi-document registry is
  deferred.

### Panels and context

- Components is a compact selection and structure pane. The universal Inspector
  edits the selected component's fields.
- My Blueprint lists behavior modules, graphs, variables, functions, and macros.
  Remove its duplicate Events and Nodes lists. Remove Find until user testing
  shows a real need.
- One physical Details window becomes a universal, dockable Inspector.
- Prototype, component, behavior, graph-member, and scene-instance selections
  change the Inspector body and header. The header must identify ownership.
- Selecting a Scene Runtime entity changes only the Inspector to Scene instance
  context. It does not close or replace the active Prototype workspace.
- Selecting a My Blueprint graph/member focuses that graph. Selecting a component
  focuses Components/Preview when Preview exists.
- Prototype Details and Runtime inspection are not separate persistent windows.
- The bottom host contains Compiler Results, Output Log, and Content Drawer as
  ordinary dockable tabs. It stays quiet when diagnostics change; no popup is
  forced open.
- Content Drawer is a normal dockable tab, closed by default in the Blueprint
  workspace. Hover expansion is deferred.
- The shell exposes File, Edit, Asset, View, Debug, Window, Tools, and Help menu
  bars. Commands use Unreal's grouping, but unsupported commands are omitted.
- The default layout gives the graph most of the window: left Components/My
  Blueprint surfaces, central document and graph tabs, right Inspector, and a
  short or collapsed bottom host.

### Events and graph authoring

- Current Event nodes use host-catalog entries with fixed signatures. For the
  current app the catalog contains Begin, Tick, and Input.
- The graph spawn menu creates catalog events directly. New nodes have their
  name and pins immediately.
- Remove free-form event names and event parameter add/remove/rename/retype/
  reorder controls.
- Remove Add Call Event and the Call Event node end to end until custom events
  return as a separate feature.
- Remove the generic per-pin Name/Type/Container editor. Fixed descriptor pins
  are read-only in Inspector. Instance values remain editable inline on nodes.
- Dynamic pin operations remain owned by the node types that need them, such as
  Sequence or wildcard argument nodes.
- Keep Disabled as a graph context-menu action because it changes execution.
- Remove node title overrides and node-level comments from the model,
  serialization, editor, and tests. Reference IDs and target names remain
  internal and are assigned by node-specific creation commands.
- Keep graph comment boxes as the only annotation mechanism. Add editor support
  for creating, selecting, moving, resizing, coloring, and renaming them.

### Prototype and scene state

- Prototype field edits propagate immediately to every matching Scene Runtime
  entity and Prototype Preview entity that still inherits the field.
- A Scene instance Inspector can edit fields as local overrides, show inherited
  versus local state, and revert one field or all fields.
- Scene instances cannot add or remove components. Prototype Components owns the
  component set.
- Prototype component additions and removals propagate to all matching instances
  immediately. Removing a component also removes that component's local values
  from instances. No warning popup is used.
- Prototype behavior attachment additions and removals propagate immediately.
  Adding starts the behavior's Begin event. Removing cancels its active
  behavior state. The operation is one Prototype undo transaction.
- Scene instances inherit the Prototype behavior list. Remove scene-level
  behavior additions and removals from the format, UI, APIs, and tests.
- Prototype and Scene authoring use separate data from runtime state. Runtime
  entities are rebuilt from current in-memory authoring state when restarted.

### Runtime targets

- Prototype Preview and Scene Runtime are separate runtime worlds with separate
  entities, behavior instances, activations, logs, and restart state.
- They share authored data and the active viewport surface, not entity handles
  or activation state.
- Prototype Preview is planned after the shell and component editing surfaces.
  The current scene viewport must not be mislabeled as Prototype Preview.
- Scene Runtime gets an explicit Play, Pause, Step, Restart, and Stop lifecycle.
  It starts stopped. Restart uses current in-memory authoring state. Stop
  discards runtime state without undoing authored data.
- The toolbar names the active target, Prototype Preview or Scene Runtime.
  Controls affect only that target.
- Prototype component edits live-patch both targets while Scene Runtime is
  playing. Behavior graph edits take effect in both targets only after Save
  successfully compiles and applies the behavior asset.
- Saving a Behavior applies the shared asset to every attached instance in both
  runtime targets. A failed compile or apply still saves the authored graph and
  leaves the previous valid runtime snapshot active.

### Persistence and history

- Add document-aware File menu commands: Save, Save As, Save All, and Reload from
  disk.
- Save writes the active Prototype, Behavior, or Scene source file. Save All
  writes every dirty source. New/pathless assets require Save As.
- Behavior Save performs source write plus compile/apply. Prototype component
  edits are already live; Scene Save persists bindings and overrides.
- Save and Save All are explicit. Do not add auto-save in this pass.
- Save failures and unapplied runtime state appear in status and diagnostics,
  not interrupting popups.
- Undo and redo are per authored document. One user action creates one history
  entry. A Prototype edit and its propagation to Preview and Scene Runtime undo
  together. Runtime state is recomputed from authored state.
- Undo/redo, Save, and Save All must update dirty state and active runtime status.
- Use atomic file replacement for source writes.

## Implementation order

### 1. Record the new contract

- Add ADR 0021 from this plan.
- Reconcile `docs/spec.md`, `docs/roadmap.md`,
  `docs/editor-document-system.md`, and the manual checks.
- Withdraw requirements for custom event editing, Call Event, node title
  overrides, node comments, generic pin schema editing, and scene behavior
  additions/removals.
- Add requirements for Prototype shell ownership, live component propagation,
  per-document history, explicit runtime targets, and document-aware Save.
- Keep ADR 0020 for Behavior working-copy/applied-snapshot semantics. Prototype
  component data is a separate live ECS propagation path.

### 2. Rebuild the shell and command surface

Primary files: `editor/ui_shell.c`, `editor/ui_shell.h`, `app/main.c`.

- Define stable panel IDs and a small explicit document/context state.
- Build the menu bar and active asset tab.
- Replace the default dock graph with Components, My Blueprint, central document
  tabs, Inspector, and the bottom tab host.
- Make panel submission depend on context. Do not submit hidden context panels
  merely to keep their old dock tabs alive.
- Keep all panels independently dockable and persist layout through `imgui.ini`.
- Add the File, Asset, View, Debug, and Window command paths without adding
  inert Unreal controls.

### 3. Narrow the Behavior editor

Primary files: `editor/editor.c`, `editor/editor.h`, `core/flow_nodes.c`,
`core/blueprint.c`, `core/graph.c`, `core/node.h`, `core/graph.h`,
`core/serialize.c`, and generated registry inputs.

- Add catalog-event creation and fixed-signature pin construction.
- Remove Call Event and custom event authoring paths.
- Remove the generic Details pin editor and route dynamic controls to node-owned
  menus.
- Delete title/comment fields and their serialization and mutation APIs.
- Keep reference IDs as internal target identity, not editable properties.
- Remove the My Blueprint Events and Nodes sections and the Find command.
- Implement graph comment-box rendering and editing.
- Preserve inline literal/default editing and graph context actions that still
  have clear ownership.

### 4. Add the Prototype Blueprint shell

Primary files: `game/prototype.c`, `game/prototype.h`,
`app/scene_authoring.c`, `app/scene_authoring.h`, and new explicit editor/context
modules only if a real ownership boundary requires them.

- Render a flat Components pane from reflected component descriptors.
- Add component add/remove commands and typed field editing in Inspector.
- Move behavior attachment add/remove into My Blueprint.
- Nest attached behavior members and route graph selection into the existing
  graph editor.
- Replace Prototype Details with the universal Inspector.
- Keep standalone Behavior document opens working.
- Add source-path and dirty tracking for Prototype, Behavior, and Scene.

### 5. Add instance overrides and per-document history

Primary files: `app/scene_authoring.c`, `game/scene_document.c`,
`game/scene_document.h`, and the editor history owner.

- Make Scene instance Inspector fields editable as local overrides.
- Show inherited/local state and Revert controls.
- Remove local behavior additions/removals and local structural component edits.
- Give Prototype, Behavior, and Scene independent undo/redo stacks.
- Snapshot authored documents, not runtime worlds. Group propagation with its
  originating edit.
- Ensure component removal and undo restore the Prototype and all affected
  instances without confirmation dialogs.

### 6. Implement live propagation and runtime targets

Primary files: `app/scene_authoring.c`, `engine/engine.c`, `engine/engine.h`,
`engine/viewport.c`, `game/live.c`, `game/behavior.c`, and new runtime-target
state where needed.

- Split authoring data from Scene Runtime entities.
- Add inherited-field comparison against each actor's previous Prototype base.
- Apply prototype field and component-set changes to all matching runtime targets.
- Propagate behavior attachment changes and run or cancel behavior state as
  required.
- Add Scene Runtime Play/Pause/Step/Restart/Stop.
- Make Restart rebuild from current in-memory scene and prototype data.
- Keep input routing tied to the selected runtime target.
- Replace the current per-edit destruction of the editor preview instance with
  the working-copy/applied-snapshot path for behavior graphs.
- Add the separate Prototype Preview world after the Scene Runtime target is
  stable. Use the same viewport surface with target-specific rendering.

### 7. Implement Save and Apply

Primary files: `editor/editor.c`, `app/scene_authoring.c`, `game/live.c`, and
new small file-writing helpers if needed.

- Track active source paths and dirty state.
- Add Save, Save As, Save All, and Reload from disk to the menu bar.
- Save authored Behavior JSON before attempting compile/apply. Keep the previous
  runtime snapshot when apply is blocked.
- Apply a saved Behavior asset to every attached instance in Preview and Scene
  Runtime targets.
- Save Prototype and Scene documents independently.
- Write files atomically and report failures in the bottom diagnostics/status
  area.

### 8. Match the visual reference and validate

Primary files: `editor/ui_shell.c`, `editor/editor.c`, `app/scene_authoring.c`,
and `engine/viewport.c`.

- Match the reference's menu-bar height, asset tab, toolbar grouping, graph
  tabs, breadcrumb, left pane proportions, Inspector width, node title bars,
  grid contrast, selection outline, and bottom diagnostics strip.
- Use existing theme tokens and row/section primitives from `editor/ui_shell.c`.
- Keep text sizes and spacing within `docs/editor-ui-style.md`.
- Use icons only where their action is clear and provide tooltips for icon-only
  controls.

## Validation

Model and runtime checks:

```sh
cmake --build build
./build/self_check
./build/integration
```

Add focused checks for:

- catalog event creation and fixed signatures;
- removal of Call Event and custom event paths;
- per-document undo/redo;
- Prototype component add/edit/remove propagation;
- preservation and revert of Scene field overrides;
- Prototype-owned behavior attachment propagation;
- separate Preview and Scene Runtime restart state;
- Save plus apply success and Save with blocked apply;
- atomic source writes and dirty-state transitions.

UI checks:

```sh
build/app --headless --screenshot
```

Inspect `/tmp/c-blueprints-screenshot.png` and report its dimensions. Exercise
Prototype selection, Components selection, My Blueprint graph selection, Scene
instance Inspector context, Save, Save All, Compile/apply failure, Undo/Redo,
Scene Runtime Restart, and bottom-host tab docking. Reset `imgui.ini` for a
clean default-layout check, then verify that a user-customized layout survives
a restart.

## Implemented first slice

The current working tree includes the first shell, model, and runtime slice:

- context-routed World Outliner, Components, My Blueprint, and universal
  Inspector panels;
- a resettable Unreal-style dock layout with a normal dockable Content Drawer;
- fixed host-catalog Event creation in the editor and removal of Call Event,
  node title overrides, node comments, and generic pin schema controls;
- Prototype component field editing, add/remove structure, live propagation to
  matching Scene Runtime instances, and Prototype undo/redo buttons;
- Scene instance inherited/local field editing with per-field and all-field
  Revert controls;
- explicit paused Scene Runtime with Play, Pause, Step, Restart, and Stop;
- document-aware Save and Save All hooks with atomic writes;
- editor Preview instances backed by a cloned applied snapshot, so working graph
  edits do not tear down the current preview.

The first slice is intentionally incomplete. Prototype Preview remains a
separate follow-up runtime world, Reload and Save As are not wired, and scene
history is not yet a full per-document stack.

## Deferred work

Do not block this pass on:

- child-entity/component attachment hierarchies;
- a multi-document registry;
- custom events, delegates, or Call Event;
- Find in Blueprint;
- hover-expanding Content Drawer;
- Diff, Hide Unrelated, Class Settings, or Eject commands;
- Prototype Preview polish beyond a minimal separate runtime target;
- instance-specific structural component overrides;
- Prototype-wide shared Behavior variables.
