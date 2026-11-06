# Chibi editor workflow comparison

Research date: 2026-09-09

This comparison uses first-party documentation from VS Code, Unreal Engine,
Unity, GameMaker, and Blender. The goal is to identify interaction patterns
that transfer well to chibi without copying an entire engine's UI.

## Patterns shared by several editors

### One workbench, many views and editors

Editors separate the document or graph being edited from views that help locate
and inspect it. VS Code calls the overall shell the workbench and provides
dedicated views for files, search, source control, and debugging. Its editor
region supports multiple tabs and editor groups, including side-by-side
editing. Unreal's Content Browser is dockable, can be moved to a sidebar, and
supports up to four instances. Unity's Project window is the main surface for
navigating project files and can be docked or floated. Blender allows the same
editor type to appear in multiple areas and uses pinned data contexts when a
node editor should remain focused on one material or node tree.

For chibi, this supports a document-centered workbench:

- Keep one persistent Content/Assets surface that can be filtered to Scenes,
  Prototypes, Behaviors, Events, Materials, and other asset kinds.
- Let documents open as named tabs, with multiple tabs and split editor groups
  available when the user wants a Prototype beside its Behavior or Material.
- Treat Navigator, Scene, Inspector, and graph views as views into the active
  document rather than separate destinations with unrelated selection state.
- Let the active document be pinned when editing a Material or graph while
  inspecting another object.

### Global back/forward plus recent editor history

VS Code exposes Go Back and Go Forward as workbench navigation commands and
also has a separate recently used editor-group history. Unreal's Content
Browser has browser-like back and forward buttons for recent paths. GameMaker
provides a Workspace Overview for open items and a Goto Window for rapid asset
navigation.

Chibi should maintain a global route history independent of Navigator. A route
should include the asset, subview or graph, selected node or component when
useful, and graph viewport state when returning to a graph. Back/forward should
work for Prototype -> Behavior -> Event -> Material, while a separate tab or
recent-document switcher handles choosing among open documents. This makes
Prototype/Behavior round trips predictable without making the user search
again.

### A single quick-open or find-anything entry point

VS Code's Quick Open opens any file by name and its Command Palette exposes
commands. Unity's Search window searches project assets, scenes, packages, and
other providers, supports textual or visual queries, and can save queries.
GameMaker's Goto Window searches every asset as the user types. Unreal's
Content Browser combines text filtering with type and other filters.

Chibi should have one keyboard-first Find Anything command and one visible
search affordance. Results should be grouped by asset kind and offer direct
actions: open, reveal in Content, select, inspect, and show usages. A search
should still work when the requested item is outside the current folder or
view. Type filters and reference filters can be added incrementally after the
basic name search is fast and useful.

### References are a first-class navigation result

VS Code supports Go to Definition, Go to References, implementations, and a
peek view that shows results inline before opening a full editor. Unreal has a
Reference Viewer that displays both assets referenced by the current asset and
assets that reference it. Unreal Blueprint Search can search nodes, pins,
graphs, variables, components, and values across all Blueprints, and its Find
References action uses an element identifier to avoid name collisions. Unity's
Search providers include reference filters such as finding objects or assets
that use a particular Material. GameMaker's project search searches scripted
assets and double-clicking a result opens and pans to the matching location.

The useful common abstraction for chibi is a typed relationship index, not a
generic text search alone. At minimum it should answer:

- Which Behaviors consume or emit this Event?
- Which Prototypes include this Behavior?
- Which components use this Material?
- Which assets reference this Prototype, Behavior, Event, or Material?
- Which graph nodes or pins use a selected resource or property?

The result should be a list first, with a compact dependency/reference graph
available when relationships are dense. Each result must be actionable: open
the asset, select the component or node, and preserve a back route to the
origin. This is the path to easy cleanup of unused components and to safely
splitting large Behaviors.

### Graph editors make discovery local and contextual

Blender and Unity both make graph navigation and node creation available from
the graph itself. Blender provides Find Node, which searches node names, socket
labels, warnings, and some values, then pans and centers on the match. It also
supports Frame Selected and Frame All, a contextual node search when dragging
from a socket, reroute nodes, breadcrumbs for nested groups, and a pinned
context. Unity Shader Graph opens node creation with Space or a context menu,
supports node search, and offers a contextual Create Node menu that filters to
compatible data types.

For chibi, graph tooling should include a named graph tab, Find Node, Frame
Selected, Frame All, Arrange Nodes, graph breadcrumbs, and contextual node
creation from a port. The inspector can remain useful for advanced settings,
but values needed for ordinary authoring should be visible on the node. A
node's display name should describe the operation directly; the docs for both
Unity and Blender use names such as Color and Mix inside a Shader Editor
context, rather than prefixing every node with `Shader`.

## Shader authoring: Blender versus Unity

