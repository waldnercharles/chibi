# Studio: design direction for the RmlUi editor

This study refines the approved **A · Studio** layout from the `cwaldner/editor-design-prototype` branch. It uses real Neon Swarm content: `fixture.py` bundles `projects/arena2d` (the Sandbox scene, 24 prototypes and 14 behaviors). The [component kit](kit.html) is the specification for the RmlUi port. Each control there lists its behavior contract, states and keys.

## Sources and what each contributes

| Source | Contribution |
| --- | --- |
| **Base UI** | Behavior contracts: parts, `data-*` state names, keyboard models, change versus commit, focus return, delays. Every control follows one of these. |
| **shadcn/ui** | Semantic color roles, variant discipline (ghost, subtle, primary, outline, danger), Input Group anatomy, and translucent hover/press overlays. The dense "Mira" style sets our control heights. |
| **cmdk** | One command surface for search, Quick Open, Add Object, Attach Behavior and Add Node. It ports `command-score` ranking and page navigation. |
| **DialKit** | One row rhythm, controls that are their own scrub area, fill-bar sliders, hover-revealed affordances, and the color popover. |
| **Blender** | Areas with editor-type headers, the Properties context tabs, collapsible panels, Outliner toggles, modal G/R transforms, status-bar key hints, node category and socket colors, and orange viewport selection. |
| **Motion, NumberFlow, Torph, Sonner, Virtuoso, dnd-kit, Shiki, Liveline, Recharts, zustand, next-themes, clsx, cva** | Each solves one problem the editor also has. Each maps to a kit primitive that follows its contract; see [Editor UI references](../../editor-ui-references.md). |

## Changes from Studio A

| Before | After | Why |
| --- | --- | --- |
| Two top rows: menubar, then documents and transport | One 30 px top bar holds menus, documents, transport and search | Returns about 40 px to the canvases; Blender uses one top row too. |
| Panels in a fixed arrangement | Blender areas: every area has an editor-type menu, and 4 px splitters resize them | RmlUi has no docking. A split tree is simpler than ImGui docking and makes Navigator and Focus presets plain data. |
| The Inspector as one long list of components | Properties context tabs (Scene, Object, Physics, Material, Gameplay, Behaviors, Node) with property search across all tabs | The Player prototype has 15 components; tabs keep each view short. |
| Overrides not shown | Teal marks on overridden fields, a count on each panel, Reset to Prototype and Apply to Prototype | Makes prototype versus instance values visible and reversible. |
| Number fields that only accept typing | Scrub, click to type, expressions, step arrows, one modifier rule, and hover copy/paste | These Blender and DialKit interactions are what makes a property editor feel fast. |
| A fixture Movement diagram | The real Movement graph: typed sockets, category headers, component bindings, inline defaults, connecting, deleting, and Add Node via search | Evaluates density against actual authored content. |
| Separate Quick Open | One command palette with groups, ranking and pages | A single interaction learned once. |
| Status text | Key hints for the area under the pointer, plus the last operation | Blender's discoverability without tutorial copy. |
| Transform only through the inspector or dragging | G/R modal transforms with X/Y constraints, Ctrl snapping, Esc cancel and live deltas | Standard in Blender and needed for fast scene layout. |

## Layout model

The workspace is a split tree of areas. A leaf holds an editor type and that editor's view state; a split holds an axis and a ratio. Drawers are a collapsible leaf at the bottom of the main column. Presets swap the tree without touching documents, selection, history or playback. The hovered area receives editor shortcuts; <kbd>Ctrl Space</kbd> maximizes it. This replaces the `DockBuilder` layout and the `[Chibi][Workspace]` dock IDs. Persisted state becomes the serialized tree.

## Interaction rules

- Keys act on the area under the pointer (Blender). Global keys: <kbd>⌘K</kbd>, <kbd>⌘P</kbd>, <kbd>⌘S</kbd>, <kbd>⌘Z</kbd>, <kbd>Space</kbd> play or pause, <kbd>F2</kbd>, <kbd>F10</kbd>.
- One modifier rule for numbers: <kbd>Alt</kbd> for fine steps, <kbd>Shift</kbd> for coarse steps, <kbd>Ctrl</kbd> to snap. This is Base UI's rule; Blender uses Shift for precision, and we accept that difference for consistency with keyboard stepping.
- Every gesture commits once: a scrub, a picker session or a drag is one undo step. Live updates never write history.
- While the scene is playing, properties show live values and are read-only.
- Motion follows the previous study: press 110 ms, popovers 140 ms, drawers 200 ms in and 100 ms out. Keyboard-driven changes are instant, and Reduce Motion removes transitions.

