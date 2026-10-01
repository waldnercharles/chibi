# Studio: design direction for the RmlUi editor

This study refines the approved **A · Studio** layout from the `cwaldner/editor-design-prototype` branch. It uses real Neon Swarm content: `fixture.js` is a snapshot of `projects/arena2d` (the Sandbox scene, 24 prototypes and 14 behaviors). The [component kit](kit.html) is the specification for the RmlUi port. Each control there lists its behavior contract, states and keys.

## Visual language

Studio is a daily desktop authoring tool. Its design dials are **variance 2, motion 2, density 6**. The foundation is the existing RmlUi kit and authored Studio assets. Marketing layouts, cinematic scroll effects, decorative glass and randomized compositions do not fit this workspace.

- **Typography:** embedded Geist at 13 dp with 18 dp leading, 500-weight control labels and 600-weight titles. GeistNumeric supplies tabular numerals. JetBrains Mono is reserved for source, paths and expressions. The font and its measurements are identical on desktop and web.
- **Color:** the default light and dark palettes use cool neutrals with blue for selection, focus and primary actions. Named palettes keep their base, text and accent colors while sharing the same component roles. Status, overrides, axes, syntax and graph categories use semantic colors where those colors carry meaning. Helper text uses the same readable contrast floor as secondary labels.
- **Structure:** quiet chrome, clear content, solid fields and overlays. A global toolbar sits above a document tab row, with an underline identifying the active document. Secondary toolbar labels collapse to icons at narrow widths; their tooltips keep names and shortcuts available. Separate areas with a soft line, and use a bordered group only for related editable fields. Controls have 4 dp corners; panels and overlays have 6 dp corners.
- **Density:** compact controls and virtualized rows are 26 dp; comfortable controls and rows are 30 dp. Headers and the document tab row are 32/36 dp, the toolbar is 40/44 dp and the status bar is 28/30 dp. The defaults live in `theme/roles.rcss`; font preferences scale the shell metrics proportionally, and native lists read `--row` through `kit_metric`, so changing an authored metric also changes their virtualization geometry. The graph retains its fixed 22 dp socket pitch in its own context.
- **Feedback:** pointer press is immediate. Focus is visible, text and icons align at the control center, and selected controls share a soft accent fill. Search appears immediately. Occasional popovers fade for 140 ms in and 100 ms out without shifting their anchor. Dragged controls track the pointer directly and hand release velocity to an existing spring. Reduce Motion makes changes immediate.
- **Ownership:** RML owns component anatomy, RCSS owns visual choices and layout, and authored behaviors own local interaction that the behavior system can express. C owns authoring data, reflection, virtualized native content and transport glue. New UI must reuse an authored component or a kit primitive before introducing a new presentation path.

| Before | After | Why |
| --- | --- | --- |
| 12 dp text, 22 dp controls, a 30 dp top bar | 13 dp text, 26 dp controls, a 40 dp top bar | Gives labels, icons and controls enough room at normal display scale. |
| Helper text dimmed by mixing it into the background | Secondary and helper labels share readable color roles | Small type needs contrast to remain useful. |
| Native lists repeat density numbers in C | List pitch comes from authored RCSS | The rendered row and the scroll model agree when density changes. |
| Browser collaboration form covers the workspace | Collaboration opens on demand in an authored Studio dialog | Preserves workspace space and uses the same controls, focus and theme. |
| Search shifts while opening | Search appears immediately | Frequent keyboard actions should respond at once. |

### Theme presets

Settings → Appearance → Theme uses the authored Select component. System, Dark and Light remain available. Named choices include Dracula; Solarized Light and Dark; Tokyo Night, Storm and Day; Catppuccin Latte, Frappé, Macchiato and Mocha; Rosé Pine, Moon and Dawn; and Gruvbox Light and Dark.

`theme/presets.rcss` owns their palette values. A single preference applies to the shell, mounted components and the graph's separate rendering context, and persists with the other editor preferences. The standalone StudioShell RML binds the same classes when previewed as an authored document. Switching removes the previous preset class and suppresses transitions during the palette change. Fonts, density, layout and control behavior are shared across presets.