Blender's Shader Editor is a general node editor for material node trees. The
header selects the data context (Object, World, or Line Style) and material
slot, while the node tree remains the main authoring surface. A Sidebar exposes
selected-node and editor settings, and Pinned keeps the chosen node tree fixed
while other objects are selected. Blender also supports direct graph
navigation, linked-node selection, reroutes, and search by node metadata.
This separates the material node tree from the object and material-slot context.

Unity Shader Graph is more guided. Its window combines a Master Stack for final
outputs, a Main Preview, a Blackboard for graph Properties and Keywords, and a
Graph Inspector for settings. Properties can be dragged from the Blackboard
into the graph, and an Exposed property appears in the Material Inspector for
per-material values. On-node changes update the preview immediately. Unity's
contextual node menu uses type compatibility to reduce irrelevant choices.
The target and output stack constrain the graph, while reusable inputs and
material-instance values have explicit homes. Neither model is simply a lower
or higher level version of the other.

The practical direction for chibi is a hybrid. Keep a small, direct graph
model with Blender-style search, framing, breadcrumbs, pinning, and contextual
insertion. Add Unity-style intent and constraints: a clear material output,
named input properties, a visible preview, and a distinction between graph
defaults and values edited on a Material instance. Keep common values on nodes
or in a compact graph-side property panel; use the Inspector for advanced or
rare settings. Preview updates should be incremental and coalesced while a
slider is dragged, with cached intermediate results where possible, so editing
Color Ramp values does not stall the UI.

For color editing, expose RGBA channels directly and provide an HSV view in
the same picker. Preserve a compact visual swatch for quick work, but allow
numeric entry and copy/paste of color values. Unity and Blender both make the
color or property part of the graph authoring context; hiding the only useful
controls in a separate Inspector adds an unnecessary context switch.


### How chibi currently compares

Chibi already uses a material graph with a Surface output for Base Color,
Emission, Roughness, Metallic, Normal, Opacity and Unlit, plus textures,
procedural inputs, ramps and named parameters. Its output model is closer to
the surface-description part of Unity Shader Graph than to a general shading
language. Unity's [Master Stack](https://docs.unity3d.com/Packages/com.unity.shadergraph@17.0/manual/Master-Stack.html)
also has a Vertex context; chibi does not currently expose that workflow.
Blender's [Material Output](https://docs.blender.org/manual/en/latest/render/shader_nodes/output/material.html)
accepts Surface, Volume and Displacement. Chibi's Surface output is narrower.

The current difficulty is largely in authoring: important settings live in the
Inspector, ramps contain hidden array values, numeric edits can rebuild GPU
programs, and node titles repeat their shader domain. These findings come from
`src/graph/shader/shader_nodes.c`, `src/editor/panels/shader_controls.c`, and
`src/render/shader_catalog.c`. Improving those interactions does not require
turning the graph into a different shader language.

## What to borrow from each tool

### VS Code

Borrow the workbench model, keyboard-first Quick Open, global back/forward,
recent editor history, named tabs, split groups, breadcrumbs, and peek-style
reference results. Its language-aware definition/reference model is the best
analogy for navigating between chibi asset types. Do not copy code-specific
terminology or assume that every relationship is a textual symbol lookup.

### Unreal Engine

Borrow the Content Browser or Content Drawer idea, browser-like history,
advanced asset filters, Reference Viewer, and asynchronous project-wide graph
search. Unreal is the closest reference for a graph-heavy game editor because
it treats assets, graph documents, references, and editor panes as one system.
Keep chibi's first version smaller: one Content surface and one relationship
index cover the requested workflows before adding collections, size maps, or
multiple specialized browser instances.

### Unity

Borrow the Project window as the central asset source, Search providers and
query filters, direct reference inspection, and the Shader Graph split between
graph properties and Material instance values. Unity's `Project`, `Hierarchy`,
and `References` search concepts map cleanly onto chibi's Assets, Scene, and
usage queries.

### GameMaker

Borrow the low-friction Asset Browser, type/tag filters, Goto Window, and
Workspace Overview. These are good fits for a smaller editor: the user can
find any asset quickly without learning a complex hierarchy, and open items
remain reachable through a lightweight recent-item switcher. Its Search in
Assets workflow also shows the value of opening a result directly at the
matching location.

### Blender

Borrow graph-local search, Frame All and Frame Selected, linked selection,
reroutes, breadcrumbs, pinning, and contextual node insertion. Blender's
general node editor makes large graphs navigable through operations on the
graph itself, rather than requiring every action to happen in a remote panel.

## Suggested sequencing for chibi

1. Define a document and route model that can represent Scene, Prototype,
   Behavior, Event, Material, and graph sublocations. Add named tabs, global
   back/forward, and a recent-document switcher.
2. Build a single Content/Assets view with fast text search, type filters, and
   open/select/reveal actions. Add Find Anything as a command and visible
   button.
