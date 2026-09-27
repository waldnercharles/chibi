# Studio edits itself: UI authoring

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

- **Text is the source of truth.** The Text Editor, the visual designer and the MCP all change a UI document through `text_edit`s (`src/support/text_edit.h`). There is one undo stack, and the author's formatting survives.
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
- **Live apply is an overlay.** `src/support/text_overlay.h` holds unsaved text:
  - `text_file_read` returns it in place of the file;
  - `file_watch_changed` sees each change at once.

  So panels, the Scene view and page themes update without a new API. The `code_apply_live` preference chooses between live and on-save.
- **Commands are one table.** `studio_commands.c` feeds menus, the palette, shortcuts and authored UI, where `emit('studio.<id>')` runs a command.
- **Authored UI hosts native content through slots**, as Unity's IMGUIContainer does. The graph canvas, the viewport, virtual-list bodies, menus, popups and the palette stay native.

## Contracts

These are on `main` and used by more than one workstream.

| Contract | Where | Notes |
|---|---|---|
| `text_edit`, `text_edits_apply` | `src/support/text_edit.h` | Byte ranges into the original text; applied in any order |
| `text_overlay_set/text/revision/clear` | `src/support/text_overlay.h` | Paths match however they are spelled. NULL text removes the overlay |
| Command registry | `studio_internal.h` (`studio_command_*`), `studio_commands.c` | Add a command by adding a table row plus its `enabled`/`run` cases |
| `STUDIO_EDITOR_CODE` | `studio_code.c` | The Text Editor area: the shown UI document's part in `kit_code`. Typing reaches the `ui_buffer` as typed text; edits made elsewhere come back to the editor as `text_edit`s. `s->code.editor` is the code editor shown, holding `editor_path`'s `editor_part`; each open part keeps its own (I) |
| `kit_code`, `kit_code_editor` | `src/editor/ui/kit.h` (C, I) | The code editor on Zep: `kit_code_apply` takes `text_edit`s as one undo step; results report changes, `:w`, cursor moves, chords for the command registry, accepted completions, and the undo keys (with `desc.history`), Go to Definition, Find References and Control+Space for the host; `desc` carries highlighting spans, diagnostics by line, completion items and the hover card's text for the byte `kit_code_hover` names |
| `studio_open_ui_source(s, path, line)`, `studio_open_ui_part(s, path, part, line)` | `studio_code.c` | Opens a UI document in the Text Editor and selects a 1-based line of one part: `studio_open_ui_source` takes an element source (`data-src`, the `rml` block's line), `studio_open_ui_part` with `UI_PART_RCSS` a style origin in a `.yaml` asset (the `rcss` block's line). A `.rcss` path has one part, so either takes its file line |
| `ui_buffer` | `src/editor/documents/ui_buffer.h` | A UI document's text (`APP_EDITOR_DOCUMENT_UI` tabs): parts changed through `text_edit`s with undo, checked, published as overlays, saved as canonical YAML |
| `studio_preview_ui(s, asset)` | `studio_canvas.c` (K) | Shows one ui asset alone in the Scene area (the Canvas); NULL returns the area to the scene. The Scene area follows the active UI document with it |
| `render_ui_context(ui)` | `src/render/ui.h` | The game UI's RmlUi context, for the Inspector |
| Text Editor preferences | `studio_prefs`: `code_vim`, `code_font_size`, `code_apply_live`, `code_wrap` | Mirrored to `save.codeVim`, `save.codeFontSize`, `save.codeApplyLive` and `save.codeWrap` |
| Settings table (F) | `studio_settings[]` in `studio_settings.c`, `studio_setting_*` in `studio_internal.h` | Add a preference by adding a `studio_prefs` field and a row; the preferences file, its saved key and the Settings page follow |
| Text Editor intelligence (I) | `src/editor/documents/ui_code.h` | A part's problems as `kit_code_marker`s (`ui_code_markers`, over `ui_code_check`), completions as `kit_code_completion`s, Go to Definition's and Find References' places in a part's text (`ui_code_definition`, `ui_code_places`), and the one history over a code editor and its buffer (`ui_code_record`, `ui_code_replay`, `ui_code_follow`) |
| Places (I) | `studio_code_open_place(s, place)` in `studio_code.c`; `studio_reference.ui` | Opens a `ui_code_place` in the Text Editor at its byte; a Find row with a `ui` place opens that way |
| Hover (I) | `ui_source_hover`, `ui_source_describe` in `src/assets/ui_source.h` | What a name is, where the project defines it and how often it is used; the Text Editor's hover card, and `findReferences`' `description` for ui symbols |
| Checking in a kept context (I) | `ui_check_in(context, …)` in `src/render/ui_check.h` | `ui_check` in a scratch context the caller keeps: destroying a backend releases every context's font resources |
| Element source | `data-src="<Asset>:<line>"` on each element: `ui_document_sources(rml, asset, first)` and `ui_document_expand(…, sources, …)` in `src/assets/ui_document.h` (A) | The line is 1-based in the asset's `rml` block. A widget's elements name the widget, and the div in place of a `<use>` names the use element. Every `ui_mount` and `render_ui`'s page mark their elements; `<slot>` is never marked, and `data-src` is reserved |
| Stylesheet origin | `rml_element_property_source(element, property, sheet, size, &line)` (`src/ui/rml.h`), then `ui_style_origin(sheet, line, path, size, &source_line)` (`src/support/ui_style.h`) (A) | RmlUi reports the rule's sheet name and its line. Hosts install named sheets (`rml_document_set_named_style_sheet`, `rml_document_add_named_style_sheet`) and record each sheet's `rcss_map` with `ui_style_set`. The sheets are `game-ui-<n>` for a `render_ui`, `kit-<document>` for a kit's theme and resources, and `kit-<document>-<key>` for each `kit_style_sheet`. `path` is an imported stylesheet's path; for a line of a ui asset's own `rcss` block it is the asset's `.yaml` path, and the line counts from the block's first line |
| UI language service | `src/assets/ui_source.h` (B) | Parses a ui asset's file (or a `.rcss` file) with tree-sitter: spans, outline, diagnostics, completion, symbols and references, and edits as `text_edit`s on the file. Positions are file byte offsets |
| UI check | `ui_check(content_root, name, text, …)` in `src/render/ui_check.h` (B) | Every RmlUi warning of a ui asset, at the line of its rml or rcss block, or of the import or widget that wrote it |
| RCSS properties | `ui_check_property_names` in `src/render/ui_check.h` (B), over `rml_property_name` and `rml_shorthand_name` in `src/ui/rml.h` | What `ui_source_project.properties` takes |
| Native slots | `kit_slot_begin(k, key, host, selector)` and `kit_slot_end` in `src/editor/ui/kit.h` (`kit_slot.c`, J) | Kit content inside a `kit_host`'s authored content, as Unity's IMGUIContainer: the element the selector finds is marked `data-kit-slot="<key>"` and found again every frame, so a reinstalled mount or a recreated data-for item gets its content anew; a slot not begun in a frame loses it. Its path is the host's and the key |
| Chrome focus | `kit_release_focus(k, host)` (J) | RmlUi focuses what is clicked; authored chrome calls it every frame so Studio's shortcuts keep working |
| Status data | `editor` (`studio_status_add`, `studio_status_show` in `studio_internal.h`; `studio_sources.c`, J) | What projects/studio's `StatusBar` binds: `hints` ({icon, key, label}), `report`, `reporting`, `assets`, `output`, `performance`, `assetsKey`, `outputKey`, `scene`, `selection`, `objects`, `playing` |
| Designer edits (H) | `ui_design_*` in `src/editor/documents/ui_design.h` | The visual designer's changes to a `ui_buffer`: move, insert, duplicate, wrap, delete, attributes, classes, inline style, text and a rule's declaration values, each one undo step (a scrub's changes join one). Structural changes return the element to select |
| Designer selection (H) | `studio_designer_shown` and `studio_designer_shown_source` in `studio_internal.h`; `ui_design_child_on_line`, `ui_design_element_at` | Map a shown element to the outline index it was written as, and back, by its `data-src` line and its place among its parent's children on that line; a cursor offset to the element it selects |

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
| B | Language service and MCP | `assets/ui_source.*`, `cmake/tree_sitter.cmake`, `render/ui_check.*`, `automation/ui_edit.c`, the controller's UI hooks, `tools/mcp`, `docs/mcp.md` |
| C | Text editor on Zep | `cmake/zep.cmake`, `editor/text/zep_bridge.*`, `rmlui/kit_code.c`, the JetBrains Mono embed, `theme/code.rcss` |
| D | UI documents | `documents/ui_buffer.*`, the UI cases in `scene_tabs.c` and `studio_shell.c`, `studio_code.c`, Assets ▸ New |
| E | UI Inspector | `studio/studio_inspect.c`, `rmlui/kit_inspect.c`, and `projects/studio`'s Inspector assets and scene entity |
| F | Settings | `studio_prefs.c`, `studio_settings.c`, the settings section of `studio_sources.c`, the Settings and widget assets |
| G | Trackpad pinch | Pinch handling in `studio_host.c`, `studio_scene.c` and the graph's zoom; `tools/web_shell.html` |
| K | UI wiring | Asset pickers in `studio_properties.c`, the Scene's UI errors, UI events in the behavior Add menu, the Canvas preview in `studio_scene.c` |

### Wave 2

| | Workstream | Needs |
|---|---|---|
| H | Visual designer: Hierarchy drag, Details, style edits as `text_edit`s | A, B, C, D |
| I | Code intelligence in the Text Editor: highlighting, completion, hover, go to definition, diagnostics | B, C, D |
| J | Authored chrome: the status bar as an authored asset with native slots | Commands, E |

### Wave 3

- Top bar, document tabs, transport and area headers become authored layouts.
- End to end on arena2d: New Widget → edit → `<use ui>` in a HUD → pick it in Play → handle its event in a graph.

## Rules for every workstream

- Branch from current `main`; rebase before the PR.
- Run the gates below. CI isn't running, so they run locally.
  - `make build`
  - `make test`
  - `CHIBI_ALLOW_DESKTOP_RENDER_TESTS=1 make test-render`
  - `make check-format`
  - `build/chibi_ctl --project-root <root> --format` after hand-editing assets
  - `make web`, then check it in the browser
- Serve the web build on your own port and stop the server by that port. `pkill -f web_serve.py` also stops other sessions' servers.
- In the hidden browser pane:
  - run `_emscripten_set_main_loop_timing(2, 1)` first;
  - move the pointer with `PointerEvent('pointermove')`;
  - hold a modifier with a keydown that spans frames.
- Build one worktree at a time where you can (`make build JOBS=4`). Each build directory takes about 2 GB.
- Polish: follow `docs/prototypes/studio/DESIGN.md` and `docs/editor-ui-references.md`, and use the kit's primitives. Review each new surface in the web build in both themes and both densities.
- Update `docs/editor-ui-references.md` and the status table in `docs/authoring-navigation.md` as features land.
