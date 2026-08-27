# ADR 0021: Prototype shell and live runtime editor

Status: Accepted
Date: 2026-08-26

## Context

The current application has the right domain pieces but presents them as
separate demo panels. The graph editor owns one behavior blueprint, the host
owns prototypes and scene entities, and the shell submits all panels regardless
of the active document. The result does not match the ownership model that the
editor is intended to expose.

The target reference is Unreal's Blueprint Editor. Its Components panel shows
class-owned component templates, its My Blueprint panel shows graph members,
and its Details panel follows the current selection. A placed Actor is a
separate Level Editor selection and does not replace the open Blueprint asset.

Our model has a related but different boundary:

- a Prototype owns component defaults and attached Behavior asset names;
- a Behavior asset owns graphs, variables, functions, and macros;
- a Scene entity is an instance of a Prototype with local field overrides;
- a runtime target owns entities, behavior instances, activations, and logs.

The editor must expose those boundaries without merging reusable Behavior
assets into Prototype data or presenting runtime entities as Prototype-owned
components.

## Decision

### Prototype is the Blueprint shell

A Prototype document is the Blueprint-like asset editor. Its Components pane
shows a flat Prototype root and its component templates/defaults. Child entities
and component attachment hierarchies are deferred until the data model supports
them.

Its My Blueprint pane shows attached Behavior modules. Each module contains its
own graphs, variables, functions, and macros. Behavior variables remain private
to each attached behavior instance. Behaviors share authored/runtime state
through Prototype-owned ECS components. A Prototype-wide variable store is not
introduced.

Behavior assets remain reusable and separately owned. A direct Content Drawer
open can edit a standalone Behavior document. Opening a Behavior from My
Blueprint keeps the Prototype shell active and focuses that module's graph.
Only one Prototype or standalone Behavior document is active at first. A
multi-document registry is deferred.

### Contextual panels

The shell has one physical, dockable Inspector window. Its header names the
current owner and selection, such as `Prototype: InputEntity` or `Scene
instance: InputEntity_1`.

Components is a compact selection and structure pane. The Inspector owns
component field editing. My Blueprint lists Behavior modules and their declared
members. It does not list every graph node or duplicate Event rows. Find in
Blueprint is removed until user testing demonstrates a need.

Prototype Details and Runtime inspection are removed as separate persistent
windows. Runtime inspection is split between a concise Inspector section and
bottom diagnostics tabs. Selecting a Scene Runtime entity changes the shared
Inspector into Scene instance context but does not replace the open Prototype
workspace.

The bottom host contains ordinary dockable tabs for Compiler Results, Output
Log, and Content Drawer. It remains quiet when diagnostics change. Diagnostics
set status indicators rather than opening popups.

Content Drawer is a normal dockable tab and is closed by default in the
Blueprint workspace. Hover expansion is deferred.

### Event and node authoring

Current Event nodes are limited to host-catalog events with fixed signatures.
The current catalog contains `Begin`, `Tick`, and `Input`. The graph spawn menu
creates those entries directly, including their names and pins.

Free-form Event names and event parameter add/remove/rename/retype/reorder
controls are removed. The Call Event node, its creation API, runtime path,
serialization coverage, and tests are removed until custom events return as a
separate feature.

The generic per-pin Name/Type/Container editor is removed. Fixed descriptor pins
are read-only in Inspector. Literal and input defaults remain editable inline on
nodes. Node-specific dynamic pin operations remain owned by node types that
actually need them.

Node title overrides and node-level comments are removed from the model,
serialization, editor, and tests. Reference IDs and target names remain
internal and are assigned by node-specific creation commands. Disabled remains
available from the graph context menu because it changes execution.

Graph comment boxes become the only graph annotation mechanism. The editor will
render and edit their title, color, position, size, and selection state using
the existing core storage and serialization.

### Prototype and Scene editing

Prototype component field edits propagate immediately to every matching entity
that still inherits the field. Prototype component additions and removals
propagate to every matching entity in every active runtime target. Removing a
component removes that component's local instance values as well. The operation
has no confirmation popup.

Scene instances can edit component fields only. An edit creates a local
override, the Inspector shows inherited versus local state, and Revert restores
the current Prototype value. Scene instances cannot add or remove components.
Prototype owns the component set.

Prototype behavior attachment additions and removals propagate immediately to
Prototype Preview and Scene Runtime. Adding a Behavior starts its `Begin` event.
Removing it cancels its active behavior state. Scene instances inherit the
Prototype Behavior list and have no local behavior additions or removals.

