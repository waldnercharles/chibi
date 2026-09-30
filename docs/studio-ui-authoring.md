# Studio: the editor's UI

Studio (`src/editor/studio`) is the editor, on desktop and in the web build. Its UI is moving entirely into the Studio project (`projects/studio`): RML/RCSS documents in `content/ui`, driven by the project's behaviors and its `Studio` scene, the way games build their UI. The goal is that a user authors a UI widget from start to finish inside Studio, and every part of the editor can be inspected and edited from the editor. Behavior-node internals written in C are the only exception.

- **Build new editor UI in `projects/studio`,** not as C panels on the kit (`src/editor/ui/kit.h`, `src/editor/rmlui/kit*.c`).
- **Convert on contact.** When new work needs an existing C panel or kit widget, convert that piece to Studio as part of the work. The Outliner, Properties, Assets, Output, Members, Performance, the UI designer's Hierarchy, Details and Library, the command palette, menus, popups and toasts are still C on the kit.
- **Native C stays for what documents host through slots:** the graph canvas, the Scene viewport and the Text Editor (Zep), whose drawing and input a document can't express.

Before building a UI mechanism, find its problem in [Editor UI references](editor-ui-references.md) and follow the contract recorded there. The graph canvas follows [Node editor references](editor-graph-references.md). [Blender](references/blender/README.md) is the reference for visual density and desktop interaction.

## How it fits together

- `src/ui` owns the RmlUi binding (`rml.h`, a pass-through over `rml.cpp`), FreeType font rendering and the Cute geometry backend, shared by the editor and game UI. RmlUi 6.3 and FreeType are pinned in CMake.
- `src/editor/rmlui` owns the kit, its controllers (`kit_*.c`) and the kit documents `studio.rml` and `graph.rml` in `resources/`, loaded from the source checkout. Their styles are the Studio project's theme (`content/ui/StudioShell.rcss` and its `theme/` imports), so RCSS edits apply at once; restart after editing the kit's RML.
- `src/editor/studio/studio_ui.c` runs the `Studio` scene as a small world. Each entity with a `GameUi` is a panel whose document mounts in a kit host (`kit_host`) the way a game mounts its UI, so widgets, behaviors and data bindings work as in games. Studio's preferences are that world's saved keys.
- Application models, authoring commands and Undo/Redo stay in C. Documents bind C state and emit commands; they never own authoring state.
- Sizes at the C boundary are logical pixels; the RmlUi context uses framebuffer pixels, and RCSS uses `dp`.
- ImGui remains only as the `--play` window host. In-game UI, including the pause menu, is GameUi documents over the game image (`src/render/ui.c`).

## Adding a panel

1. Write `projects/studio/content/ui/<Name>.rml` and `<Name>.rcss`. Reuse the project's widgets through `<use ui="…">`: Switch, Select, Slider, Segmented, SidebarNav, SettingRow, Dialog, KeyHint, MenuOption, MenuCheck, TextField, ColorField and VirtualList. The widget gallery (the Widgets panel) shows them.
2. Add a prototype in `content/prototypes/<Name>.yaml` with `GAME.GameUi: {document: <Name>, hidden: true}` and an entity in `content/scenes/Studio.yaml`. Attach behaviors for keys and dismissal: `MenuKeys`, `NavKeys`, `Dismiss`.
3. Bind Studio's data: `save.<key>` for preferences, `editor` for chrome state, `commands` for each command's state, `settings`, `theme`, `inspector` and `world`, and `self` for what a behavior's Set UI Data gives the entity. Run commands with `emit('studio.<id>')`.
4. Every panel is also an editor type, listed after the built-in editors in an area's editor menu, as Blender lists Preferences. C shows one in an area (`studio_ui_area`), over the editor as a modal while its GameUi is not hidden (`studio_ui_modal`, in a `kit_modal_host`), or in a popover (`studio_ui_panel`).
5. Put native content in a slot (`kit_slot_begin`) rather than drawing around the document.

Panels start with their GameUi hidden, so opening the Studio project never draws them over its scene. Editing a panel's assets or behaviors applies within a second; an invalid edit keeps the last valid version. Studio's shortcuts stay off while focus is inside hosted content.

