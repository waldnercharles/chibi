# Studio keys

Every key Studio takes, by the area it acts in. **Mod** is Command on a Mac (the web build on a Mac included) and Ctrl elsewhere; menus write chords in the platform's notation (⇧⌘Z, Ctrl+Shift+Z), and on a Mac write Backspace as Delete and Delete as Fwd Delete, as its keys are labeled. Help ▸ Keyboard Shortcuts (Mod+/) shows this table in Studio, searchable, with the current bindings.

## Rules

- **Keys act on the area under the pointer** (Blender). A command chosen from a menu or Search acts on the area clicked last.
- **Commands** come from one registry (`src/editor/studio/studio_commands.c`). Settings ▸ Keyboard rebinds any of them: a recorded chord replaces the default and its aliases, conflicts show as "Also assigned to …" and are counted and searchable ("conflict"), and Reset restores the default. Bindings persist with the preferences.
- **Text fields** keep their keys: typing, Enter, Escape, Mod+A/C/V/X/Z/Y and caret keys. Other Mod chords still run commands, as VS Code's do (Mod+S saves, Mod+K searches). A key that a field, popup or modal releases (Escape, Enter) does nothing else in the same frame.
- **Popups, Search and modal dialogs** own every key while open; Escape closes them.
- **While playing**, a click in the Scene gives the game the keyboard. Studio keeps chords with Mod and the function keys (Stop, playback speed, the debugger), which games do not bind. Hold Alt to navigate the Scene camera while playing.
- **Web build**: the page lets Studio take Command chords even while a field has focus (the browser would otherwise save, print, zoom or bookmark); Mod+V still pastes through the browser, into the focused field, or else into Studio's Paste (objects or nodes), so text copied in another tab or session reaches both. Option chords dispatch as their letter (⌥G, not ©).
- Held keys repeat only Undo, Redo, UI Scale, playback speed, Up and Down (with Shift too) in the Outliner, and the debugger's steps.

## Global

Rebindable commands, active over any area unless noted.