Each Prototype edit and all of its propagation is one per-document undo
transaction. Undo and redo are the recovery path for destructive changes; the
editor does not interrupt normal authoring with warning dialogs.

### Runtime targets

Prototype Preview and Scene Runtime are separate runtime worlds. They have
separate entities, behavior instances, activations, logs, and restart state.
They share authored data and the active viewport surface, not entity handles or
activation state.

The first implementation may defer a polished Prototype Preview surface, but it
must not label the Scene viewport as Prototype Preview. Scene Runtime gets an
explicit Play, Pause, Step, Restart, and Stop lifecycle and starts stopped.
Restart rebuilds from current in-memory authoring state. Stop discards runtime
state without undoing authored data.

The toolbar names the active target, Prototype Preview or Scene Runtime, and
runtime controls affect only that target. Prototype component edits live-patch
both targets while Scene Runtime is playing. Behavior graph edits take effect
in both targets only after a successful Save.

### Persistence and application

The top menu bar contains document-aware File, Edit, Asset, View, Debug,
Window, Tools, and Help menus. Unreal's grouping and visual hierarchy are used
without adding unsupported commands such as Diff, Hide Unrelated, Class
Settings, or Eject.

Save writes and applies the active asset. Save All handles every dirty
Prototype, Behavior, and Scene source. Save does not auto-run for every text
change. New or pathless assets use Save As. Reload from disk is separate from
Restart runtime.

Behavior Save writes the authored graph first, then compiles and applies the
shared Behavior asset to every attached instance in Prototype Preview and Scene
Runtime. If compile or apply fails, the authored graph remains saved, the last
valid runtime snapshot remains active, and the status/diagnostics area reports
that application is blocked. Prototype component edits are already live, and
Scene Save persists bindings and local field overrides.

Source writes use atomic replacement. Save, Save All, Undo, and Redo update dirty
state and runtime status. There is no auto-save in this pass.

### ImGui shell

The app already submits one ImGui frame per host loop. This decision does not
change frame creation or submission. It reduces the number of persistent dock
windows and routes panel submission by context.

The default layout contains:

- a left Components surface and My Blueprint surface;
- a central active asset tab with Preview and graph tabs;
- a right universal Inspector;
- a quiet bottom host for Compiler Results, Output Log, and Content Drawer.

All surfaces remain independently dockable, resizable, and persistent through
`imgui.ini`.

## Consequences

The editor reads like a Blueprint document while retaining the project's
component and Behavior composition model. Prototype defaults can be inspected
and changed without confusing them with scene-instance overrides. Scene
selection can be inspected without losing the Prototype workspace.

Live component propagation makes the scene useful as an editor preview. It also
requires a reliable per-instance baseline and a transaction that can update all
matching entities. Per-document history becomes necessary for Prototype and
Scene data rather than only Behavior graphs.

Behavior graph Save can recreate the shared behavior's runtime instances, as
specified by ADR 0020. Component edits do not require per-keystroke behavior
teardown. A failed Behavior apply leaves the file and runtime at different
versions, so the Inspector and diagnostics must show the stale runtime state.

Removing custom events, Call Event, node title overrides, node comments, and
scene-local Behavior composition narrows the current product. The removed
features can return only with explicit ownership and persistence designs.

The first implementation will temporarily have a smaller Prototype workspace
than Unreal because Preview and reflected component editing are not yet
complete. The shell should expose the final ownership model before those
surfaces are fully populated.

## Alternatives rejected

### Keep the current mixed workbench

This keeps Scene hierarchy, Blueprint, Prototype Details, Runtime inspection,
Content Drawer, and graph Details as independent windows. It preserves current
code but does not make the active ownership or selection context clear.

### Merge Behavior assets into Prototype

This would make My Blueprint look more like a single Unreal Blueprint, but it
would destroy Behavior reuse and require a new graph, variable, runtime, and
serialization ownership model.

### Use only an explicit Apply button for Prototype values

This avoids live propagation but makes the scene preview lag behind the
Prototype editor. Prototype component data is separate from the immutable
applied Behavior definition, so field and component-set propagation can happen
without violating ADR 0020.

### Keep custom Events and Call Event in the generic Event node

This retains flexibility at the cost of the oversized inspector and an unclear
boundary between host events and authored events. A future Custom Event feature
can restore it with its own creation and signature workflow.