## RmlUi port contract

The CSS uses only what RmlUi 6.3 supports: flexbox, `gap`, absolute positioning, `var()`, border radius, solid fills, transforms and transitions. It uses no grid, `calc()`, pseudo-elements, `outline`, `!important` or shadows. State selectors use the same `data-*` attributes. Porting a rule means:

- `px` becomes `dp`.
- `ms` durations become seconds, and `cubic-bezier(.23,1,.32,1)` becomes `cubic-out`.
- `border: 1px solid c` becomes `border: 1dp c`.
- `:root` becomes `body`.
- Reduced motion becomes `@media (theme: reduced-motion)`.
- `font-variant-numeric` becomes a tabular-digit Inter face.

Required native work:

1. **Host.** A full-window RmlUi document per workspace, with no ImGui container windows. Popups go in one overlay layer. The backend needs `SetMouseCursor` for scrubbing and splitters, relative-mouse mode for pointer lock, and the Symbols font loaded as a fallback face.
2. **Controllers.** C++ controllers implement the kit contracts: Layers/Positioner, Menu/Menubar/ContextMenu, Tooltip, NumberField, Select, ToggleGroup, Checkbox, Collapsible, Command (a `command-score` port), Splitter and Tree. They mutate only view state and emit C actions.
3. **Custom elements.** `<viewport>` shows the scene render target as a callback texture. `<graph-wires>` emits Bezier geometry. The color plane uses the vertex-gradient decorators, which need no shaders.
4. **Model boundary.** Selection, values, overrides, history, dirty state and playback stay in the C authoring layer. The view syncs from it and never owns it.

## Migration plan

Progress shows in the web build under `?design-preview=1`, where converted surfaces replace their ImGui counterparts.

| Phase | Replaces | Result |
| --- | --- | --- |
| 0 · Foundation | ImGui window hosting for RmlUi, and the ImGui input path in `retained_menu.c` | A full-window document, input and focus routing, text input and IME, cursors, icons, overlay layer, the kit RCSS and controllers |
| 1 · Shell | Menubar, document bar, activity rail, docking, Quick Open, toolbar | Top bar, area tree with splitters and editor types, status bar, command palette |
| 2 · Panels | Navigator (files, catalog, hierarchy, outline), all Inspector variants, Output Log, Compiler Results | Outliner, Properties with reflected controls, Assets, Output |
| 3 · Scene | Scene pane chrome and overlays | `<viewport>` element, tool shelf, navigation gizmo, modal transforms, asset drop |
| 4 · Graph | Node Canvas widgets and draw lists | RML nodes, wire element, inline controls, add-node search, bindings, comments, reroutes, debug and toast |
| 5 · Remainder | Performance, shader details, ramp and image editors, input bindings, runtime widgets and the pause menu | RmlUi editors; game-facing widgets get their own document |
| 6 · Removal | dcimgui, the Cute ImGui patches, and `ImVec`/`ImDrawList` in editor internals | Tests inject RmlUi input and locate stable element ids |

## Progress, September 23, 2026

`?design-preview=1` (or `--design-preview` on desktop) now runs Studio in place of the ImGui workspace. The code is in `src/editor/studio` (shell, areas, editors, host), `src/editor/ui/kit.h` (the C API) and `src/editor/rmlui/kit.cpp` (reconciler and controllers). `build/test_suite --rmlui-kit` covers clicks, scrubbing, text entry with expressions, stepping, context clicks, menus, command ranking and keyed reconciliation.