3. Build a relationship index for Prototype, Behavior, Event, component, and
   Material references. Add list results first, then a graph view for dense
   relationships.
4. Improve graph authoring with graph-local search, Frame Selected/All,
   Arrange Nodes, breadcrumbs, contextual insertion, visible node values, and
   responsive preview invalidation.
5. Migrate Arena3D assets and simplify Behaviors using the new navigation and
   usage tooling, so large graphs can be split based on observed ownership and
   references rather than guesswork.

## Primary sources

- [VS Code code navigation](https://code.visualstudio.com/docs/editing/editingevolved) — Quick Open, definition, implementation, symbols, Peek, and references.
- [VS Code default keybindings](https://code.visualstudio.com/docs/reference/default-keybindings) — global back/forward, editor history, Quick Open, symbols, and references.
- [VS Code custom layout](https://code.visualstudio.com/docs/configure/custom-layout) — tabs, editor groups, and split editors.
- [VS Code tips and tricks](https://code.visualstudio.com/docs/editing/getting-started/tips-and-tricks) — navigation history, recent files, multiple Quick Open files, and reference view.
- [Unreal Content Browser](https://dev.epicgames.com/documentation/en-us/unreal-engine/content-browser-in-unreal-engine) — central asset browser, filters, Content Drawer, docking, and multiple browser instances.
- [Unreal Content Browser interface](https://dev.epicgames.com/documentation/unreal-engine/content-browser-interface-in-unreal-engine?lang=en-US) — folder path history, back/forward, breadcrumbs, sources, and filters.
- [Unreal Reference Viewer](https://dev.epicgames.com/documentation/unreal-engine/reference-viewer-in-unreal-engine?lang=en-US) — bidirectional asset reference graph.
- [Unreal Blueprint Search](https://dev.epicgames.com/documentation/unreal-engine/blueprint-search?application_version=4.27) — project-wide graph search, Find References, indexing, and asynchronous search.
- [Unreal Blueprint Editor menu](https://dev.epicgames.com/documentation/unreal-engine/menu-for-the-blueprints-visual-scripting-editor-in-unreal-engine?lang=en-US) — Find in Blueprints, Reference Viewer, and Find in Content Browser actions.
- [Unity Project window](https://docs.unity3d.com/6000.0/Documentation/Manual/ProjectView.html) — central asset navigation, docking, and layouts.
- [Unity Search window](https://docs.unity3d.com/6000.0/Documentation/Manual/search-window-reference.html) — providers, textual/visual queries, saved searches, and search history.
- [Unity project and scene search](https://docs.unity3d.com/6000.0/Documentation/Manual/search-project-and-scenes-overview.html) — Project, Hierarchy, Asset Database, type, property, and reference filters.
- [Unity direct reference asset management](https://docs.unity3d.com/6000.0/Documentation/Manual/assets-direct-reference.html) — serialized direct references and dependencies.
- [Unity Shader Graph window](https://docs.unity3d.com/Packages/com.unity.shadergraph@10.5/manual/Shader-Graph-Window.html) — Master Stack, Preview, Blackboard, Graph Inspector, framing, and graph controls.
- [Unity Shader Graph first graph](https://docs.unity3d.com/Packages/com.unity.shadergraph@10.5/manual/First-Shader-Graph.html) — node search, on-node color editing, exposed properties, and Material Inspector values.
- [Unity Shader Graph Create Node menu](https://docs.unity3d.com/Packages/com.unity.shadergraph@10.8/manual/Create-Node-Menu.html) — searchable and contextual node insertion.
- [Unity Shader Graph Blackboard](https://docs.unity3d.com/Packages/com.unity.shadergraph@10.5/manual/Blackboard.html) — graph properties, keywords, drag-to-create nodes, and ordering.
- [GameMaker Asset Browser](https://manual.gamemaker.io/lts/en/Introduction/The_Asset_Browser.htm) — asset organization, text filtering, type filters, tags, and sorting.
- [GameMaker IDE input and navigation](https://manual.gamemaker.io/lts/en/IDE_Navigation/IDE_Input.htm) — Goto Window and Workspace Overview.
- [GameMaker Edit menu](https://manual.gamemaker.io/lts/en/IDE_Navigation/Menus/The_Edit_Menu.htm) — Search in Assets and result navigation.
- [Blender Shader Editor](https://docs.blender.org/manual/en/dev/editors/shader_editor.html) — material node-tree context, material slots, pinning, and Sidebar settings.
- [Blender node editors](https://docs.blender.org/manual/en/5.0/interface/controls/nodes/node_editors.html) — graph header, breadcrumbs, pinning, reroutes, Sidebar, framing, and contextual insertion.
- [Blender selecting nodes](https://docs.blender.org/manual/en/latest/interface/controls/nodes/selecting.html) — Find Node, linked selection, and automatic pan/center on a match.
