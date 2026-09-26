# Studio: design direction for the RmlUi editor

This study refines the approved **A · Studio** layout from the `cwaldner/editor-design-prototype` branch. It uses real Neon Swarm content: `fixture.js` is a snapshot of `projects/arena2d` (the Sandbox scene, 24 prototypes and 14 behaviors). The [component kit](kit.html) is the specification for the RmlUi port. Each control there lists its behavior contract, states and keys.

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

- Keys act on the area under the pointer (Blender). Global keys: <kbd>⌘K</kbd>, <kbd>⌘P</kbd>, <kbd>⌘S</kbd>, <kbd>⌘Z</kbd>, <kbd>⌘=</kbd> <kbd>⌘-</kbd> <kbd>⌘0</kbd> UI Scale, <kbd>Space</kbd> play or pause, <kbd>F2</kbd>, <kbd>F10</kbd>.
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

Studio is the only editor, on desktop and in the web build; the ImGui workspace is gone.

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

The code is in `src/editor/studio` (shell, areas, editors, host), `src/editor/ui/kit.h` (the C API) and `src/editor/rmlui` (the reconciler in `kit.c`, controllers in the `kit_*.c` modules). `build/test_suite --rmlui-kit` covers clicks, scrubbing, text entry with expressions, stepping, context clicks, menus, command ranking and keyed reconciliation.

| Phase | State |
| --- | --- |
| 0 · Foundation | Done, except pointer lock. The full-window document replays SDL events in order, so clicks and chords shorter than a frame are kept. IME follows the focused field. Cursors, the Inter and Symbols faces and the overlay layer are in place. Motion, toasts, lists, drag and drop, charts and highlighting are kit primitives (see [Editor UI references](../../editor-ui-references.md)). |
| 1 · Shell | These work: the top bar, areas with editor-type menus and splitters, status-bar hints, the command palette (commands, objects, assets), the runtime target menu, and settings for theme, density and motion. Preferences and the area layout persist. Splitting and joining areas and presets remain. |
| 2 · Panels | The Outliner is virtualized, with filter, add, a context menu, arrow-key selection and drop targets. Properties has context tabs, identity and lineage, reflected fields, override marks, Reset to Prototype, behaviors, scene physics and prototype documents. Assets lists every indexed asset, and its prototypes and behaviors can be dragged. Output has Log, Problems, Runtime and Info views with highlighting and follow-output; problems and runtime errors open their node. Problems covers the open asset or, with Project, every graph asset. |
| 3 · Scene | The engine canvas is shown through the `kit:viewport` texture, and selection, dragging and navigation use host input. The tool shelf, the zoom readout (NumberFlow) and asset drop are done. Modal G/R and the gizmo remain. |
| 4 · Graph | In progress, following the [Node editor references](../../editor-graph-references.md) contract. The Behavior area hosts the graph canvas: a second RmlUi context whose dp ratio is the zoom, shown through the `kit:graph` texture (`src/editor/studio/studio_graph*.c`, `graph.rml`, `kit_wires.c`).<br>Done:<ul><li>Pan and cursor-anchored zoom, mouse vs trackpad detection, framing, and a per-graph camera.</li><li>Marquee, move, and auto-pan at the edges.</li><li>Wire dragging with snapping, pick-up and drop on a node body. A drop on empty canvas opens a type-ranked add-node search and wires the new node in place.</li><li>Inline values, and binding and inline-source chips.</li><li>Node, wire and comment menus.</li><li>Comment frames (C), call navigation, breakpoints (F9), copy and paste.</li><li>Arrange (⇧L), level of detail, dead-code dimming and selection-following wires.</li><li>Run heat from the debug target, a debug badge, and a wire style setting.</li><li>A Node tab in Properties with every pin and expression editing.</li><li>Collapse to Function (Ctrl+G), Convert to Expression, and New Function or Macro.</li><li>Node settings for short bindings, flow pins in titles and compact nodes; expressions shown as code; square container sockets; light and dark canvases.</li></ul><ul><li>Reroutes: double-click a wire to add one, drag to move it, Alt+click to remove it.</li><li>Dropping a lone unwired node on a wire inserts it.</li><li>Watch Values in the node menu, with value bubbles beside watched and hovered pins while a run is inspected.</li><li>Renaming and deleting functions and macros from the graph picker.</li><li>A pin menu, adding and removing pins on variadic nodes, splitting and recombining struct pins, alignment and Straighten (Q), key-and-click placing (B, S, D, F, N, O, G), and paste at the pointer.</li><li>Members: macro inputs and outputs, including Exec; containers and input defaults for functions and macros, whose inputs and outputs reorder by dragging their grips; Undo toasts for deleted members.</li><li>Comment frames rename in place, resize from their edges and corners and take a color; nodes carry comment bubbles.</li><li>Problem badges on nodes, live compile, and the debugger: Resume, Step Over, Step Into, Step Out and the call stack.</li><li>Find in graph (⌘F), Find References in Output's Find view, Go to Definition (⌥G), and Call Hierarchy in Output's Calls view (⇧⌥H); Members count each member's uses.</li><li>Hovering a node's title describes the node, and add-node results carry the same sentence.</li><li>Breadcrumbs of the behavior and the calls entered, and Back and Forward through visited graphs.</li><li>Select linked (L, ⌥L, and all input or output nodes), Collapse to Macro, Expand Node and Refresh Nodes.</li><li>Comments over several lines, and a selected comment frame's title and color in the Node tab.</li></ul>Remaining: the knife. |
| 5 · Remainder | Performance is converted: live frame and simulation charts. Runtime widgets and the pause menu are GameUi documents drawn over the game canvas. Shader details, ramp and image editors, and input bindings remain. |

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
- Shortcuts follow the platform, including the web build on a Mac: Command is the primary modifier, chords are written ⇧⌘Z, and Command edits text in fields.
- Scrolling in the graph zooms, from a mouse or a trackpad.
- The graph editor owns Undo, Redo and Save while its area has focus. Space and Shift D there no longer play the scene or duplicate objects.
- A node spawned from a dragged wire lands with the wired socket at the drop point. The search lists exact type matches first, then conversions, then wildcards.
- Nodes that no event reaches, or whose values nothing reads, are dimmed. Wires attached to the selection or the hovered node stand out, and the rest recede.
- Scrubbing a node's inline value is one undo step.
- Saving a scene keeps its file name as the document name. It used to write "Scene".

