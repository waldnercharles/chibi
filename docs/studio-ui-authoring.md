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
| `STUDIO_EDITOR_CODE` | `studio_code.c` | The Text Editor area: the shown UI document's part, edited through `kit_code_area` until `kit_code` lands |
| `studio_open_ui_source(s, path, line)`, `studio_open_ui_part(s, path, part, line)` | `studio_code.c` | Opens a UI document in the Text Editor and selects a 1-based line of one part: `studio_open_ui_source` takes an element source (`data-src`, the `rml` block's line), `studio_open_ui_part` with `UI_PART_RCSS` a style origin in a `.yaml` asset (the `rcss` block's line). A `.rcss` path has one part, so either takes its file line |
| `ui_buffer` | `src/editor/documents/ui_buffer.h` | A UI document's text (`APP_EDITOR_DOCUMENT_UI` tabs): parts changed through `text_edit`s with undo, checked, published as overlays, saved as canonical YAML |
| `render_ui_context(ui)` | `src/render/ui.h` | The game UI's RmlUi context, for the Inspector |
| Text Editor preferences | `studio_prefs`: `code_vim`, `code_font_size`, `code_apply_live` | Mirrored to `save.codeVim` and `save.codeApplyLive` |
| Element source | `data-src="<Asset>:<line>"` on each element: `ui_document_sources(rml, asset, first)` and `ui_document_expand(…, sources, …)` in `src/assets/ui_document.h` (A) | The line is 1-based in the asset's `rml` block. A widget's elements name the widget, and the div in place of a `<use>` names the use element. Every `ui_mount` and `render_ui`'s page mark their elements; `<slot>` is never marked, and `data-src` is reserved |
| Stylesheet origin | `rml_element_property_source(element, property, sheet, size, &line)` (`src/ui/rml.h`), then `ui_style_origin(sheet, line, path, size, &source_line)` (`src/support/ui_style.h`) (A) | RmlUi reports the rule's sheet name and its line. Hosts install named sheets (`rml_document_set_named_style_sheet`, `rml_document_add_named_style_sheet`) and record each sheet's `rcss_map` with `ui_style_set`. The sheets are `game-ui-<n>` for a `render_ui`, `kit-<document>` for a kit's theme and resources, and `kit-<document>-<key>` for each `kit_style_sheet`. `path` is an imported stylesheet's path; for a line of a ui asset's own `rcss` block it is the asset's `.yaml` path, and the line counts from the block's first line |

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
