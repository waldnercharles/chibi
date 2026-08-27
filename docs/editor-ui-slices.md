# Editor UI slices

Status: the shell, contextual Blueprint panels, Scene Runtime controls, and
prototype field slice are implemented. ADR 0021 and
[unreal-blueprint-editor-plan.md](unreal-blueprint-editor-plan.md) supersede
older slice descriptions below where they differ.

The shell exposes a normal dockable `Content Drawer` in the shared bottom tab
host. Prototype documents use Components, My Blueprint, and Inspector. Scene
documents use World Outliner and Inspector. The prototype and scene model,
runtime, and persistence paths remain available to host code and tests.

The Scene, Prototype, and Behavior document contract is recorded in [editor-document-system.md](editor-document-system.md). The runtime registry remains an implementation detail; source files and paths are the editor-facing asset identity.

## Slice 1: shell and layout

The shell now provides:

- a restrained dark skin with a blue interaction accent;
- a versioned dockspace with a useful default layout;
- movable, resizable, dockable panels;
- a `Window > Reset default layout` command;
- persistent Dear ImGui layout settings through `imgui.ini`.

The default layout puts Scene and Node Canvas in central tabs, Components and
My Blueprint in the left Blueprint area, Inspector on the right, and Compiler
Results, Output Log, and Content Drawer in one bottom tab host. Content Drawer
is closed by default.

## Slice 2: Content Drawer

The dockable `Content Drawer` is rooted at `content/` and follows a filesystem browser model. It opens at the root, navigates one directory at a time, shows breadcrumbs and back/forward history, and renders folders before files in responsive Grid or List views. Search matches project-relative paths case-insensitively, and `Refresh` rescans the root. A single click selects an item; double-clicking a folder enters it and double-clicking a recognized file opens its prototype or behavior document. Unknown files remain visible without a fabricated asset type.

Prototype and behavior actions live under one Content menu. The drawer keeps source paths visible beside the runtime asset names, while runtime state still reports saved, modified, or error state for recognized assets. The root is configurable through `app_scene_authoring_set_content_root`, so a host can mount a different project content directory without changing the browser.

Resource thumbnails, drag/drop, a persistent asset index, file rename/delete,
and Prototype Preview remain deferred. Save As writes a deterministic default
path under the content root; Reload restores the active document from its
source path. Behavior attachment, scene-instance override, Save, and runtime
lifecycle surfaces are now present.

The visual pass adds the embedded Nerd Font symbols to the normal ImGui font and gives the asset browser shared icon-led row and card primitives. Cards and rows use larger names, colored asset icons, state dots, and selected/hover surfaces instead of packing instance counts and state words into one label. The row, card, section primitives, and theme tokens live in `editor/ui_shell.c` so later panes can share the same visual language.

The section plus menus now create and duplicate prototypes, create and duplicate behavior blueprints, and spawn prototype instances. Prototype and behavior selections remain linked, as in the HTML mockups, but only the active editing context receives the strong selection outline.

The baseline and interaction rules are recorded in `docs/editor-ui-style.md`.

Checkpoint: launch the app, confirm `Content Drawer` is closed by default,
open it from `Window`, expand the `content/` tree, search for a path with
different letter casing, refresh the tree, select a prototype or behavior
file, use `File > Save As...` and `File > Reload from disk`, and use `Window >
Reset default layout` to restore the layout.

## Slice 3: World Outliner and contextual Inspector

The dockable `World Outliner` panel lists the authored names of current Scene
instances. Rows are selectable and update only the shared Inspector context.
Scene Runtime exposes Play, Pause, Step, Restart, and Stop in Inspector. A
selected instance exposes inherited/local component fields and Revert controls.
Prototype documents do not use World Outliner as their Components pane.

## Slice 4: explicit document context

The host keeps a fixed active-document context with the three approved kinds and
a borrowed stable identity. Scene selection activates Scene and focuses World
Outliner/Inspector. Opening a prototype activates Prototype and focuses
Components. Opening a behavior activates Behavior and focuses My Blueprint and
Node Canvas. Selecting a scene instance changes only Inspector context. The
context remains a direct switch rather than a generic registry.

## Replacement order

1. Context-routed shell with Components, My Blueprint, World Outliner, and
   Inspector.
2. Prototype component editing and live instance propagation.
3. Scene Runtime lifecycle and instance overrides.
4. Behavior working-copy/apply boundary and document-aware Save.
5. Prototype Preview as a separate runtime target.

Each slice should have a visible empty, loading, modified, and error state where those states apply. Content code should not force a window into a dock node; the shell owns layout and users can change it.