| Phase | State |
| --- | --- |
| 0 · Foundation | Done, except pointer lock. The full-window document replays SDL events in order, so clicks and chords shorter than a frame are kept. IME follows the focused field. Cursors, the Inter and Symbols faces and the overlay layer are in place. Motion, toasts, lists, drag and drop, charts and highlighting are kit primitives (see [Editor UI references](../../editor-ui-references.md)). |
| 1 · Shell | These work: the top bar, areas with editor-type menus and splitters, status-bar hints, the command palette (commands, objects, assets), the runtime target menu, and settings for theme, density and motion. Preferences and the area layout persist. Splitting and joining areas and presets remain. |
| 2 · Panels | The Outliner is virtualized, with filter, add, a context menu, arrow-key selection and drop targets. Properties has context tabs, identity and lineage, reflected fields, override marks, Reset to Prototype, behaviors, scene physics and prototype documents. Assets lists every indexed asset, and its prototypes and behaviors can be dragged. Output has Log, Problems and Info views with highlighting and follow-output. |
| 3 · Scene | The engine canvas is shown through the `kit:viewport` texture, and selection, dragging and navigation use host input. The tool shelf, the zoom readout (NumberFlow) and asset drop are done. Modal G/R and the gizmo remain. |
| 4 · Graph | In progress, following the [Node editor references](../../editor-graph-references.md) contract. The Behavior area hosts the graph canvas: a second RmlUi context whose dp ratio is the zoom, shown through the `kit:graph` texture (`src/editor/studio/studio_graph.c`, `graph.rml`, `kit_wires.cpp`).<br>Done:<ul><li>Pan and cursor-anchored zoom, mouse vs trackpad detection, framing, and a per-graph camera.</li><li>Marquee, move, and auto-pan at the edges.</li><li>Wire dragging with snapping, pick-up and drop on a node body. A drop on empty canvas opens a type-ranked add-node search and wires the new node in place.</li><li>Inline values, and binding and inline-source chips.</li><li>Node, wire and comment menus.</li><li>Comment frames (C), call navigation, breakpoints (F9), copy and paste.</li><li>Arrange (⇧L), level of detail, dead-code dimming and selection-following wires.</li><li>Run heat from the debug target, a debug badge, and a wire style setting.</li><li>A Node tab in Properties with every pin and expression editing.</li><li>Collapse to Function (Ctrl+G), Convert to Expression, and New Function or Macro.</li></ul>Remaining: reroute editing, knife and insert-on-wire, split pins, renaming graphs, pin watches, and the ImGui canvas's removal. |
| 5 · Remainder | Performance is converted: live frame and simulation charts. Shader details, ramp and image editors, input bindings, runtime widgets and the pause menu remain. |

Behavior changed from the ImGui editor:

- A scrub, a typed entry or a picker session is one undo step.
- Undo, Redo, Save and the dirty mark follow the active document. A prototype document has its own history.
- Override marks and a field context menu with Reset to Prototype replace the per-field revert buttons.
- Output, Assets and Performance open in a drawer at the bottom of the main column, from the status bar, the View menu, ⇧⌘A and ⇧⌘U. The drawer eases open in 200 ms and closed in 100 ms, and its height persists.
- Splitters, scrubbed fields and text inputs show resize and text cursors.
- Theme (System, Dark or Light), density (Compact or Comfortable) and Reduce Motion are editor settings in the top bar's gear menu. They persist, with the area layout, in `studio_prefs`.
- Delete shows a toast with Undo for six seconds, while it is still the latest edit. Save reports success in a toast; a failure stays until it is closed.
- Status-bar reports fade after three seconds. The full operation history is in Output ▸ Info.
- The transport target is a menu: the scene, or a preview of the open or selected prototype. Its label morphs when the target changes.
- The Outliner root collapses, and Up and Down step the selection while the pointer is over it.
- Prototypes dragged from Assets land where they are dropped in the Scene; behaviors dropped on an Outliner row attach to that object.
- The graph editor owns Undo, Redo and Save while its area has focus. Space and Shift D there no longer play the scene or duplicate objects.
- A node spawned from a dragged wire lands with the wired socket at the drop point. The search lists exact type matches first, then conversions, then wildcards.
- Nodes that no event reaches, or whose values nothing reads, are dimmed. Wires attached to the selection or the hovered node stand out, and the rest recede.
- Scrubbing a node's inline value is one undo step.
- Saving a scene keeps its file name as the document name. It used to write "Scene".

## Decisions

1. Density is an editor setting. Compact is the default.
2. Areas (Blender splits) replace floating docking.
3. Open: Properties categories still come from a table in `studio_scene.c` instead of reflection metadata.
4. Open: orange selection in the viewport and graph, blue in lists.