**RCSS from web references.** Studio's RCSS uses what RmlUi supports: flexbox, `gap`, absolute positioning, `var()`, border radius, solid fills, transforms and transitions. When porting CSS: `px` becomes `dp`; `ms` durations become seconds and `cubic-bezier(.23,1,.32,1)` becomes `cubic-out`; `border: 1px solid c` becomes `border: 1dp c`; `:root` becomes `body`; reduced motion becomes `@media (theme: reduced-motion)`; tabular digits use the `InterNumeric` face. The Text Editor and `validateAsset` report RmlUi's warnings (`ui_check`).

**Icons** are glyphs of the embedded Nerd Font Symbols font, loaded as the `Symbols` face (`src/ui/rml_backend.c`). Authored documents write the codepoint in a `.icon` span (`<span class="icon">&#xea76;</span>`) or bind a command's `icon`. C code uses the named `KIT_ICON_*` values in `src/editor/ui/kit_icons.h`, each commented with its `nf-` name so it can be found with `rg`. For a new icon, find it in the [Nerd Fonts cheat sheet](https://www.nerdfonts.com/cheat-sheet). Material Design icons are in the supplementary private-use plane, such as `nf-md-play` (U+F040A).

## Workspace

- **Areas.** The workspace is a split tree of areas, as in Blender. A leaf holds an editor type and its view state; a split holds an axis and a ratio. Every area has an editor-type menu in its header (`AreaHeader`), and splitters resize areas. Drawers are a collapsible leaf at the bottom of the main column (Output, Assets, Performance). The hovered area receives editor shortcuts; Ctrl+Space maximizes it.
- **Workspaces.** Layout stacks Scene over Behavior. UI sets the Canvas beside the Text Editor, with Hierarchy over Details on the right. Each remembers its editors, splits, drawer and side pane. A UI document's tab selects UI and any other tab Layout (Settings ▸ Appearance ▸ Workspace Follows Tab); View ▸ Workspace or the top bar's tabs switch by hand until the tab changes. Switching changes only the areas: documents, selection, history and playback stay. Reset Layout resets the active workspace. Presets are fixed.
- **Areas follow the active document.** While a UI document is active, the Scene area is its Canvas, the Outliner its Hierarchy, Properties its Details, Assets its Library, and the Text Editor area (`STUDIO_EDITOR_CODE`) shows its source (Unity's UI Builder, Unreal's UMG Designer). Selecting in any of them, or moving the Text Editor's cursor, selects in all.
- **Chrome is authored.** The top bar (`TopBar`), status bar (`StatusBar`) and area headers (`AreaHeader`) are documents of the Studio project. The menubar's menus are markup in `TopBar`'s `#menus`; the menubar, play target menu, debugger steps, UI Scale, editor type menus and editors' own controls stay native in slots.

## Interaction rules

