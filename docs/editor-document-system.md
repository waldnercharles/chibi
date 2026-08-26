# Editor document system

Status: the explicit active-document slice is implemented. The editor uses three top-level document kinds: `Scene`, `Prototype`, and `Behavior`. A generic document registry is reserved for future asset editors; it is not implemented by the current UI slices.

## Purpose

The editor should change its visible work surfaces when the user opens a scene, prototype, or behavior. The shell owns docking and layout. A document owns the active editing context, selection, persistence state, and view state. Panels read that context and expose commands without owning the underlying game data.

This follows the useful part of Unreal's editor model while keeping the implementation small and data-driven:

- a scene is a level-editing document;
- a prototype is an actor-class document;
- a behavior is an event-graph document;
- specialized future editors can register new document kinds without changing the shell contract.

## Top-level document kinds

### Scene

A Scene document represents one scene resource or the current in-memory scene.

Its default surfaces are:

- Scene viewport;
- Scene hierarchy;
- Details for the selected scene instance;
- scene and runtime output;
- Content Drawer for opening or spawning related assets.

A scene actor is a selection inside the Scene document, not a fourth document kind. Its Details surface can show prototype identity, component overrides, behavior additions and removals, and runtime attachment state.

### Prototype

A Prototype document represents one `game_prototype` asset.

Its default surfaces are:

- an isolated preview of the spawned entity;
- Components and editable component fields;
- attached Behaviors;
- prototype defaults, includes, and validation;
- save and apply commands where the model permits them.

The current Prototype Details surface implements the first authoring step: it
shows the selected prototype identity and attached behavior assets, then adds
an already registered behavior through the Properties panel. The Content
Drawer remains an asset browser and does not own attachment commands.

The preview must use an isolated runtime or preview world. Editing a prototype must not mutate an already spawned scene instance.

### Behavior

A Behavior document represents one behavior asset and its working blueprint.

Its default surfaces are:

- the event graph canvas;
- the graph tree for event, function, and macro graphs;
- node and pin Details;
- compiler results;
- breakpoints and runtime inspection when an instance is selected.

Opening a behavior preserves graph view state by stable graph identity. Runtime inspection is a selection subcontext, not a separate document kind.

## Generic document contract

A future document registry should describe documents with stable data rather than window names. Each document needs:

- a stable kind identifier;
- a stable document identifier, such as a path or asset ID;
- a display name and source name;
- dirty, loading, read-only, and error state;
- an open, close, save, and reload boundary;
- a selection model;
- per-document view state;
- a set of supported panels and commands.

Panel descriptors should declare:

- a stable panel ID;
- the document kinds where the panel is valid;
- the selection kinds it can consume;
- its default shell region;
- whether it is primary, contextual, or diagnostic;
- the command and model operations it exposes.

The descriptor determines availability. The panel implementation still remains an ordinary C draw function selected by a direct `switch` or a small explicit registry. Do not build a generic C++ panel hierarchy or put game data inside ImGui window state.

## Selection contract

Selection is separate from the active document. A selection should identify its kind and stable owner:

- scene instance;
- prototype component or field;
- behavior graph, node, pin, or diagnostic;
- runtime behavior attachment or activation.

A panel may require one selection kind, accept several kinds, or show an empty state. Changing selection must not silently change the active document unless an explicit open or focus command requests it.

For example, selecting a scene instance updates Scene Details. Choosing `Open prototype` changes the active document to that prototype. Selecting a behavior attachment can keep the Scene document active while opening the Behavior document only on an explicit command.

## Shell relationship

`editor/ui_shell.c` should remain a layout and theme boundary. It should know panel IDs and default regions, but not prototype fields, scene entities, or graph mutation rules.

The document/context owner should decide which panel windows submit content for the active document. The shell then places those windows in the user's dock layout. A reset layout may provide defaults, but normal editing must not depend on internal dock node IDs.

The current direction is:

