# Editor UI slices

Status: the Content Drawer slice is implemented.

The former prototype-first workbench and the Minimap panel are not available in the running editor. The shell exposes a dockable `Content Drawer` in the shared bottom tab host. `Scene hierarchy` and `Blueprint` share the left context area. The prototype and scene model, runtime, and persistence paths remain available to host code and tests; this slice does not add a replacement for the removed authoring controls.

The Scene, Prototype, and Behavior document contract is recorded in [editor-document-system.md](editor-document-system.md). The runtime registry remains an implementation detail; source files and paths are the editor-facing asset identity.

## Slice 1: shell and layout

The shell now provides:

- a restrained dark skin with a blue interaction accent;
- a versioned dockspace with a useful default layout;
- movable, resizable, dockable panels;
- a `Window > Reset default layout` command;
- persistent Dear ImGui layout settings through `imgui.ini`.

The default layout puts the scene and graph in central tabs, `Scene hierarchy` and `Blueprint` in left context tabs, Details on the right, and Content Drawer, Breakpoints, Compiler Results, and Output Log in one bottom tab host. Content Drawer is open in the default layout.

## Slice 2: Content Drawer

The dockable `Content Drawer` is rooted at `content/` and follows a filesystem browser model. It opens at the root, navigates one directory at a time, shows breadcrumbs and back/forward history, and renders folders before files in responsive Grid or List views. Search matches project-relative paths case-insensitively, and `Refresh` rescans the root. A single click selects an item; double-clicking a folder enters it and double-clicking a recognized file opens its prototype or behavior document. Unknown files remain visible without a fabricated asset type.

Prototype and behavior actions live under one Content menu. The drawer keeps source paths visible beside the runtime asset names, while runtime state still reports saved, modified, or error state for recognized assets. The root is configurable through `app_scene_authoring_set_content_root`, so a host can mount a different project content directory without changing the browser.

This slice does not add resource thumbnails, drag/drop, a persistent asset index, file rename/delete, scene-instance overrides, behavior attachment controls, save/apply UI, or runtime attachment debugging. The model and persistence paths remain available to host code and tests.

The visual pass adds the embedded Nerd Font symbols to the normal ImGui font and gives the asset browser shared icon-led row and card primitives. Cards and rows use larger names, colored asset icons, state dots, and selected/hover surfaces instead of packing instance counts and state words into one label. The row, card, section primitives, and theme tokens live in `editor/ui_shell.c` so later panes can share the same visual language.

The section plus menus now create and duplicate prototypes, create and duplicate behavior blueprints, and spawn prototype instances. Prototype and behavior selections remain linked, as in the HTML mockups, but only the active editing context receives the strong selection outline.

The baseline and interaction rules are recorded in `docs/editor-ui-style.md`.

Checkpoint: launch the app, confirm `Content Drawer` is open in the bottom tab host, expand the `content/` tree visually, search for a path with different letter casing, refresh the tree, select a prototype or behavior file, open the Content menu, and use `Window > Reset default layout` to restore the layout.

## Slice 3: scene hierarchy context

The dockable `Scene hierarchy` panel lists the authored names of the current scene actors. Rows are selectable and use the existing instance-selection path, so the selected actor and behavior inspection state stay synchronized. An empty scene shows a short empty-state label. The panel is read-only in this slice; it does not expose scene editing controls.

The default shell keeps `Scene hierarchy` and `Blueprint` as tabs in the left context area. `Scene` and `Node Canvas` remain tabs in the central node, `Details` stays on the right, and Content Drawer shares the bottom tab host with the three diagnostics.

## Slice 4: explicit document context

The host keeps a fixed active-document context with the three approved kinds and a borrowed stable identity. Scene hierarchy selection activates `Scene` and focuses the central `Scene` tab; the Content menu's `Open prototype` action activates `Prototype` without moving central focus because no preview surface exists; and opening a behavior file activates `Behavior`, focuses `Node Canvas`, and loads the existing graph editor. Selecting a prototype file does not select a scene instance. The shell continues to submit its existing windows for all three contexts because Prototype preview/Details and Scene-instance Details are not implemented yet.

The context is read-only at the app boundary so later panel routing can switch directly on document kind without introducing a generic registry. See [editor-document-system.md](editor-document-system.md) for the exact state and limitations.

## Replacement order

1. Content Drawer and scene hierarchy.
2. Explicit document context.
3. Scene viewport.
4. Inspector and override presentation.
5. Behavior graph workspace.
6. Runtime diagnostics and debugger.

Each slice should have a visible empty, loading, modified, and error state where those states apply. Content code should not force a window into a dock node; the shell owns layout and users can change it.