- Keys act on the area under the pointer. Global keys: ⌘K, ⌘P, ⌘S, ⌘Z, ⌘= ⌘- ⌘0 (UI Scale), Space (play or pause), F2, F10. Shortcuts follow the platform, including the web build on a Mac: Command is the primary modifier, and chords are written ⇧⌘Z.
- One modifier rule for numbers: Alt for fine steps, Shift for coarse, Ctrl to snap (Base UI's rule; Blender uses Shift for precision).
- Every gesture commits once: a scrub, a picker session or a drag is one undo step. Live updates never write history. Undo, Redo, Save and the dirty mark follow the active document.
- While the scene plays, properties show live values and are read-only.
- Motion: press 110 ms, popovers 140 ms in and 100 ms out, drawers 200 ms in and 100 ms out. Keyboard-driven changes are instant, and Reduce Motion removes transitions.
- Results the user might act on or must not miss go in a toast (Undo after Delete, a failed save); the status bar reports the last operation and fades after three seconds. Output ▸ Info keeps the full history.
- Density is a setting; Compact is the default. UI Scale (50–200%) sizes the whole editor, as Blender's Resolution Scale does; games' UI keeps its own scale. The Scene's view zoom (⌘ or Ctrl with the wheel) magnifies the rendered frame with its game UI, separately from the camera.
- An idle editor draws at most four frames a second. Input, a running scene, a frame that draws anything new, or RmlUi's next update (a caret blink) bring back full rate.
- Undecided: orange selection in the viewport and graph, blue in lists.

## Decisions

- **Text is the source of truth.** The Text Editor and the visual designer change a UI document through `text_edit`s (`src/support/text_edit.h`). There is one undo stack, and the author's formatting survives. Agents write the `.rml` and `.rcss` files, as a person does.
- **One history: the document's buffer.** The `ui_buffer` keeps the undo history and the Text Editor's code editors keep none (`kit_code_desc.history`). Typing reaches the buffer as typed text, which joins the step being typed; an accepted completion is a step of its own; and the editor's undo keys (⌘Z, Vim's `u` and Control+R) step the buffer as Studio's Undo does. Whatever changes the buffer comes back to the editor as one edit, so its cursor keeps its place, and after an undo it lands on the change. Each open document part keeps its own code editor, with its cursor, scroll and Vim state.
- **The text editor is [Zep](https://github.com/Rezonality/zep)** (MIT; Vim and standard modes), drawn through RmlUi. `src/editor/text/zep_bridge.{h,cpp}` is a pass-through C wrapper, like `src/ui/rml.cpp`; drawing, input routing and layout stay in C (`src/editor/rmlui/kit_code.c`).
- **tree-sitter parses RML and RCSS.** It provides highlighting, the outline, element and rule ranges, diagnostics, and the edits the designer makes.
- **Behaviors feed documents data as `self`.** Set UI Data (Entity, Name, Value) gives an entity's documents a value to bind as `self.<Name>`, the way a component's props reach it. Value takes any type and arrives as JSON: `{{ self.score }}` shows a number; an array of structs repeats with `data-for="row : self.items"` and `{{ row.name }}`; Name `items.2.name` sets one row's field. Values change in place, so bound views stay bound. Studio's own panels bind `self` to their Studio-scene entity the same way.
  - Studio reflects each document's `self` bindings (`ui_document_bindings`): the Add menu offers "Set items (Hud)" for the documents on what attaches the behavior, Name offers their names, and the MCP's searchNodes returns the same presets with the rows' fields.
- **VirtualList is an authored widget.** `<use ui="VirtualList" id="list">` wraps a row template (`data-for="row, i : self.rows"`, rows of one fixed height). The VirtualList behavior reads where the list is scrolled (Get UI Scroll), sets `self.first` and `self.row`, and sends ShowRows(First, Count) when the rows in view change; the entity's own behavior answers with `self.rows` (Array's Slice of its data) and `self.total`. The widget gallery (WidgetGallery) shows ten thousand rows this way.
- **Live apply is an overlay.** `src/support/text_overlay.h` holds unsaved text: `text_file_read` returns it in place of the file, and `file_watch_changed` sees each change at once. So panels, the Scene view and page themes update without a new API. The `code_apply_live` preference chooses between live and on-save.
- **Commands are one table.** `studio_commands.c` feeds menus, the palette, shortcuts and authored UI, where `emit('studio.<id>')` runs a command, `emit('studio.<id>', index)` runs it on a tab or an area, as VS Code's commands take arguments, and `commands.<id>` binds its state. Ids are camelCase, so both name it the same.
- **Preferences are one table.** Each is a row of `studio_settings[]` (`studio_settings.c`) that the preferences file, the saved keys panels bind and the Settings page share. Project settings (name, start scene, default root document) write `project.yaml`.
- **Component presentation is authored per project** in `content/editor/Components.yaml` (`src/editor/documents/component_presentation.h`): Properties tabs, prototype icons, number ranges and units. Studio re-reads it within a second; an invalid edit keeps the last valid file.
- **The UI Inspector inspects every UI surface Studio draws,** as Chrome DevTools, Unity's UI Toolkit Debugger and Unreal's Widget Reflector do: its chrome, its authored panels, the graph canvas, and a game's UI in the Scene at any view zoom. ⇧⌘C, Inspect Element or the panel's Pick starts picking. It shows the element tree, styles by rule with each rule's file and line, computed values, attributes and forced states; Edit Source opens the ui asset at its line. It is itself a panel of the Studio scene, so it can inspect itself. Kit chrome reads Native (C), with its kit path.
- **Play in the Studio project runs Studio itself**, since the editor is that project's game: a guest Studio fills the Scene area, built from the project's working copy and editing the same scene authoring. Play again or Stop ends it.