## Decisions

1. Density is an editor setting. Compact is the default. UI Scale (50–200%) sizes the whole editor, as Blender's Resolution Scale and Unreal's Application Scale do; games' UI keeps its own scale, in the Scene view and in Play. It steps as browsers and VS Code zoom (<kbd>⌘=</kbd>, <kbd>⌘-</kbd>, <kbd>⌘0</kbd>, View › UI Scale) and its percentage stays in the status bar, as Office keeps its zoom there. The Scene's view zoom is separate from its camera: the wheel moves the camera through the world, while <kbd>⌘</kbd> or <kbd>Ctrl</kbd> with the wheel, or the header's percentage, magnifies the rendered frame with its game UI, as Unity's Game view Scale does; the middle button pans it while magnified. The frame renders sharper up to 200% and magnifies its pixels beyond. An idle editor draws at most four frames a second, as Blender redraws only on events: input, a running scene, a frame that draws anything the frame before it did not (so animations need no registration), or RmlUi's next update (a caret blink) bring back full rate, and a write that leaves a style unchanged lays nothing out.
2. Areas (Blender splits) replace floating docking.
3. Properties tabs, prototype icons, and number ranges and units are authored per project in `content/editor/Components.yaml` (`src/editor/documents/component_presentation.h`). Studio re-reads the file within a second of a change. An invalid edit keeps the last valid file and reports the error; without the file, components show under Object with no icon and plain numbers.
4. Open: orange selection in the viewport and graph, blue in lists.
5. Studio's own UI is moving to authored assets, built the way games build theirs. `projects/studio` holds the editor's ui assets, widgets and a `Studio` scene whose GameUi entities are panels. Studio runs that scene as a small world and mounts a panel's document in a kit host (`kit_host`, `src/editor/studio/studio_ui.c`), where Studio's stylesheets and theme variables apply. Its RCSS is scoped to the panel. Preferences are that world's saved keys, so a binding such as `data-checked="save.theme"` edits them. The Behavior header's view popover and the Settings dialog (the gear, or Mod+,) are authored this way. Their keyboard handling is a `MenuKeys` behavior (`data-keys` events, Move UI Focus and Click UI Element), and the dialog hides its entity's GameUi through a `Dismiss` behavior on Escape or a backdrop click. Panels start with their GameUi hidden, so opening the Studio project never draws them over its scene view; selecting one there shows it while the scene is edited (Play shows what the game would), a prototype preview shows it, and Studio's popover hosts show them regardless, and a modal panel shows while its entity's GameUi is not hidden, in a `kit_modal_host` above the editor, and Studio's shortcuts stay off while focus is inside hosted content. Editing a panel's assets or behaviors applies within a second, and an invalid edit keeps the last valid version.
6. Every panel of the Studio scene is also an editor type, listed after the built-in editors in an area's editor menu, the way Blender lists Preferences. An area shows its panel whether or not its GameUi is hidden. Data lists the saved keys of the world the Scene shows, and Theme lists the custom properties of the stylesheets the open scene's root document imports, a section per file and rule; both edit in place (Enter or blur commits), so with the Studio project open they change the editor's preferences and look.
7. The UI Inspector inspects every UI surface Studio draws, as Chrome DevTools, Unity's UI Toolkit Debugger and Unreal's Widget Reflector do: its chrome, its authored panels, the graph canvas, and a game's UI in the Scene at any view zoom. <kbd>⇧⌘C</kbd>, Inspect Element in the palette, or the panel's Pick starts picking. The element under the pointer is highlighted in DevTools' box-model colors, a click selects it, and <kbd>Esc</kbd> ends picking. The Inspector is itself a panel of the Studio scene, so it can inspect itself. It opens in the drawer when no area shows it, and shows:
   - the Elements tree, with the widget each element shows;
   - Styles by rule, with each rule's file and line;
   - Computed values and Attributes;
   - Force state;
   - Edit Source, which opens the ui asset at its line in the Text Editor.

   Kit chrome reads Native (C), with its kit path.
