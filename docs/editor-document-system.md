# Editor document system

Status: ADR 0021 and `docs/unreal-blueprint-editor-plan.md` define the current
workflow. The editor uses three top-level document kinds: `Scene`, `Prototype`,
and `Behavior`. A generic document registry is reserved for future asset
editors; it is not implemented by the current UI slices.

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
- World Outliner;
- the universal Inspector for the selected scene instance;
- Scene Runtime controls and diagnostics;
- Content Drawer for opening or spawning related assets.

A scene actor is a selection inside the Scene document, not a fourth document kind.
Its Inspector can show Prototype identity, inherited/local component fields, and
runtime attachment state. Scene instances cannot add or remove components or
Behaviors.

### Prototype

A Prototype document represents one `game_prototype` asset.

Its default surfaces are:

- an isolated preview of the spawned entity;
- Components and editable component fields;
- attached Behaviors;
- prototype defaults, includes, and validation;
- save and apply commands where the model permits them.

Components owns Prototype component structure. My Blueprint lists attached
Behavior modules and their graph members. Inspector edits Prototype defaults and
reports dirty state. The Content Drawer remains an asset browser and does not
own attachment commands.

The preview must use an isolated runtime or preview world. Editing a prototype must not mutate an already spawned scene instance.

### Behavior

A Behavior document represents one behavior asset and its working blueprint.

Its default surfaces are:

- the event graph canvas;
- My Blueprint for event, function, macro, and variable members;
- the universal Inspector for node and variable context;
- compiler results and Output Log;
- runtime inspection only when a Scene Runtime attachment is selected.

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
- a left Components/My Blueprint area for Blueprint documents;
- a left World Outliner for Scene documents;
- one right universal Inspector;
- one bottom tab host for Compiler Results, Output Log, and Content Drawer.

Content Drawer is a normal dockable tab and is closed by default in the Blueprint
workspace. It scans the configurable `content/` root, opens at that root,
navigates one directory at a time, and preserves project-relative paths through
breadcrumbs and back/forward history. It supports Grid and List views,
case-insensitive path search, single selection, and double-click activation.
Recognized Prototype and Behavior files open through their document commands;
unknown files remain visible without a fabricated type. A persistent asset
index, resource thumbnails, and drag/drop are deferred.

## Data-driven boundaries

Data-driven does not mean that every panel is generated from untyped metadata. Keep domain data explicit and use metadata where it is stable:

- the prototype registry supplies component descriptors and field layout;
- scene documents supply instance names, prototype identity, and sparse overrides;
- the behavior registry supplies asset identity and working blueprints;
- graph metadata supplies nodes, pins, diagnostics, and view state;
- document descriptors supply panel availability and default placement.

All edits should go through model commands or existing mutation APIs so they remain serializable, undoable where supported, and safe against live runtime state.

## Growth path

1. The host active-document state and filesystem-backed Content Drawer remain
   fixed boundaries.
2. Complete the universal Inspector's scene-instance override and Revert paths.
3. Add document-aware history and complete Revert All behavior.
4. Add Prototype Preview as a separate runtime target.
5. Route future Behavior document view state and panel availability through the
   exposed context.
6. Extract the shared document and panel descriptor contract before adding
   another asset editor.

Do not implement a generic registry before the first three document kinds expose real workflows. The registry should capture repeated contracts from those implementations rather than guess at every future editor in advance.

## Historical active-document slice

The following notes describe the pre-ADR-0021 slice and remain for migration
history. The plan and ADR above define the current shell.

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
- Prototype Preview still needs an explicit preview-world boundary;
- scene-instance field editing needs document history and complete Revert All
  behavior;
- Prototype Preview still needs its separate runtime world;
- Save As uses deterministic content-root paths because a file-picker UI is not
  implemented;
- the context router is explicit but remains a fixed three-kind switch.