## Open work

- Splitting and joining areas by hand.
- Modal G/R transforms and a gizmo in the Scene; pointer lock for scrubbing.
- Editors the removed ImGui editor had and Studio lacks: shader details, the color ramp and image editors, and input bindings. Build them in `projects/studio`.

## References

Unity's UI Toolkit is the closest analog: UXML/USS map to RML/RCSS.

| Reference | What we take |
|---|---|
| Unity UI Builder | Hierarchy, Canvas, Inspector, Library and a code view for a UI document |
| Unity UI Toolkit Debugger, Unreal Widget Reflector | Pick any element in any editor window; see its rules and where they are defined |
| Blender | Areas with editor-type headers; Edit Source (right-click any control, land in the Text Editor at its line); areas that follow the active document |
| Godot, Unreal | The editor/project settings split |
| VS Code, JetBrains | Settings search and the command registry |
| Storybook | A widget previewed alone, with a log of its actions |
| Chrome DevTools | The Elements and Styles panes |

## Contracts

| Contract | Where | Notes |
|---|---|---|
| `text_edit`, `text_edits_apply` | `src/support/text_edit.h` | Byte ranges into the original text; applied in any order |
| `text_overlay_set/text/revision/clear` | `src/support/text_overlay.h` | Paths match however they are spelled. NULL text removes the overlay |
| Command registry | `studio_internal.h` (`studio_command_*`), `studio_commands.c` | Add a command by adding a table row plus its `enabled`/`run` cases |
| Settings table | `studio_settings[]` in `studio_settings.c`, `studio_setting_*` in `studio_internal.h` | Add a preference by adding a `studio_prefs` field and a row; the preferences file, its saved key and the Settings page follow |
| Authored panels | `studio_ui_panel`, `studio_ui_area`, `studio_ui_modal`, `studio_ui_show` in `studio_internal.h` (`studio_ui.c`) | Show a Studio-scene entity's GameUi document in a kit host, an area, or over the editor |
| `STUDIO_EDITOR_CODE` | `studio_code.c` | The Text Editor area: the shown UI document's part in `kit_code`. Typing reaches the `ui_buffer` as typed text; edits made elsewhere come back to the editor as `text_edit`s. `s->code.editor` is the code editor shown, holding `editor_path`'s `editor_part`; each open part keeps its own |
| `kit_code`, `kit_code_editor` | `src/editor/ui/kit.h` | The code editor on Zep: `kit_code_apply` takes `text_edit`s as one undo step; results report changes, `:w`, cursor moves, chords for the command registry, accepted completions, and the undo keys (with `desc.history`), Go to Definition, Find References and Control+Space for the host; `desc` carries highlighting spans, diagnostics by line, completion items and the hover card's text for the byte `kit_code_hover` names |
| `studio_open_ui_source(s, path, line)`, `studio_open_ui_part(s, path, part, line)` | `studio_code.c` | Opens a UI document in the Text Editor and selects a 1-based line of one part: `studio_open_ui_source` takes an element source (`data-src`, the `.rml` line) or a `.rcss` path, `studio_open_ui_part` with `UI_PART_RCSS` a style origin. A ui asset's `.rcss` opens the document its `.rml` is (`ui_buffer_document`) |
| `ui_buffer` | `src/editor/documents/ui_buffer.h` | A UI document's text (`APP_EDITOR_DOCUMENT_UI` tabs): a ui asset's `.rml` and `.rcss` as its RML and RCSS parts, or a stylesheet's `.rcss`; changed through `text_edit`s with undo, checked, published as overlays, each part saved to its file as written |
| `studio_preview_ui(s, asset)` | `studio_canvas.c` | Shows one ui asset alone in the Scene area (the Canvas), as Storybook shows a story: at a screen size, on a light or dark backdrop, with an Actions log of the events it emits. NULL returns the area to the scene. With the Studio project open, the Canvas binds the data Studio's chrome does (`world`, `theme`, `inspector`, `settings`, `editor`, `commands`) through `studio_sources_show`, which applies nothing the preview writes; its `studio.*` events only reach the Actions log |
| `render_ui_context(ui)` | `src/render/ui.h` | The game UI's RmlUi context, for the Inspector |
| Host data | `render_ui_new(content_root, data)`, `render_ui_data(ui)` in `src/render/ui.h`; `engine_viewport_adopt_ui` in `src/runtime/engine.h` | A host gives its documents `data`'s root members as variables beside `save` and `system`, and shows them in `render_ui_data`'s model; a viewport takes documents a host made this way |
| Game UI keyboard | `render_ui_wants_text` in `src/render/ui.h`; `engine_viewport_ui_keys`, `engine_viewport_ui_typing` and the host input's `keys` in `src/runtime/engine.h` | Hosts give game documents key presses and typed text, in order, while the game plays: Studio's Scene and Canvas preview through the host input, the standalone player from ImGui and Cute. While a text field has focus (`engine_viewport_ui_typing`), gameplay and single-key shortcuts leave its letters alone and the host starts SDL text input |
| Text Editor preferences | `studio_prefs`: `code_vim`, `code_font_size`, `code_apply_live`, `code_wrap` | Mirrored to `save.codeVim`, `save.codeFontSize`, `save.codeApplyLive` and `save.codeWrap` |
| Text Editor intelligence | `src/editor/documents/ui_code.h` | A part's problems as `kit_code_marker`s (`ui_code_markers`, over `ui_code_check`), completions as `kit_code_completion`s, Go to Definition's and Find References' places in a part's text (`ui_code_definition`, `ui_code_places`), and the one history over a code editor and its buffer (`ui_code_record`, `ui_code_replay`, `ui_code_follow`) |
| Typing composes | `ui_code_record(b, part, editor, base, …)` in `ui_code.h` | Typing records what changed from `base`, the text the code editor and its part last shared (`studio_code_editor.synced`), into the part as it stands, so a designer edit made in the same frame composes with it; where both changed the same bytes, the typing stands |
| Diagnostic ranges | `kit_code_marker.start`, `end` in `kit.h`; `ui_code_markers` | A marker underlines its bytes: a syntax error's range, the text naming a widget or import, or what RmlUi quotes, on its line or its rule's; else the line |
| Places | `studio_code_open_place(s, place)` in `studio_code.c`; `studio_reference.ui` | Opens a `ui_code_place` in the Text Editor at its byte; a Find row with a `ui` place opens that way |
| Hover | `ui_source_hover`, `ui_source_describe` in `src/assets/ui_source.h` | What a name is, where the project defines it and how often it is used; the Text Editor's hover card, and `findReferences`' `description` for ui symbols |
| UI language service | `src/assets/ui_source.h` | Parses one `.rml` or `.rcss` file with tree-sitter: spans, outline, diagnostics, completion, symbols and references, and edits of markup as `text_edit`s on the file. Positions are file byte offsets |
| UI check | `ui_check(content_root, name, rml, rcss, …)` in `src/render/ui_check.h` | Every RmlUi warning of a ui asset, at the line of its `.rml` or `.rcss`, or of the import or widget that wrote it |
| Stylesheet check | `ui_check_stylesheet` in `src/render/ui_check.h` | A `.rcss` file's RmlUi warnings, its imports resolved beside it, at its lines or an import's; `ui_code_check` uses it for stylesheets |
| Checking in a kept context | `ui_check_in(context, …)` in `src/render/ui_check.h` | `ui_check` in a scratch context the caller keeps: destroying a backend releases every context's font resources |
| RCSS properties | `ui_check_property_names` in `src/render/ui_check.h`, over `rml_property_name` and `rml_shorthand_name` in `src/ui/rml.h` | What `ui_source_project.properties` takes |
| UI problems | `studio_code_problems`, `studio_code_project_problems` in `studio_internal.h` | Output's Problems rows for the shown UI document, and in Project every ui asset and stylesheet with open documents' unsaved text; each opens its place (`ui_code_place`) |
| UI completion in the MCP | `inspectUi`'s `complete` (`automation_ui_complete` in `src/automation/ui_edit.h`) | `ui_source_complete`'s items at a file line and column, as the Text Editor offers them |
| Element source | `data-src="<Asset>:<line>"` on each element: `ui_document_sources(rml, asset, first)` and `ui_document_expand(…, sources, …)` in `src/assets/ui_document.h` | The line is 1-based in the asset's `.rml`. A widget's elements name the widget, and the div in place of a `<use>` names the use element. Every `ui_mount` and `render_ui`'s page mark their elements; `<slot>` is never marked, and `data-src` is reserved |
| Stylesheet origin | `rml_element_property_source(element, property, sheet, size, &line)` (`src/ui/rml.h`), then `ui_style_origin(sheet, line, path, size, &source_line)` (`src/support/ui_style.h`) | RmlUi reports the rule's sheet name and its line. Hosts install named sheets (`rml_document_set_named_style_sheet`, `rml_document_add_named_style_sheet`) and record each sheet's `rcss_map` with `ui_style_set`. The sheets are `game-ui-<n>` for a `render_ui`, `kit-<document>` for a kit's theme and resources, and `kit-<document>-<key>` for each `kit_style_sheet`. `path` is an imported stylesheet's path, or a ui asset's `.rcss` for its own lines |
| Designer edits | `ui_design_*` in `src/editor/documents/ui_design.h` | The visual designer's changes to a `ui_buffer`: move, insert, duplicate, wrap, delete, attributes, classes, inline style, text, a rule's declaration values, added and removed declarations (`ui_design_add_declaration`, `ui_design_remove_declaration`) and Extract to Class (`ui_design_extract_class`, one step over both parts through `ui_buffer_join_edit`), each one undo step (a scrub's changes join one). Structural changes return the element to select. `ui_design_slots` lists the keyboard sensor's places |
| Designer selection | `studio_designer_shown` and `studio_designer_shown_source` in `studio_internal.h`; `ui_design_child_on_line`, `ui_design_element_at` | Map a shown element to the outline index it was written as, and back, by its `data-src` line and its place among its parent's children on that line; a cursor offset to the element it selects |
| Back and Forward | `studio_nav_jump`, `studio_nav_can_step`, `studio_nav_step` in `studio_internal.h` (`studio_graph_nav.c`) | One history over the graphs visited and the Text Editor's Go to Definition jumps: a jump records the place it leaves and the one it lands on |
| Native slots | `kit_slot_begin(k, key, host, selector)` and `kit_slot_end` in `src/editor/ui/kit.h` (`kit_slot.c`) | Kit content inside a `kit_host`'s authored content, as Unity's IMGUIContainer: the element the selector finds is marked `data-kit-slot="<key>"` and found again every frame, so a reinstalled mount or a recreated data-for item gets its content anew; a slot not begun in a frame loses it. Its path is the host's and the key |
| Chrome focus | `kit_release_focus(k, host)` | RmlUi focuses what is clicked; authored chrome calls it every frame so Studio's shortcuts keep working. Kit content in the host's slots keeps focus, so a header's filter field takes typing |
| Chrome data | `editor` (`studio_status_add`, `studio_status_show` in `studio_internal.h`; `studio_sources.c`) | What `StatusBar`, `TopBar` and `AreaHeader` bind: `hints` ({icon, key, label}), `report`, `reporting`, `scene`, `selection`, `objects`, `playing`, `paused` (at a breakpoint), `tabs` ({label, path, icon, dirty, active, closable}) and `areas` ({index, drawer, maximized}; each `AreaHeader` binds its own as `area` through `data-alias-area`) |
| Commands data | `commands` (`studio_commands_add`, `studio_commands_show` in `studio_internal.h`; `studio_sources.c`) | Each command's state by id: `label`, `menu`, `shortcut`, `icon` (the glyph), `enabled` and `checked`, as `data-attrif-data-disabled="!commands.stop.enabled"` |
| Emit with a value | `rml_emit_fn` in `src/ui/rml.h`, `ui_mount_emit` in `src/render/ui.h` | `emit('Name', value)` records `value` as the activation's value, as an input's change does. Studio runs `emit('studio.<id>', index)` with `studio_command_run_with`: `selectTab` and `closeTab` take a tab's index, `maximize` an area's |
| Area headers | `studio_ui_area_header(s, area, name)` in `studio_internal.h` | A mount of `AreaHeader` per area, whose root binds `area` to `editor.areas[area]`; the editor type menu and the editor's controls stay native in its `#editor-type` and `#controls` slots |
| Authored menus | `TopBar.rml`'s `#menus`, `menus` in `studio_shell.c` | `<menu label>` per menu; `<li command="id">` runs a command by id, `<li list>` is one Studio fills (`projects`, `webProjects`, `add`, `uiScale`, `debugger`), `<li label>` heads a group, `<hr/>` separates and a nested `<menu label>` is a submenu. An unknown id shows as a disabled entry naming it |
| Kit styles | `kit_theme` in `kit.h` | The theme is every kit document's only stylesheet: the workspace and the graph canvas are the Studio project's `theme/workspace.rcss` and `theme/graph.rcss`, scoped to `body.studio` and `body.graph-canvas` |
| Native motion elements | `src/ui/motion.h` (`motion.c`, `text_motion.c`) | `<number-flow data-attr-value="expr" precision grouping prefix suffix/>`, `<morph-text>{{ expr }}</morph-text>`, and `presence` on a `data-if` element, registered for every RmlUi context by the backend. `kit_number_flow`, `kit_morph` and `kit_button_desc.morph` emit the same elements. Each follows the context's `reduced-motion` theme; presence keeps a leaving element until its transitions end through RmlUi's data-if handler (`rml_set_data_if_handler`) and `presence_settle`, which `rml_backend_update` runs |
| Studio in Studio | `studio_guest_start`, `studio_guest_stop`, `studio_session` in `studio_internal.h` (`studio_host.c`) | Play in the Studio project (`own_project`) runs a guest `studio` in the Scene area over the same scene authoring: its own kit, backend and `studio_ui`, the host's preferences copied and never saved, input routed by the host (`to_guest`), drawn to the host's `kit:guest`. It has no Play, Stop or Open Project; the host names no `kit:viewport` while it runs |

## Verifying

- `make test` is the gate. `make test-render` adds the GPU checks, among them `--rmlui-render` and `--rmlui-render-gles` (skipped without a desktop GLES driver); on macOS it needs `CHIBI_ALLOW_DESKTOP_RENDER_TESTS=1`. Focused selectors: `--rmlui-kit`, `--studio-documents`, `--ui-documents`, `--ui-designer`, `--studio-settings`.
- Check UI changes in the web build (`make web-serve`), in both themes and both densities. Serve on your own `WEB_PORT` and stop the server by that port: `pkill -f web_serve.py` also stops other sessions' servers.
- In the hidden browser pane, run `_emscripten_set_main_loop_timing(2, 1)` first, move the pointer with `PointerEvent('pointermove')`, and hold a modifier with a keydown that spans frames.

## Workstream letters

Code comments and test section markers from the build-out tag work by letter:

| | Work |
|---|---|
| A | UI source maps: `data-src`, stylesheet origins |
| B | Language service and MCP: `ui_source`, `ui_check`, `inspectUi` |
| C | Text editor on Zep: `kit_code`, `zep_bridge` |
| D | UI documents: `ui_buffer`, `studio_code.c`, Assets ▸ New |
| E | UI Inspector |
| F | Settings |
| G | Trackpad pinch |
| H | Visual designer |
| I | Code intelligence in the Text Editor |
| J | Authored status bar and native slots |
| K | UI wiring: asset pickers, UI errors in the Scene, UI events in the Add menu, the Canvas |
| N | Native motion elements |
| P | Follow-ups to H, I and J |
| Q | Designer and Text Editor polish |
| T | Authored top bar, document tabs and area headers |
| X | arena2d end to end: the Sandbox HUD, game UI keys and typed text |
