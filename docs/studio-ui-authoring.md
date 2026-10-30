# Studio edits itself: UI authoring

Studio source and shell assets are owned by the extracted `chibi-studio`
repository. Paths under `src/editor/` and `projects/studio/` in this guide are
relative to that repository. The engine supplies the shared SDK interfaces.

The goal: a user authors a UI widget from start to finish inside Studio, and every part of the editor can be inspected from the editor. Behavior-node internals written in C are the only exception. The measure is how much of the editor can be shown editing itself.

## References

Unity's UI Toolkit is the closest analog: UXML/USS map to RML/RCSS.

| Reference | What we take |
|---|---|
| Unity UI Builder | Hierarchy, Canvas, Inspector, Library and a code view for a UI document |
| Unity UI Toolkit Debugger, Unreal Widget Reflector | Pick any element in any editor window; see its rules and where they are defined |
| Blender | Edit Source (right-click any control, land in the Text Editor at its line); areas that follow the active document |
| Godot, Unreal | The editor/project settings split |
| VS Code, JetBrains | Settings search and the command registry |
| Storybook | A widget previewed alone, with a log of its actions |
| Chrome DevTools | The Elements and Styles panes |

## Decisions

- **Text is the source of truth.** The Text Editor and the visual designer change a UI document through `text_edit`s (`src/support/text_edit.h`). There is one undo stack, and the author's formatting survives. Agents write the `.rml` and `.rcss` files, as a person does.
- **One history: the document's buffer.** The `ui_buffer` keeps the undo history and the Text Editor's code editors keep none (`kit_code_desc.history`). Typing reaches the buffer as typed text, which joins the step being typed; an accepted completion is a step of its own; and the editor's undo keys (⌘Z, Vim's `u` and Control+R) step the buffer as Studio's Undo does. Whatever changes the buffer comes back to the editor as one edit, so its cursor keeps its place, and after an undo it lands on the change. Each open document part keeps its own code editor, with its cursor, scroll and Vim state.
- **The text editor is [Zep](https://github.com/Rezonality/zep)** (MIT; Vim and standard modes), drawn through RmlUi, not ImGui.
  - `src/editor/text/zep_bridge.{h,cpp}` is a pass-through C wrapper, like `src/ui/rml.cpp`.
  - Drawing, input routing and layout stay in C (`src/editor/rmlui/kit_code.c`).
- **tree-sitter parses RML and RCSS.** It provides highlighting, the outline, element and rule ranges, diagnostics, and the edits the designer makes.
- **Areas follow the active document**, as in Blender. While a UI document is active:
  - the Scene area is its Canvas;
  - Outliner is its Hierarchy;
  - Properties is its Details;
  - Assets is its Library;
  - the Text Editor area (`STUDIO_EDITOR_CODE`) shows its source.
- **Layout mode changes explicitly**: Vertical Split stacks Scene over
  Behavior; Horizontal Split puts Canvas beside the Text Editor, with
  Hierarchy and Details on the right. Each mode remembers its sizes, editors,
  drawer and side pane. Opening a source file or changing tabs keeps the mode;
  switching changes only the areas, so documents, selection, history and
  playback stay.
- **Behaviors feed documents data as `self`.** Set UI Data (Entity, Name,
  Value) gives an entity's documents a value to bind as `self.<Name>`, the way
  a component's props reach it. Value takes any type and arrives as JSON:
  `{{ self.score }}` shows a number; an array of structs repeats with
  `data-for="row : self.items"` and `{{ row.name }}`; Name `items.2.name` sets
  one row's field. Values change in place, so bound views stay bound. Each
  entity's data is a data variable of its own (`e<entity>`), so what one
  entity's behaviors set re-evaluates only the views bound to it. Studio's
  own panels bind `self` to their Studio-scene entity the same way.
  - Studio reflects each document's `self` bindings
    (`ui_document_bindings`): the Add menu offers "Set items (Hud)" for the
    documents on what attaches the behavior, and Name offers their names.
- **VirtualList is an authored widget.** `<use ui="VirtualList" id="list">`
  wraps a row template (`data-for="row, i : self.rows"`, rows of one fixed
  height). The VirtualList behavior reads where the list is scrolled (Get UI
  Scroll), sets `self.first` and `self.row`, and sends ShowRows(First, Count)
  when the rows in view change; the entity's own behavior answers with
  `self.rows` (Array's Slice of its data) and `self.total`. The widget gallery
  (the Widgets panel, WidgetGallery) shows ten thousand rows this way.
  A panel source in C uses the `List` widget instead, which
  `ui_list_window` windows as the source syncs; the Inspector's element tree
  shows ten thousand elements this way, each row keyed by its element.
- **Live apply is an overlay.** `src/support/text_overlay.h` holds unsaved text:
  - `text_file_read` returns it in place of the file;
  - `file_watch_changed` sees each change at once.

  So panels, the Scene view and page themes update without a new API. The `code_apply_live` preference chooses between live and on-save.
- **Commands are one table.** `studio_commands.c` feeds menus, the palette, shortcuts and authored UI, where `emit('studio.<id>')` runs a command, `emit('studio.<id>', index)` runs it on a tab or an area, as VS Code's commands take arguments, and `commands.<id>` binds its state. Ids are camelCase, so both name it the same.
- **Authored UI hosts native content through slots**, as Unity's IMGUIContainer does. The graph canvas, the viewport, the areas' editors still in the kit and the kit's own menus stay native; the Workspace's places hold each area's document, and the palette, the asset pickers, the refactor popover, the graph canvas's context menus and Properties' Node and Member tabs are authored (`Palette.rml`, `Picker.rml`, `Refactor.rml`, `GraphMenu.rml`, `Properties.rml` with `GraphField`). A choice or asset field on a graph node, in the canvas's own context, opens Studio's picker under a box (`studio_palette_open_box`). Authored menus and popovers share one stack with the kit's popups (`src/ui/popover.c`, F3). A popover marked `data-template`, as the Menu and Submenu widgets are, builds its content when it opens and removes it after its exit transition.

## Contracts

These are on `main` and used by more than one workstream.

| Contract | Where | Notes |
|---|---|---|
| `text_edit`, `text_edits_apply` | `src/support/text_edit.h` | Byte ranges into the original text; applied in any order |
| `text_overlay_set/text/revision/clear` | `src/support/text_overlay.h` | Paths match however they are spelled. NULL text removes the overlay |
| Command registry | `studio_internal.h` (`studio_command_*`), `studio_commands.c` | Add a command by adding a table row plus its `enabled`/`run` cases |
| `STUDIO_EDITOR_CODE` | `studio_code.c` | The Text Editor area: the shown UI document's part in `kit_code`. Typing reaches the `ui_buffer` as typed text; edits made elsewhere come back to the editor as `text_edit`s. `s->code.editor` is the code editor shown, holding `editor_path`'s `editor_part`; each open part keeps its own (I) |
| `kit_code`, `kit_code_editor` | `src/editor/ui/kit.h` (C, I) | The code editor on Zep: `kit_code_apply` takes `text_edit`s as one undo step; results report changes, `:w`, cursor moves, chords for the command registry, accepted completions, and the undo keys (with `desc.history`), Go to Definition, Find References and Control+Space for the host; `desc` carries highlighting spans, diagnostics by line, completion items and the hover card's text for the byte `kit_code_hover` names |
| `studio_open_ui_source(s, path, line)`, `studio_open_ui_part(s, path, part, line)` | `studio_code.c` | Opens a UI document in the Text Editor and selects a 1-based line of one part: `studio_open_ui_source` takes an element source (`data-src`, the `.rml` line) or a `.rcss` path, `studio_open_ui_part` with `UI_PART_RCSS` a style origin. A ui asset's `.rcss` opens the document its `.rml` is (`ui_buffer_document`) |
| `ui_buffer` | `src/editor/documents/ui_buffer.h` | A UI document's text (`APP_EDITOR_DOCUMENT_UI` tabs): a ui asset's `.rml` and `.rcss` as its RML and RCSS parts, or a stylesheet's `.rcss`; changed through `text_edit`s with undo, checked, published as overlays, each part saved to its file as written |
| `studio_preview_ui(s, asset)` | `studio_canvas.c` (K) | Shows one ui asset alone in the Scene area (the Canvas); NULL returns the area to the scene. The Scene area follows the active UI document with it. With the Studio project open, the Canvas binds the data Studio's chrome does (`world`, `theme`, `inspector`, `settings`, `editor`, `commands`) through `studio_sources_show`, which applies nothing the preview writes; its `studio.*` events only reach the Actions log |
| `render_ui_context(ui)` | `src/render/ui.h` | The game UI's RmlUi context, for the Inspector |
| Host data | `render_ui_new(content_root, data)`, `render_ui_data(ui)` in `src/render/ui.h`; `engine_viewport_adopt_ui` in `src/runtime/engine.h` | A host gives its documents `data`'s root members as variables beside `save` and `system`, and shows them in `render_ui_data`'s model; a viewport takes documents a host made this way |
| Game UI keyboard (X) | `render_ui_wants_text` in `src/render/ui.h`; `engine_viewport_ui_keys`, `engine_viewport_ui_typing` and the host input's `keys` in `src/runtime/engine.h` | Hosts give game documents key presses and typed text, in order, while the game plays: Studio's Scene and Canvas preview through the host input, the standalone player from ImGui and Cute. While a text field has focus (`engine_viewport_ui_typing`), gameplay and single-key shortcuts leave its letters alone and the host starts SDL text input |
| Text Editor preferences | `studio_prefs`: `code_vim`, `code_font_size`, `code_apply_live`, `code_wrap` | Mirrored to `save.codeVim`, `save.codeFontSize`, `save.codeApplyLive` and `save.codeWrap` |
| Settings table (F) | `studio_settings[]` in `studio_settings.c`, `studio_setting_*` in `studio_internal.h` | Add a preference by adding a `studio_prefs` field and a row; the preferences file, its saved key and the Settings page follow |
| Text Editor intelligence (I) | `src/editor/documents/ui_code.h` | A part's problems as `kit_code_marker`s (`ui_code_markers`, over `ui_code_check`), completions as `kit_code_completion`s, Go to Definition's and Find References' places in a part's text (`ui_code_definition`, `ui_code_places`), and the one history over a code editor and its buffer (`ui_code_record`, `ui_code_replay`, `ui_code_follow`) |
| Places (I) | `studio_code_open_place(s, place)` in `studio_code.c`; `studio_reference.ui` | Opens a `ui_code_place` in the Text Editor at its byte; a Find row with a `ui` place opens that way |
| Hover (I) | `ui_source_hover`, `ui_source_describe` in `src/assets/ui_source.h` | What a name is, where the project defines it and how often it is used; the Text Editor's hover card, and `findReferences`' `description` for ui symbols |
| Checking in a kept context (I) | `ui_check_in(context, …)` in `src/render/ui_check.h` | `ui_check` in a scratch context the caller keeps: destroying a backend releases every context's font resources |
| Element source | `data-src="<Asset>:<line>"` on each element: `ui_document_sources(rml, asset, first)` and `ui_document_expand(…, sources, …)` in `src/assets/ui_document.h` (A) | The line is 1-based in the asset's `.rml`. A widget's elements name the widget, and the div in place of a `<use>` names the use element. Every `ui_mount` and `render_ui`'s page mark their elements; `<slot>` is never marked, and `data-src` is reserved |
| Stylesheet origin | `rml_element_property_source(element, property, sheet, size, &line)` (`src/ui/rml.h`), then `ui_style_origin(sheet, line, path, size, &source_line)` (`src/support/ui_style.h`) (A) | RmlUi reports the rule's sheet name and its line. Hosts install named sheets (`rml_document_set_named_style_sheet`, `rml_document_add_named_style_sheet`) and record each sheet's `rcss_map` with `ui_style_set`. The sheets are `game-ui-<n>` for a `render_ui`, `kit-<document>` for a kit's theme and resources, and `kit-<document>-<key>` for each `kit_style_sheet`. `path` is an imported stylesheet's path, or a ui asset's `.rcss` for its own lines |
| UI language service | `src/assets/ui_source.h` (B) | Parses one `.rml` or `.rcss` file with tree-sitter: spans, outline, diagnostics, completion, symbols and references, and edits of markup as `text_edit`s on the file. Positions are file byte offsets |
| UI check | `ui_check(content_root, name, rml, rcss, …)` in `src/render/ui_check.h` (B) | Every RmlUi warning of a ui asset, at the line of its `.rml` or `.rcss`, or of the import or widget that wrote it |
| RCSS properties | `ui_check_property_names` in `src/render/ui_check.h` (B), over `rml_property_name` and `rml_shorthand_name` in `src/ui/rml.h` | What `ui_source_project.properties` takes |
| Native slots | `kit_slot_begin(k, key, host, selector)` and `kit_slot_end` in `src/editor/ui/kit.h` (`kit_slot.c`, J) | Kit content inside a `kit_host`'s authored content, as Unity's IMGUIContainer: the element the selector finds is marked `data-kit-slot="<key>"` and found again every frame, so a reinstalled mount or a recreated data-for item gets its content anew; a slot not begun in a frame loses it. Its path is the host's and the key |
| Chrome focus | `kit_release_focus(k, host)` (J) | RmlUi focuses what is clicked; authored chrome calls it every frame so Studio's shortcuts keep working. Kit content in the host's slots keeps focus, so a header's filter field takes typing |
| Chrome data | `editor` (`studio_status_add`, `studio_status_show` in `studio_internal.h`; `studio_sources.c`, J, T) | What projects/studio's `StatusBar`, `TopBar` and `AreaHeader` bind: `hints` ({icon, key, label}), `report`, `reporting`, `scene`, `selection`, `objects`, `playing`, `paused` (at a breakpoint), `tabs` ({label, path, icon, dirty, active, closable}) and `areas` ({index, drawer, maximized}; each `AreaHeader` binds its own as `area` through `data-alias-area`) |
| Commands data (T) | `commands` (`studio_commands_add`, `studio_commands_show` in `studio_internal.h`; `studio_sources.c`) | Each command's state by id: `id`, `label`, `menu`, `shortcut`, `icon` (the glyph), `enabled`, `checkable` and `checked`, as `data-attrif-data-disabled="!commands.stop.enabled"`; MenuCommand shows one with `data-alias-command="commands.<id>"` |
| Emit with arguments (T, F1) | `rml_emit_fn` in `src/ui/rml.h`, `ui_mount_emit` and `ui_activation` in `src/render/ui.h` | `emit('Name', ...)` records up to four arguments as text, the first as the activation's value when it is a number, as an input's change does, with the event's modifiers, button, pointer and sending element (`anchor`). Studio routes it (`studio_panel_route`, `studio_panels.c`): `emit('studio.<id>', index)` runs the command with `studio_command_run_with` (`selectTab` and `closeTab` take a tab's index, `maximize` an area's), and `emit('<source>.<action>', ...)` a panel source's action (`studio_panel_sources` in `studio_sources.c`) |
| Area headers (T, F3) | `studio_ui_area_header(s, area, name)`, `studio_ui_area_controls(s, area, key, name)` in `studio_internal.h` | A mount of `AreaHeader` per area, whose root binds `area` to `editor.areas[area]` (`editor`, `panel`, `icon` and `title` for its editor type menu, a Menu over `choices.editors` that sends `editor.type` and `editor.panel`); the editor's controls stay native in its `#controls` slot, where `studio_ui_area_controls` mounts a Studio panel per area, as the Behavior header's View menu (GraphView) |
| Designer edits (H) | `ui_design_*` in `src/editor/documents/ui_design.h` | The visual designer's changes to a `ui_buffer`: move, insert, duplicate, wrap, delete, attributes, classes, inline style, text and a rule's declaration values, each one undo step (a scrub's changes join one). Structural changes return the element to select. Q added a rule's declarations (`ui_design_add_declaration`, `ui_design_remove_declaration`), Extract to Class (`ui_design_extract_class`, one step over both parts through `ui_buffer_join_edit`) and the keyboard sensor's places (`ui_design_slots`) |
| Designer panels (D1) | `Hierarchy.rml`, `Details.rml`, `Library.rml` and their headers; the `hierarchy`, `hierarchy_ui`, `hierarchy_carry`, `hierarchy_header`, `details`, `details_add` and `library` sources (`studio_designer_hierarchy.c`, `_details.c`, `_library.c`) over plain-C capabilities (`studio_designer_values.c`, `studio_designer_edit.c`, `studio_designer_edit.h`) | The Outliner, Properties and Assets areas show them while a UI document is active, mounted by the same area hooks as their other panels. Rows are TreeRows by key (`hierarchy_ui.selected`, `hierarchy_carry`), an edit is an intent (`emit('details.change', row, value)`, `hierarchy.drop`, `library.add`) that C turns into one `ui_design_*` text edit, and a scrub's `input`s join one step through the designer's gesture key |
| Designer selection (H) | `studio_designer_shown` and `studio_designer_shown_source` in `studio_internal.h`; `ui_design_child_on_line`, `ui_design_element_at` | Map a shown element to the outline index it was written as, and back, by its `data-src` line and its place among its parent's children on that line; a cursor offset to the element it selects |
| Typing composes (Q) | `ui_code_record(b, part, editor, base, …)` in `ui_code.h` | Typing records what changed from `base`, the text the code editor and its part last shared (`studio_code_editor.synced`), into the part as it stands, so a designer edit made in the same frame composes with it; where both changed the same bytes, the typing stands |
| Diagnostic ranges (Q) | `kit_code_marker.start`, `end` in `kit.h`; `ui_code_markers` | A marker underlines its bytes: a syntax error's range, the text naming a widget or import, or what RmlUi quotes, on its line or its rule's; else the line |
| Stylesheet check (Q) | `ui_check_stylesheet` in `src/render/ui_check.h` | A `.rcss` file's RmlUi warnings, its imports resolved beside it, at its lines or an import's; `ui_code_check` uses it for stylesheets |
| UI problems (Q) | `studio_code_problems`, `studio_code_project_problems` in `studio_internal.h` | Output's Problems rows for the shown UI document, and in Project every ui asset and stylesheet with open documents' unsaved text; each opens its place (`ui_code_place`) |
| Back and Forward (Q) | `studio_nav_jump`, `studio_nav_can_step`, `studio_nav_step` in `studio_internal.h` (`studio_graph_nav.c`) | One history over the graphs visited and the Text Editor's Go to Definition jumps: a jump records the place it leaves and the one it lands on |
| UI completion in native inspection (Q) | `inspect_ui`'s `complete` (`automation_ui_complete` in `src/automation/ui_inspect.h`) | `ui_source_complete`'s items at a file line and column, as the Text Editor offers them |
| Studio in Studio | `studio_guest_start`, `studio_guest_stop`, `studio_session` in `studio_internal.h` (`studio_host.c`) | Play in the Studio project (`own_project`) runs a guest `studio` in the Scene area over the same scene authoring: its own kit, backend and `studio_ui`, the host's preferences copied and never saved, input routed by the host (`to_guest`), drawn to the host's `kit:guest`. It has no Play, Stop or Open Project; the host names no `kit:viewport` while it runs |
| Kit styles | `kit_theme` in `kit.h` | The theme is every kit document's only stylesheet: the workspace and the graph canvas are the Studio project's `theme/workspace.rcss` and `theme/graph.rcss`, scoped to `body.studio` and `body.graph-canvas` |
| Panel RCSS and the page body (fx-classes) | `ui_document_scope` in `src/assets/ui_document.c`; `kit_set_body_class` | An asset's RCSS is scoped to its host (`[data-ui="Name"]`), where `body` names the host (`body.area-panel`, `body[data-drop-over]`). The classes right after `body` name the page body too, which the kit gives the density (`comfortable`), the theme (`dark` and `theme-<name>`) and `theme-switching` on every document, so a panel's own `body.comfortable .row { height: 24dp }`, `body.dark .tile` and `body:not(.dark) .viewport` follow them as they change, and the rules live in the panel's `.rcss`, not the shared theme. A rule matches when the host or the page body has the classes (`body.comfortable .row` scopes to `[data-ui="Out"].comfortable .row, body.comfortable [data-ui="Out"] .row`); a negated class (`body:not(.dark)`) asks the page body alone, and another qualifier or a `>`, `+` or `~` after the classes asks only the host. Reduced motion is the context's theme, so write `@media (theme: reduced-motion) { … }`, which scoping keeps. Widgets' RCSS scopes the same way. Per-density sizes can also come from the page's variables (`--h`, `--row`), which a host inherits |
| Authored menus | `TopBar.rml`'s `#menus`, `menus` in `studio_shell.c` | `<menu label>` per menu; `<li command="id">` runs a command by id, `<li list>` is one Studio fills (`projects`, `webProjects`, `add`, `uiScale`, `debugger`), `<li label>` heads a group, `<hr/>` separates and a nested `<menu label>` is a submenu. An unknown id shows as a disabled entry naming it |
| Native motion elements (N) | `src/ui/motion.h` (`motion.c`, `text_motion.c`) | `<number-flow data-attr-value="expr" precision grouping prefix suffix/>`, `<morph-text>{{ expr }}</morph-text>`, and `presence` on a `data-if` element, registered for every RmlUi context by the backend. `kit_number_flow`, `kit_morph` and `kit_button_desc.morph` emit the same elements. Each follows the context's `reduced-motion` theme; presence keeps a leaving element until its transitions end through RmlUi's data-if handler (`rml_set_data_if_handler`) and `presence_settle`, which `rml_backend_update` runs |
| Popovers (F3) | `src/ui/popover.h` (`popover.c`), `<choice-list>` in `src/ui/controls/controls.h`; projects/studio's Menu, MenuItem, MenuCommand, MenuGroup, MenuSeparator, Submenu, MenuCheck, MenuOption, ChoiceList, EnumField and AssetField; `choices` in `studio_sources.c` | The HTML popover attribute for every context: `<button popovertarget="id">` toggles `<div popover id="id">` (the id looked up from the button outward), placed below it in the window, its document above the other areas; `role="menu"` adds Base UI's highlight, typeahead and 100 ms submenus, `<menubar>` switches menus on hover. One stack per context holds them with the kit's popups (`popover_show`, `popover_open`) and the color picker: an outside press and Escape close the top first. `choices` holds `fields.<Component>.<field>`, `assets.<kind>`, `editors` and `panels` tables for EnumField, AssetField and the editor type menu. An alias named as a global variable reads the global |
| Native controls (F2) | `src/ui/controls/controls.h` (`number_field.c`, `color_field.c`, `splitter.c`, `live_text.c`) | `<number-field value min max step precision integer float slider unit addon mixed readonly disabled invalid>`, `<color-field value="#rrggbbaa" mixed readonly disabled>`, `<splitter axis min max value invert>`, `<live-text value>` and `<line-chart series window unit precision reference min max fill live>` (C2: it draws the series C publishes by name with `line_chart_publish`, and new samples redraw it without layout), registered for every RmlUi context by the backend. Each takes attributes in and sends `input` while a gesture runs and `change` once when it commits, both bubbling with `ev.value`, `ev.canceled` and `ev.name` (its `name` attribute); a value repaints without laying out the document. `kit_number`, `kit_color` and `kit_live_label` emit them and read them through `number_field_take` and `color_field_take`; the Workspace's splitters send `workspace.resize`. projects/studio's NumberField, VectorField, ColorField, Checkbox, PropertyRow, Panel and Splitter widgets wrap them for authored panels. A self-closing `<number-field/>` cannot carry `data-for` (RmlUi repeats an element's inner markup), so write `<number-field ...></number-field>` |
| Drag and drop (F4) | `src/ui/drag.h` (`drag.c`) | One drag for every RmlUi context, the editor's and games', registered by the backend: a source has `data-drag="type"` and `data-drag-payload` (`data-drag-label` and `data-drag-icon` dress the overlay); a target has `data-drop="type ..."`, `data-drop-hint`, and `data-drop-zones="tree"` (top and bottom quarters before and after, the middle inside) or `"list"` (halves). It picks up after `FEEL_DRAG_DP`, marks the source `data-dragging`, every target in the context's documents that takes the type `data-drop-accept` and the one under the pointer `data-drop-over` and `data-drop-zone`, shows an overlay in the topmost document, and on release sends the target a bubbling `drop` (`ev.type`, `ev.payload`, `ev.zone`). Escape cancels; the release's click is swallowed and the next press is a click. C surfaces take drops with `ui_drag_dropped`, read the drag with `ui_drag_active` and `ui_drag_over`, and say what a drop does with `ui_drag_hint`. `kit_drag_source` and `kit_drop_target` set the same attributes, so kit and authored lists drag into each other |
| List windows (F4) | `src/ui/list.h` (`list.c`) | `ui_list_window(scroller, list, state, &first)` windows any scroller whose first child is a spacer, Virtuoso's way: rows of one height (`--row` by default), overscan, reveal and follow; the window keeps its length as it scrolls, so a `data-for` over it reuses its rows. `kit_list` wraps it |
| Autofocus and stop() (P5) | `src/ui/autofocus.h` (`autofocus.c`); `stop` in `rml_data_model_create` (`src/ui/rml.cpp`) | An element created with `autofocus` after its document shows (a `data-when` copy, a `data-for` row) takes focus at the end of the update that built it, if it shows; a text field shows its ring and selects its text, as a rename's field. `data-event-click="stop(); emit('...')"` stops the event, as Vue's `.stop`, so a button in a row keeps its click from the row's own handler |
| Graph chrome (G1) | `GraphMenu.rml` over `graph_menu` (`studio_graph_menu.c`); `Properties.rml`'s graph section and `GraphField.rml` over `graph_details`, `graph_values` and `graph_details_ui` (`studio_graph_details.c`, `studio_graph_properties.c`, `studio_member_properties.c`) | A canvas menu is written once in C: `sg_menu_item` adds an entry while the menu is described and returns true only for the key a click sends (`graph_menu.run`), so its code acts. A row's id names what it edits (`n12.i3`, `n12.i3/a1/f0` inside a compound value, `c4.t`, `v7.n`, `g2.p9`); its controls send `graph_details.input`, `.change`, `.check`, `.choices`, `.type`, `.rename`, `.container`, `.remove`, `.add`, `.addPin` and `.reorder` with it. An input's source field (GraphField's `source`) wraps its `<input role=combobox>` in a virtualized `<choice-list>` whose list is the palette's rows (`studio_palette_open_field`): `graph_details.source` opens it, `.query` ranks as typed, `.escape` and `.tab` shut it or choose |
| Members (P5) | `Members.rml`, `MembersHeader.rml`; the `members` and `members_ui` sources in `studio_members.c` | `members_ui` holds the selected member's, the active member's and the named member's row keys and the menu, which the TreeRows bind by key (`data-alias-selection`, `data-alias-active`), so a selection dirties no row; the open graph's row is active by its `active` flag. Rows keyed `s4`, `c3`, `g7`, `v12`, `l5` or `e0.9` send `members.click`, `.edit`, `.menu`, `.uses` and `.add` with their key; the name field `members.renamed` or `.cancel`; the menu `.open`, `.rename`, `.duplicate`, `.refs`, `.calls` and `.delete`. A member drags as `data-drag="member"` with its key; the graph canvas takes it with `ui_drag_dropped` and names the drop with `ui_drag_hint`. Focus in an editor's authored area passes Studio's shortcuts (`studio_ui_shortcut_focus`) |
| List, TreeRow and Tile (F4) | projects/studio's `List`, `TreeRow` and `Tile` widgets | `List` is a scroller a panel source windows (give it an id and `data-event-scroll`); `TreeRow` (`data-alias-item`: key, label, icon, meta, depth, parent, open, selected, active, marked, hidden; the twisty writes `item.open`; the use's children trail; a panel whose selection changes often aliases the selected and the active key, from a variable of their own, as `data-alias-selection` and `data-alias-active`, which TreeRow compares with `item.key`, so a selection dirties no row) and `Tile` (`data-alias-tile`: key, label, icon, badge, image, loading, selected, unresolved; the image paints as a decorator, so a thumbnail arriving lays nothing out; it compares `tile.key` with `data-alias-selection` as TreeRow does). Either marks its own row or tile, the use's only child, so a selection restyles no sibling look like the native rows and tiles, through the theme's `.tree-row` and `.tile`. Put `data-drag`, `data-drop` and zones on the use. The widget gallery's `gallery` source (`studio_gallery.c`) windows the project's assets into a List and collects drops from it and from Assets |
| Area panels (P2) | `studio_ui_area(s, area, name)`, `studio_ui_area_view(s, area, name, views)`, `studio_ui_area_key` and `studio_ui_keyboard_area` in `studio_internal.h` | An editor's authored panel in its area; `studio_ui_area_view` aliases `view` to the area's item of `views` (the Hierarchy's `hierarchy.views[area]`), so each area windows its own List. Only fields, the code editor and a modal's hosted content claim the keyboard (`kit_wants_keyboard`): a focused row, list or button in an area or the chrome passes keys on; when Tab put the focus in an area panel (`:focus-visible`), its area takes the keys the area under the pointer would. A panel binds its own area keys with `data-area-keys="Left:FoldRoot"` (`ui_mount_area_key`), Blender's per-editor keymap |
| Behavior-driven panels (spike) | `studio_nodes.h` (the `studio` pack), `studio_ui.c`'s `dispatch`, `ui_activation_send` and `ui_mount_measure` in `src/render/ui.h` | A panel's logic is its prototype's behaviors, as the Outliner's: its UI events run them in the frame that sent them, with payloads their event assets declare (emit's arguments, and Shift, Alt, Mod and Pointer from the DOM event), then `Refresh`, as a panel source syncs, when it is due: what the editor nodes read changed (`studio_nodes_stamp`), the panel's document is new, or its mount sent an event; they call Studio's editor nodes, which only Studio's world binds the `studio` service for, and which only the Studio project's node search offers. A game's documents send their events the same way, in the update after the click. Lists marked `data-list-rows` are windowed natively and send `window` (ShowRows) |
| Assets (P3) | projects/studio's `Assets`, `AssetsHeader` and `NewAsset`; `assets`, `asset_view` and `asset_menus` in `studio_assets.c` | Assets is one List windowing either TreeRows (the list, grouped by kind) or lines of Tiles (the grid, its columns fitted to the List's width, its tile size as `--tile-width` and `--tile-height`), each in a slot C places that keeps it while it stays in view; each row or tile is marked `data-asset`, which C reads under the pointer for F2, Delete and the resting preview. The selection and the preview live in `asset_view` and the menus in `asset_menus`, so a click or an open menu reevaluates only the views reading them: rows and tiles carry their asset's `key` and alias `asset_view.selected` as their `data-alias-selection`. The header's filter binds `assets.filter` both ways, as view state; the kind, the size, clicks, double clicks, `contextmenu` and the row menu's items send `assets.*` intents. New's name popover is NewAsset's, in a document of its own that mounts only while it asks, shown by C under the header's New button or at the pointer |
| Scene area (C1) | projects/studio's `Scene`, `Canvas` and `SceneHeader`; `scene` and `scene_view` in `studio_scene.c`, `canvas` in `studio_canvas.c` | The area's surface, the scene's or the Canvas's frame, stays a native `kit_surface` in the body's slot (`#scene-surface`, `#canvas-frame-slot`), where pointer input, gizmos, the camera and drops stay C's. `scene_view` shows the overlays: each camera frame's label (`cameras`: name, `at`, a `translate()` in whole pixels, so a pan lays nothing out), each undrawn object's marker (`markers`: icon, `at`, active, selected, hovered, locked), why the tool's gizmo is grey with its Add (`hint`, `hintAction`, `hintAt`), a drag's change by the pointer (`delta`, `deltaAt`), the camera's zoom against its framing as `<number-flow>` (plain `zoomText` while `zooming`), Live, the generated world's progress and the notice line; the status bar names the scene and selection. The header's tools, magnet, Show items, Reset Camera, View Through Camera and Frame bind `commands` and run `studio.<id>`; `scene.zoom` and `scene.increment` choose the Canvas's View Zoom and the grid increment by index, and `scene.addMissing` adds what a grey gizmo lacks; `canvas.back`, `.used`, `.preview`, `.size`, `.backdrop`, `.actions` and `.clear` drive the Canvas, whose Actions log is a List window that follows new events. `studio_ui_area` returns the area's panel host, for the slot |

## Workstreams

Each workstream is one agent in its own worktree and branch, with one PR. Only its owner edits a file. The shared files are append-only, one section per workstream:
- `src/editor/studio/studio_internal.h`
- `src/editor/ui/kit.h`
- `src/ui/rml.h` and `rml.cpp`
- `test/test_suite.c`
- `cmake/sources.cmake`
- `CMakeLists.txt`

### Wave 1

| | Workstream | Owns |
|---|---|---|
| A | UI source maps | `support/rcss.c`, `assets/ui_document.c`, `render/ui.c` (not `render_ui_context`), `restyle` in `rmlui/kit.c` |
| B | Shared language service and native inspection | `assets/ui_source.*`, the third-party archive's tree-sitter, `render/ui_check.*`, `automation/ui_inspect.c`, the controller's UI hooks, [saved-file workflow](runtime-authoring.md) |
| C | Text editor on Zep | `cmake/zep.cmake`, `editor/text/zep_bridge.*`, `rmlui/kit_code.c`, the JetBrains Mono embed, `theme/code.rcss` |
| D | UI documents | `documents/ui_buffer.*`, the UI cases in `scene_tabs.c` and `studio_shell.c`, `studio_code.c`, Assets ▸ New |
| E | UI Inspector | `studio/studio_inspect.c`, the Inspector section of `studio_sources.c`, `rmlui/kit_inspect.c`, and `projects/studio`'s Inspector assets and scene entity. Its tree is a `List` of TreeRows over a window of every element, with no row limit (I1) |
| F | Settings | `studio_prefs.c`, `studio_settings.c`, the settings section of `studio_sources.c`, the Settings and widget assets |
| G | Trackpad pinch | Pinch handling in `studio_host.c`, `studio_scene.c` and the graph's zoom; `web/web_shell.html` |
| K | UI wiring | Asset pickers in `studio_properties.c`, the Scene's UI errors, UI events in the behavior Add menu, the Canvas preview in `studio_canvas.c` |

### Wave 2

| | Workstream | Needs |
|---|---|---|
| H | Visual designer: Hierarchy drag, Details, style edits as `text_edit`s | A, B, C, D |
| I | Code intelligence in the Text Editor: highlighting, completion, hover, go to definition, diagnostics | B, C, D |
| J | Authored chrome: the status bar as an authored asset with native slots | Commands, E |

### Wave 3

- Top bar, document tabs, transport and area headers become authored layouts (T): `TopBar` and an `AreaHeader` per area, with the editors' own controls native in their slots. Since C3 the menubar, the play target's, speed's, layout's and UI Scale's menus, a paused run's steps and the workspace (`Workspace`: the areas' places, splitters, drawer, side column and maximize) are authored too.
- End to end on arena2d (X): the Sandbox's `HUD` document uses a `Meter` widget, `HudDisplay` sets it and handles its `Repair` event (`test/editor/arena_hud.c`); game UI hears keys and typed text in the standalone player and text in the Scene.

| | Workstream | Owns |
|---|---|---|
| Q | Designer and editor polish: the Hierarchy's keyboard sensor, Canvas drop lines, a rule's declarations and Extract to Class, typing that composes with designer edits, diagnostic ranges, stylesheet checks, UI problems in Output, Back after Go to Definition, native inspection completion | `studio_designer*.c`, `ui_design.[ch]`, `studio_code.c`, `kit_code.c`'s markers, `ui_code.[ch]`, the UI parts of `studio_output.c` and `studio_find.c`, `inspect_ui`'s completion |
| X | arena2d end to end: the Sandbox HUD and its behavior, game UI keys and typed text in the standalone player and the Scene, game UI state in runtime snapshots | `projects/arena2d`'s `HUD`, `Meter` and `HudDisplay`, `presentation.c`'s input routing, `render_ui_wants_text`, `engine_viewport_ui_keys` and `engine_viewport_ui_typing`, `test/editor/arena_hud.c` |

## Rules for every workstream

- Branch from current `main`; rebase before the PR.
- Run the gates below. CI isn't running, so they run locally.
  - `chibi build`
  - `make test`
  - `CHIBI_ALLOW_DESKTOP_RENDER_TESTS=1 make test-render`
  - `chibi build format --check`
  - `build/chibi_ctl --project-root <root> --format` after hand-editing assets
  - `make web`, then check it in the browser
- Serve the extracted Studio product with `make web-serve STUDIO_SOURCE=<studio repository> WEB_PORT=<port>` and stop that server process explicitly.
- In the hidden browser pane:
  - run `_emscripten_set_main_loop_timing(2, 1)` first;
  - move the pointer with `PointerEvent('pointermove')`;
  - hold a modifier with a keydown that spans frames.
- Build one worktree at a time where you can (`chibi build JOBS=4`). Each build directory takes about 2 GB.
- Polish: follow [Editor UI references](editor-ui-references.md) and use the kit's primitives. Review each new surface in the web build in both themes and both densities.
- Update `docs/editor-ui-references.md` and the status table in `docs/authoring-navigation.md` as features land.