| Key | Action | Context | Source |
| --- | --- | --- | --- |
| Mod+K, Mod+P | Search (command palette; `>` commands, `@` symbols, `?` help) | Global | `studio_commands.c` |
| Mod+, | Settings | Global | `studio_commands.c` |
| Mod+/ | Keyboard Shortcuts | Global | `studio_commands.c` |
| Mod+S | Save the active document (the graph's while the pointer is over it) | Global | `studio_commands.c`, `studio_shell.c` |
| Mod+Z | Undo, in the document of the area under the pointer | Global | `studio_commands.c`, `studio_shell.c` |
| Mod+Shift+Z, Mod+Y | Redo | Global | `studio_commands.c` |
| Mod+F | Find: the graph's and Text Editor's find widgets, else the area's filter field | Behavior, Text Editor, Outliner, Properties, Assets, Output, Members | `studio_commands.c` |
| Mod+=, Mod+Shift+=, Mod+Plus, Mod+Keypad + | Zoom In (UI Scale) | Global | `studio_commands.c` |
| Mod+-, Mod+Shift+-, Mod+Keypad - | Zoom Out | Global | `studio_commands.c` |
| Mod+0, Mod+Keypad 0 | Reset Zoom | Global | `studio_commands.c` |
| Mod+Shift+A | Toggle the Assets drawer | Global | `studio_commands.c` |
| Mod+Shift+U | Toggle the Output drawer | Global | `studio_commands.c` |
| Mod+Shift+C | Inspect Element (UI Inspector picking) | Global | `studio_commands.c`, `studio_inspect.c` |
| Ctrl+Space | Maximize the area under the pointer, or restore | Global | `studio_commands.c` |
| Space (also with Shift or Alt) | Play, Pause, or Resume a paused run | Except Behavior | `studio_commands.c` |
| Shift+F5 | Stop | Global | `studio_commands.c` |
| Mod+Alt+-, Mod+Alt+= | Slower, Faster: step the playback speed through 0.1×, 0.25×, 0.5×, 1× and 2× (UI Scale's keys with Alt; Stop returns to 1×) | Global | `studio_commands.c`, `studio_debug.c` |
| Mod+Alt+0 | Normal Speed | Global | `studio_commands.c`, `studio_debug.c` |

Commands without a default chord (Reload, Open Prototype, Apply Overrides to Prototype, Save as New Prototype, Reset Camera, Toggle Performance, Reset Layout, Step, Record Gameplay, Show Live Values, the density, motion and theme toggles, Preview UI Asset, New …, Add Component, the layout modes, Side Pane, Collaboration and its actions, Close Tab, Close Drawer) are in the menus and Search, and Settings ▸ Keyboard can bind them.

## Scene and Outliner

Commands act on the whole selection; F2 renames the active object.

| Key | Action | Context | Source |
| --- | --- | --- | --- |
| F | Frame Selected: fit every selected object | Scene, Outliner | `studio_commands.c` |
| Home | Frame All | Scene | `studio_commands.c` |
| F2 | Rename the active object in place | Scene, Outliner | `studio_commands.c`, `studio_scene.c` |
| Shift+D, Mod+D | Duplicate the selection one grid increment down and right; the copies become the selection | Scene, Outliner | `studio_commands.c`, `studio_shell.c` |
| X, Backspace, Delete | Delete the selection (Undo in the toast) | Scene, Outliner | `studio_commands.c`, `studio_shell.c` |
| Mod+C, Mod+X, Mod+V | Copy, Cut (Undo in the toast) and Paste objects; Paste centers them under the pointer over the Scene, else one grid increment from where they were copied | Scene, Outliner | `studio_commands.c`, `studio_shell.c` |
| Mod+A | Select All | Scene, Outliner | `studio_commands.c` |
| Escape | Deselect All (not while a drag is held) | Scene, Outliner | `studio_commands.c` |
| Up, Down | Select previous and next row | Outliner | `studio_commands.c` |
| Shift+Up, Shift+Down | Extend the selection from its anchor | Outliner | `studio_commands.c` |
| Mod+click | Add a row to the selection, or take it out | Outliner | `studio_scene.c` |
| Shift+click | Select the rows from the anchor (the last row clicked) | Outliner | `studio_scene.c` |
| Shift+click, Mod+click | Add an object to the selection, or take it out (not on a gizmo handle, where Shift keeps the aspect) | Scene | `workspace/scene_viewport.c` |
| Shift+drag, Mod+drag | Add a marquee's objects to the selection | Scene | `workspace/scene_viewport.c` |
| Mod or Ctrl + wheel | Magnify the Scene view; middle drag pans it | Scene | `studio_scene.c` |
| Alt (held) | Navigate the camera while playing | Scene | `workspace/viewport.c` |

### Scene tools (reserved, in progress on other branches)

| Key | Action | Context |
| --- | --- | --- |
| Q | Select tool | Scene |
| W | Move tool | Scene |
| E | Rotate tool | Scene |
| R | Scale tool | Scene |
| G, then X or Y | Grab (modal move), constrained to an axis | Scene |
| TBD | Snap toggle | Scene |
| Numpad 0, TBD | View Through Camera | Scene |
| K | Keep Simulation Changes | Properties, Scene, while playing |

Q, W, E, R and G are free in the Scene; Q is Straighten and G places a Gate only in the Behavior area. Two overlaps need the modal grab to own its keys while it runs: X (and Backspace or Delete) deletes in the Scene, and Escape deselects there, so the grab must take X and Y as constraints and Escape as cancel before the commands see them. While playing, a game with the keyboard takes plain letters, so K over the Scene reaches Studio only when the game does not have the keyboard.

## Behavior

Keys of the graph canvas (`studio_graph_input.c`, `studio_graph_find.c`, `studio_graph_nav.c`), active while the pointer is over it.

| Key | Action | Source |
| --- | --- | --- |
| Shift+A, Tab | Add node at the pointer | `studio_graph_input.c` |
| X, Backspace, Delete | Delete nodes | `studio_graph_input.c` |
| Shift+D, Mod+D | Duplicate nodes | `studio_graph_input.c` |
| Mod+A | Select all nodes | `studio_graph_input.c` |
| Mod+C, Mod+X, Mod+V | Copy, cut, paste at the pointer | `studio_graph_input.c` |
| F | Frame the selection, or all | `studio_graph_input.c` |
| Home | Frame all | `studio_graph_input.c` |
| C | Comment frame around the selection | `studio_graph_input.c` |
| Q | Straighten | `studio_graph_input.c` |
| Shift+L | Arrange | `studio_graph_input.c` |
| L, Alt+L | Select linked inputs, outputs | `studio_graph_input.c` |
| Mod+G | Collapse to Function | `studio_graph_input.c` |
| Alt+G | Go to Definition | `studio_graph_input.c` |
| Shift+Alt+F | Find References | `studio_graph_input.c` |
| Shift+Alt+H | Call Hierarchy | `studio_graph_input.c` |
| F2 | Rename the pin or event under the pointer | `studio_graph_input.c` |
| F9 | Toggle breakpoints on the selection | `studio_graph_input.c` |
| Alt+Left, Alt+Right | Back, Forward through visited graphs | `studio_graph_nav.c` |
| Space (held) | Pan with a drag | `studio_graph_input.c` |
| B, S, D, F, N, O, G (held) + click | Place Branch, Sequence, Delay, For Each, Do N, Do Once, Gate | `studio_graph_input.c` |
| Ctrl (held) | Snap while moving | `studio_graph_input.c` |
| Escape | Cancel the drag | `studio_graph_input.c` |
| Down, Up, Enter, Escape | Find: next, previous, go to, close | `studio_graph_find.c` |

Space does not play and Shift+D does not duplicate objects here; Mod+Z, Mod+Shift+Z and Mod+S act on the graph.

## Members

| Key | Action | Source |
| --- | --- | --- |
| F2 | Rename | `studio_members.c` |
| X, Backspace, Delete | Delete (Undo in the toast) | `studio_members.c` |
| Shift+D, Mod+D | Duplicate variable | `studio_members.c` |
| Alt (held) while dragging | Drop a Set node | `studio_members.c` |

## Text Editor

`kit_code.c` and `studio_code.c`. Mod chords it does not edit with (Mod+S, Mod+K, Mod+F, Mod+/, UI Scale, drawers) run Studio's commands.

| Key | Action |
| --- | --- |
| F12, Shift+F12 | Go to Definition, Find References |
| Ctrl+Space | Suggest completions; Up, Down, Enter, Tab, Escape choose |
| Mod+[, Mod+] | Back, Forward |
| Mod+Up, Mod+Down (Mac); Ctrl+Home, Ctrl+End | Start and end of the text |
| Enter or Down, Shift+Enter or Up, Escape | Find: next, previous, back to the text |
| Ctrl+[ | Escape, in Vim mode |

Ctrl+Space completes here; Maximize Area does not reach a focused editor.

## Assets

| Key | Action | Source |
| --- | --- | --- |
| F2 | Rename the asset under the pointer | `studio_assets.c` |
| X, Backspace, Delete | Delete it | `studio_assets.c` |
| Drag onto a field, an Outliner row or a Scene object | Assign the asset to the field of its kind (Release assigns, Escape cancels) | `studio_drop.c` |

## UI Hierarchy and Canvas

The Outliner and Scene while a UI document is open (`studio_designer.c`).

| Key | Action |
| --- | --- |
| Up, Down | Select previous, next (Hierarchy) |
| Left, Right | Fold, unfold (Hierarchy) |
| Space, Enter | Pick up the element; then Up and Down move it, Space, Enter or Tab drops it, Escape cancels |
| Shift+D, Mod+D | Duplicate |
| X, Backspace, Delete | Delete |
| Drag a handle | Resize the selection from its right edge, bottom edge or corner; Shift keeps the aspect at the corner, Escape cancels (Canvas) |

## Debugger

While a run is paused at a breakpoint, which selects the instance it stopped in; function keys reach Studio even while a game has the keyboard. Break When Spawned pauses Play itself, so Space or Play continues it.

| Key | Action | Source |
| --- | --- | --- |
| F5 | Resume | `studio_commands.c` |
| F10 | Step Over | `studio_commands.c` |
| F11 | Step Into | `studio_commands.c` |
| Shift+F11 | Step Out | `studio_commands.c` |

## Menus, Search, popups and fields

The kit's controls (`src/editor/rmlui/kit_popup.c`, `kit_command.c`, `kit_number.c`).

| Key | Action |
| --- | --- |
| Up, Down, Home, End | Move in a menu, list or Search |
| Enter, Space | Choose |
| Escape | Close the popup, Search or dialog; cancel a field edit or a drag |
| Enter, Tab | Commit a field |
| Up, Down; PageUp, PageDown; Home, End | Step a focused number; coarse step; minimum, maximum |
| Alt, Shift, Ctrl (held) | Fine, coarse and snapped steps |
| Escape | End UI Inspector picking (`studio_inspect.c`) |
| Escape | Cancel recording in Settings ▸ Keyboard |
| Up, Down | Settings categories (`NavKeys` behavior) |