- central tabs for Scene and Node Canvas;
- a left context area for Scene hierarchy and Blueprint tree;
- a right Details area split between graph-node Details and Prototype Details;
- one bottom tab host for Content Drawer, Breakpoints, Compiler Results, and Output Log.

Content Drawer is open by default in the bottom host. It scans the configurable `content/` root, opens at that root, navigates one directory at a time, and preserves project-relative paths through breadcrumbs and back/forward history. It supports Grid and List views, case-insensitive path search, single selection, and double-click activation. Recognized prototype and behavior files open through their existing document commands; unknown files remain visible without a fabricated type. A persistent asset index, resource thumbnails, and drag/drop are deferred.

## Data-driven boundaries

Data-driven does not mean that every panel is generated from untyped metadata. Keep domain data explicit and use metadata where it is stable:

- the prototype registry supplies component descriptors and field layout;
- scene documents supply instance names, prototype identity, and sparse overrides;
- the behavior registry supplies asset identity and working blueprints;
- graph metadata supplies nodes, pins, diagnostics, and view state;
- document descriptors supply panel availability and default placement.

All edits should go through model commands or existing mutation APIs so they remain serializable, undoable where supported, and safe against live runtime state.

## Growth path

1. The host active-document state, filesystem-backed Content Drawer, and the first Prototype Details behavior attachment command are implemented by this slice.
2. Build Scene Details for selected instances.
3. Build the Prototype preview and Components surfaces.
4. Add behavior removal, new behavior authoring, and the live apply boundary.
5. Route future Behavior document view state and panel availability through the exposed context.
6. Extract the shared document and panel descriptor contract before adding another asset editor.

Do not implement a generic registry before the first three document kinds expose real workflows. The registry should capture repeated contracts from those implementations rather than guess at every future editor in advance.

## Implemented active-document slice

`app/scene_authoring.c` now owns one explicit `app_editor_document_context` with a fixed kind switch and a borrowed stable identity. The current transitions are:

- construction and scene-hierarchy selection activate `Scene` with the stable scene identity `Scene`;
- selecting a Content Drawer prototype keeps the prototype selected for asset commands without changing a scene-instance selection;
- selecting a recognized prototype file keeps `Prototype` selected for asset commands, and the Content menu opens it with its prototype name as identity;
- selecting a recognized behavior file activates `Behavior` with the behavior asset name and loads its working blueprint into the existing graph editor.

The context is exposed as read-only state for later panel routing. Asset rows derive their active outline from the document kind. Activating Scene focuses the central `Scene` tab, and activating Behavior focuses the central `Node Canvas` tab. Prototype activation does not focus an unrelated surface; focus waits for its preview surface. The shell and existing panel submission stay unchanged, so selecting a Prototype does not hide the Scene viewport or graph tools while Prototype surfaces are unavailable.

The central-tab behavior needs a manual UI check: select a scene instance from `Scene hierarchy`, open a behavior asset from `Content Drawer`, and confirm the visible central tab changes to `Scene` and `Node Canvas` respectively. Select a prototype row and confirm selection changes without moving focus; use `Open prototype` as the explicit prototype document boundary. Close Content Drawer, confirm it stops submitting, then reopen it from `Window` and confirm it returns focus after submission.

## Current gaps

The current code has the model seams but not the full document router:

- `editor/editor.c` still owns behavior graph editing and graph view state, while the host owns the top-level context;
- `editor/ui_shell.c` owns theme and docking;
- prototype component APIs and an isolated preview still need replacement surfaces;
- the Prototype Details panel currently attaches registered behavior assets but does not remove them or create new behavior assets;
- attachment is an in-memory model command; prototype save and the full live apply lifecycle remain separate commands;
- the Scene hierarchy is still read-only;
- an isolated prototype preview still needs an explicit preview-world boundary;
- scene-instance Details and context-specific panel visibility still need to be routed from the exposed context.