Palette sources are [Dracula](https://draculatheme.com/contribute), [Solarized](https://ethanschoonover.com/solarized/), [Tokyo Night](https://github.com/folke/tokyonight.nvim), [Catppuccin](https://catppuccin.com/palette/), [Rosé Pine](https://rosepinetheme.com/palette/) and [Gruvbox](https://github.com/morhetz/gruvbox). Secondary UI text and status colors are adapted where needed to keep small labels legible on control and overlay surfaces.

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
| Menus, documents, playback and search share one crowded row | A 40 dp toolbar sits above a 32 dp document tab row | Keeps actions in reach and gives document names room at narrow window widths. |
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

Two layout modes ship, selected from the top bar or View ▸ Layout Mode. Vertical Split stacks Scene over Behavior. Horizontal Split sets the Canvas beside the Text Editor, with Hierarchy over Details on the right. Each remembers its editors, splits, drawer and side pane. Opening a source file or changing tabs keeps the selected mode. Reset Layout resets the active mode only. The top bar's side-pane toggle and View ▸ Hide Side Pane collapse the right column at its last width, and showing one of its editors opens it again. The modes are fixed; there is no layout editor.

## Interaction rules

- Keys act on the area under the pointer (Blender). Global keys: <kbd>⌘K</kbd>, <kbd>⌘P</kbd>, <kbd>⌘S</kbd>, <kbd>⌘Z</kbd>, <kbd>⌘=</kbd> <kbd>⌘-</kbd> <kbd>⌘0</kbd> UI Scale, <kbd>Space</kbd> play or pause, <kbd>F2</kbd>, <kbd>F10</kbd>.
- One modifier rule for numbers: <kbd>Alt</kbd> for fine steps, <kbd>Shift</kbd> for coarse steps, <kbd>Ctrl</kbd> to snap. This is Base UI's rule; Blender uses Shift for precision, and we accept that difference for consistency with keyboard stepping.
- Every gesture commits once: a scrub, a picker session or a drag is one undo step. Live updates never write history.
- While the scene is playing, properties show live values and are read-only.
- Motion is restrained: immediate press feedback, popovers 140 ms in and 100 ms out, drawers 200 ms in and 100 ms out. Search and keyboard-driven changes are instant, and Reduce Motion removes transitions.

## RmlUi port contract

The CSS uses only what RmlUi 6.3 supports: flexbox, `gap`, absolute positioning, `var()`, border radius, solid fills, transforms and transitions. It uses no grid, `calc()`, pseudo-elements, `outline`, `!important` or shadows. State selectors use the same `data-*` attributes. Porting a rule means:

- `px` becomes `dp`.
- `ms` durations become seconds, and `cubic-bezier(.23,1,.32,1)` becomes `cubic-out`.
- `border: 1px solid c` becomes `border: 1dp c`.
- `:root` becomes `body`.
- Reduced motion becomes `@media (theme: reduced-motion)`.
- `font-variant-numeric` becomes the embedded GeistNumeric face.

Required native work:

1. **Host.** A full-window RmlUi document per workspace, with no ImGui container windows. Popups go in one overlay layer. The backend needs `SetMouseCursor` for scrubbing and splitters, relative-mouse mode for pointer lock, and the Symbols font loaded as a fallback face.
2. **Controllers.** C controllers implement the kit contracts: Layers/Positioner, Menu/Menubar/ContextMenu, Tooltip, NumberField, Select, ToggleGroup, Checkbox, Collapsible, Command (a `command-score` port), Splitter and Tree. They mutate only view state and emit C actions.
3. **Custom elements.** `<viewport>` shows the scene render target as a callback texture. `<graph-wires>` emits Bezier geometry. The color plane uses the vertex-gradient decorators, which need no shaders. `<number-flow>` and `<morph-text>`, with `presence` on `data-if` elements, carry NumberFlow, Torph and Motion's presence into authored UI, the editor's and games' (`src/ui/motion.h`).
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
| 0 · Foundation | Done, except pointer lock. The full-window document replays SDL events in order, so clicks and chords shorter than a frame are kept. IME follows the focused field. Cursors, the Geist, GeistNumeric and Symbols faces and the overlay layer are in place. Motion, toasts, lists, drag and drop, charts and highlighting are kit primitives (see [Editor UI references](../../editor-ui-references.md)). |
| 1 · Shell | These work: the top bar, areas with editor-type menus and splitters, Horizontal Split and Vertical Split modes that stay selected across tab changes, status-bar hints, the command palette (commands, settings, objects, assets, projects and the active document's symbols; recent items first; `>` for commands and settings, `@` for symbols, `?` for the prefixes), the runtime target menu, and settings for theme, density and motion. Preferences and each mode's arrangement persist. Splitting and joining areas remain. |
| 2 · Panels | The Outliner is virtualized, with filter, add, a context menu, arrow-key selection and drop targets. Properties has context tabs, identity and lineage, reflected fields, override marks, Reset to Prototype, behaviors, scene physics and prototype documents. Assets lists every indexed asset, and its prototypes and behaviors can be dragged. Its New menu, and a New … command per kind, makes an asset of any kind it lists (`assets/new_asset.h`, which the MCP's create requests share) and opens it. Output has Log, Problems, Runtime and Info views with highlighting and follow-output; problems and runtime errors open their node. Problems covers the open asset or, with Project, every graph asset. |
| 3 · Scene | The engine canvas is shown through the `kit:viewport` texture, and selection, dragging and navigation use host input. The tool shelf, the zoom readout (NumberFlow) and asset drop are done. Modal G/R and the gizmo remain. |
| 4 · Graph | In progress, following the [Node editor references](../../editor-graph-references.md) contract. The Behavior area hosts the graph canvas: a second RmlUi context whose dp ratio is the zoom, shown through the `kit:graph` texture (`src/editor/studio/studio_graph*.c`, `graph.rml`, `kit_wires.c`).<br>Done:<ul><li>Pan and cursor-anchored zoom, mouse vs trackpad detection, framing, and a per-graph camera.</li><li>Marquee, move, and auto-pan at the edges.</li><li>Wire dragging with snapping, pick-up and drop on a node body. A drop on empty canvas opens a type-ranked add-node search and wires the new node in place.</li><li>Inline values, and binding and inline-source chips.</li><li>Node, wire and comment menus.</li><li>Comment frames (C), call navigation, breakpoints (F9), copy and paste.</li><li>Arrange (⇧L), level of detail, dead-code dimming and selection-following wires.</li><li>Run heat from the debug target, a debug badge, and a wire style setting.</li><li>A Node tab in Properties with every pin and expression editing.</li><li>Collapse to Function (Ctrl+G), Convert to Expression, and New Function or Macro.</li><li>Node settings for short bindings, flow pins in titles and compact nodes; expressions shown as code; square container sockets; light and dark canvases.</li></ul><ul><li>Reroutes: double-click a wire to add one, drag to move it, Alt+click to remove it.</li><li>Dropping a lone unwired node on a wire inserts it.</li><li>Watch Values in the node menu, with value bubbles beside watched and hovered pins while a run is inspected.</li><li>Renaming and deleting functions and macros from the graph picker.</li><li>A pin menu, adding and removing pins on variadic nodes, splitting and recombining struct pins, alignment and Straighten (Q), key-and-click placing (B, S, D, F, N, O, G), and paste at the pointer.</li><li>Members: macro inputs and outputs, including Exec; containers and input defaults for functions and macros, whose inputs and outputs reorder by dragging their grips; Undo toasts for deleted members.</li><li>Comment frames rename in place, resize from their edges and corners and take a color; nodes carry comment bubbles.</li><li>Problem badges on nodes, live compile, and the debugger: Resume, Step Over, Step Into, Step Out and the call stack.</li><li>Find in graph (⌘F), Find References in Output's Find view, Go to Definition (⌥G), and Call Hierarchy in Output's Calls view (⇧⌥H); Members count each member's uses.</li><li>Hovering a node's title describes the node, and add-node results carry the same sentence.</li><li>Breadcrumbs of the behavior and the calls entered, and Back and Forward through visited graphs.</li><li>Select linked (L, ⌥L, and all input or output nodes), Collapse to Macro, Expand Node and Refresh Nodes.</li><li>Comments over several lines, and a selected comment frame's title and color in the Node tab.</li></ul>Remaining: the knife. |
| 5 · Remainder | Performance is converted: live frame and simulation charts. Runtime widgets and the pause menu are GameUi documents drawn over the game canvas. Shader details, ramp and image editors, and input bindings remain. |

Behavior changed from the ImGui editor:

- A scrub, a typed entry or a picker session is one undo step.
- Undo, Redo, Save and the dirty mark follow the active document. A prototype document has its own history.
- Override marks and a field context menu with Reset to Prototype replace the per-field revert buttons.
- Output, Assets and Performance open in a drawer at the bottom of the main column, from the status bar, the View menu, ⇧⌘A and ⇧⌘U. The drawer eases open in 200 ms and closed in 100 ms, and its height persists.
- Splitters, scrubbed fields and text inputs show resize and text cursors.
- Preferences live on a Settings page (the gear, Mod+, or the `settings` command), as VS Code's settings editor: Editor and Project tabs, a category sidebar (Appearance, Graph, Text Editor, Keyboard), search across labels, descriptions and keywords, and a row per setting with its description, control, a modified mark and Reset. Keyboard records command shortcuts, shows their area contexts and overlapping assignments, and resets overrides to the defaults; Project sets the project name, start scene and default root document in `project.yaml`. Every preference is a row of one table (`studio_settings[]`, `studio_settings.c`) that the preferences file, the saved keys panels bind and the page share; they persist, with the area layout, in `studio_prefs`.
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
- A field that names an asset, such as `GameUi.document` or the Scene tab's root document, is a select that searches the project's assets of its kind on the command surface, with None and, where a name may be missing, the typed name. A name no asset has shows as invalid.
- A game UI's error shows in the Scene's notice line and heads Output's Problems; Output records each new error once.
- While a ui asset's document is active, the Scene area is its Canvas: the asset alone in the Scene area over an empty world, as Storybook shows a story: at a screen size (Fit, Phone, Tablet, Desktop), on a light or dark backdrop, magnified by the view zoom, with an Actions log of the events it emits and what uses it. Its events reach no behavior. Preview UI Asset… in the command palette, Preview in an asset's Assets menu and the Canvas's crumb show any ui asset the same way.
- The other areas follow a ui asset's document too, as Unity's UI Builder and Unreal's UMG Designer arrange theirs (`studio_designer*.c`):
  - The Outliner is its Hierarchy: each element as `tag#id.class`, a `<use>` by its widget, with its only text. Rows select, fold (Left and Right), filter and walk (Up and Down). Dragged, a row drops before or after another in its top and bottom quarters and inside it in its middle. Its menu adds, duplicates (⇧D), wraps in a div, opens the source or a widget, and deletes (X), with Undo in a toast.
  - Properties is its Details: the element's text, widget, id, classes as chips, attributes and bindings (in code), its inline style, and the rules of the document's RCSS that match it, each value in the control its property calls for (lengths scrub with their unit, colors pick, keywords choose, the rest is text). With nothing selected it lists every rule.
  - Assets is its Library: elements and the project's widgets as tiles, dragged into the Hierarchy or the Canvas, or clicked to add to the selection.
  - The Canvas outlines the element under the pointer and the selection, labeled with its size, and a click selects there.

  Selecting in any of them, or moving the Text Editor's cursor, selects in all, and the Text Editor's cursor moves to the selection. Every change is one text edit of the document and one undo step; a scrub or a color session is one step.
- The Add menu offers the events that the GameUi documents on whatever attaches the open behavior send, described by what the player does ("Sent when the player clicks #extract in Bank").
- Objects rename in place, as in Blender and Unity: F2 over the Outliner or the Scene, a double-click on an Outliner row, Rename in the row's menu or the Object menu, or the name at the top of Properties' Object tab, which is a field. Enter or leaving the field renames as one Undo step and Escape keeps the name; a name another object has is refused with a toast. Nothing references an object by its name, so nothing else changes. F still frames a row.
- A prototype's Properties ends with Add Component, which searches the components it lacks on the command surface, grouped by the tab each shows in (Unity's Add Component); the added component's panel opens. A component panel's menu, the header's ⋯ while hovered or a right press on the header (Unity's component menu, Blender's modifier menu), removes it with Undo in a toast. Each is one step of the prototype's Undo, and its instances follow; an instance's values for a removed component come back with it.
- Play in the Studio project runs Studio itself, since the editor is that project's game: a guest Studio fills the Scene area, built from the project's working copy (unsaved text included) and editing the same scene authoring. It keeps its own preferences while it runs, takes the pointer over it and the keys while it has them (a press in it, or the pointer over it while no host field has the keyboard), and has no Play or Open Project of its own. Play again or Stop ends it, as Unity's Play toggles.
- Every document's tab but a scene's closes, asking before it discards unsaved edits; a closed graph leaves the Behavior area.
- The Text Editor finds (<kbd>⌘F</kbd>, VS Code's find widget: Enter and Down for the next match, Shift+Enter and Up for the previous, Esc back to the text), types over a selection, and goes to the start and end of the text with <kbd>⌘↑</kbd> and <kbd>⌘↓</kbd> (<kbd>Ctrl+Home</kbd> and <kbd>Ctrl+End</kbd> elsewhere). Horizontal Split holds the Text Editor beside the Canvas when selected.
- Saving a UI document formats RCSS with two-space indentation and one declaration per line. RML only splits long attribute lists; text, comments, bindings and every byte between tags stay as written. Invalid parts stay byte-for-byte unchanged, and formatting all saved parts is one Undo step.
- A graph opens fitted while that stays legible, else at 75% with its events in view; Frame All still fits it all.
- The Scene shows View Zoom in its header only while it magnifies; its levels are in the View menu, and the camera's zoom is the corner readout.

## Decisions

1. Density is an editor setting. Compact is the default. UI Scale (50–200%) sizes the whole editor, as Blender's Resolution Scale and Unreal's Application Scale do; games' UI keeps its own scale, in the Scene view and in Play. It steps as browsers and VS Code zoom (<kbd>⌘=</kbd>, <kbd>⌘-</kbd>, <kbd>⌘0</kbd>, View › UI Scale) and its percentage stays in the status bar, as Office keeps its zoom there. The Scene's view zoom is separate from its camera: the wheel moves the camera through the world, while <kbd>⌘</kbd> or <kbd>Ctrl</kbd> with the wheel, or the header's percentage, magnifies the rendered frame with its game UI, as Unity's Game view Scale does; the middle button pans it while magnified. The frame renders sharper up to 200% and magnifies its pixels beyond. An idle editor draws at most four frames a second, as Blender redraws only on events: input, a running scene, a frame that draws anything the frame before it did not (so animations need no registration), or RmlUi's next update (a caret blink) bring back full rate, and a write that leaves a style unchanged lays nothing out.
2. Areas (Blender splits) replace floating docking.
3. Properties tabs, prototype icons, and number ranges and units are authored per project in `content/editor/Components.yaml` (`src/editor/documents/component_presentation.h`). Studio re-reads the file within a second of a change. An invalid edit keeps the last valid file and reports the error; without the file, components show under Object with no icon and plain numbers.
4. Open: orange selection in the viewport and graph, blue in lists.
5. Studio's own UI is moving to authored assets, built the way games build theirs. `projects/studio` holds the editor's ui assets, widgets and a `Studio` scene whose GameUi entities are panels. Studio runs that scene as a small world and mounts a panel's document in a kit host (`kit_host`, `src/editor/studio/studio_ui.c`), where Studio's stylesheets and theme variables apply. Its RCSS is scoped to the panel. Preferences are that world's saved keys, so a binding such as `data-checked="save.theme"` edits them. The Behavior header's view popover, the Settings page, the status bar, the top bar and the area headers are authored this way. The status bar (`StatusBar`, with a `KeyHint` per hint) binds Studio's `editor` variable, and its drawer toggles run commands with `emit('studio.toggleAssets')` and read them from Studio's `commands` variable (each command's label, shortcut, icon, enabled and checked); UI Scale and its menu stay native in the bar's `#ui-scale` element, a native slot (`kit_slot_begin`), as Unity's IMGUIContainer hosts IMGUI in UI Toolkit, and its authored controls retain keyboard focus. The top bar (`TopBar`) lists `editor.tabs`, whose faces and close buttons run `emit('studio.selectTab', i)` and `emit('studio.closeTab', i)` with the tab's index, and binds the transport, the gear and Search to `commands`; its `#menus` lists the menubar's menus as markup (`<menu label>` holding `<li command="id">`, `<li list>`, `<li label>` headings, `<hr/>` and submenus), which the native menubar draws in `#menubar`, and the play target's menu and a paused run's debugger steps stay native in its slots. Each area shows its own `AreaHeader`, which binds its item of `editor.areas` as `area` (`data-alias-area`, as a widget's props): Maximize, faint until hovered and Restore while its area fills the workspace, runs `emit('studio.maximize', area.index)`, and the drawer's Close `closeDrawer`; the editor type menu and the editor's own controls stay native in its `#editor-type` and `#controls` slots, where a field keeps the keyboard. The popover's keyboard handling is a `MenuKeys` behavior (`data-keys` events, Move UI Focus and Click UI Element). The Settings page binds Studio's `settings` variable (`studio_sources.c`) through reusable widgets games can use too (Switch, Select, Slider, Segmented, SidebarNav and SettingRow); its controls take focus natively, with RmlUi's `nav` properties moving between them, a `NavKeys` behavior makes the sidebar's selection follow Up and Down, and a `Dismiss` behavior hides the page's GameUi on Escape, the close button or a backdrop click. Panels start with their GameUi hidden, so opening the Studio project never draws them over its scene view; selecting one there shows it while the scene is edited (Play runs a guest Studio, as above), a prototype preview shows it, and Studio's popover hosts show them regardless, and a modal panel shows while its entity's GameUi is not hidden, in a `kit_modal_host` above the editor, and global shortcuts remain available from non-text chrome buttons and document tabs while text fields, open popups and modals keep their keyboard ownership. Editing a panel's assets or behaviors applies within a second, and an invalid edit keeps the last valid version.
6. Every panel of the Studio scene is also an editor type, listed after the built-in editors in an area's editor menu, the way Blender lists Preferences. An area shows its panel whether or not its GameUi is hidden. Data lists the saved keys of the world the Scene shows, and Theme lists the custom properties of the stylesheets the open scene's root document imports, a section per file and rule; both edit in place (Enter or blur commits), so with the Studio project open they change the editor's preferences and look.
7. The UI Inspector inspects every UI surface Studio draws, as Chrome DevTools, Unity's UI Toolkit Debugger and Unreal's Widget Reflector do: its chrome, its authored panels, the graph canvas, and a game's UI in the Scene at any view zoom. <kbd>⇧⌘C</kbd>, Inspect Element in the palette, or the panel's Pick starts picking. The element under the pointer is highlighted in DevTools' box-model colors, a click selects it, and <kbd>Esc</kbd> ends picking. The Inspector is itself a panel of the Studio scene, so it can inspect itself. It opens in the drawer when no area shows it, and shows:
   - the Elements tree, with the widget each element shows;
   - Styles by rule, with each rule's file and line;
   - Computed values and Attributes;
   - Force state;
   - Edit Source, which opens the ui asset at its line in the Text Editor.

   Kit chrome reads Native (C), with its kit path.
