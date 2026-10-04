# Studio in RML and RCSS

Studio's whole editor moves to authored RML and RCSS, as the user decided on
2026-10-03. Anything that can't be authored, or that would cost substantial
performance, is postponed and listed in Postponed below. Status: S0, F0,
F1, F2, F3, F4, F5, P1, P2, P3, P4, P5, C1, C2, C3, C4, D1, G1 and I1 done; the rest planned. The
Outliner runs on KDL behaviors over editor nodes (Behavior-driven panels, a spike). The spike (S0) ran on a
loaded machine, so its times are provisional until re-measured on an idle
one (S0 results).

## Target

Every Studio surface is RML for structure, RCSS for style and, where it helps,
authored behaviors for UI-local keys. C keeps the authoring state: selection,
values, overrides, history, playback, preferences, reflection, ranking and
thumbnails. C publishes each panel's data and handles the intents the panel
sends back. Controls that need native speed become native RmlUi elements in
`src/ui`, registered for every context (editor and games), as browsers ship
`<input>`, `<select>` and `<canvas>`.

What already works this way:

- `Settings.rml` lays out rows C builds, building each row's one control
  with `data-when` over its `row.control`, and its Record button sends
  `emit('settings.record', row.key)` to the `settings` source (F1).
- `Inspector.rml` lists the element tree as a List of TreeRows over the window
  of rows the `inspector` source shows, without a row limit (I1).
- `TopBar`, `StatusBar` and `AreaHeader` bind `editor` and `commands`.
- `Properties.rml` and `PropertiesHeader.rml` are Properties' Object,
  Prototype, Scene and Behaviors tabs over the `properties` source (P1),
  and its Node and Member tabs over `graph_details` (G1).

About 1,600 kit call sites remain across 40 files. The largest:
`studio_properties.c` (171), `studio_scene.c` (162), `studio_shell.c` (98),
`studio_code.c` (90), `studio_member_properties.c` (88),
`studio_designer_details.c` (85), `studio_graph_draw.c` (76),
`studio_graph.c` (71), `studio_graph_literals.c` (65), `studio_assets.c` (62).

## Inventory

| Surface | Lives now | Needs from C | Plan |
|---|---|---|---|
| Top bar | Authored: `TopBar.rml`'s `<menubar>` of Menus and MenuCommands, the play target, speed and layout menus and the debugger's steps | `commands`, `editor.tabs`, `menubar`, `shell` | Done (W3 C3) |
| Status bar | Authored: `StatusBar.rml` with UI Scale's menu | `editor`, `commands`, `shell` | Done (W3 C3) |
| Area header | `AreaHeader.rml`; editor type menu authored (F3, a Menu over `choices.editors`); editor controls in a native slot | `editor.areas`, `choices` | W3 C3; each panel's controls move with the panel |
| Settings, Shortcuts, Collaboration, NewProject, Dialog, Inspector panel, Data, Theme, the Behavior header's View menu (GraphView, trigger included since F3) | Authored | | Done |
| Properties: Object, Prototype, Scene, Behaviors | Authored (P1): `Properties.rml`, `PropertiesHeader.rml`; `studio_properties_view.c` publishes `properties`, `studio_properties.c` runs its intents | Reflection, ECS reads, overrides and mixed values, `write_field` gestures, ranges and units from `Components.kdl`, live values at 20 Hz in Play, locks, search, Keep | Done; Add Component and the asset fields' pickers are the palette's (C4) |
| Properties: Node, Member | Authored (G1): `Properties.rml`'s graph section and `GraphField.rml` over `graph_details` (`studio_graph_details.c`); `studio_graph_properties.c` and `studio_member_properties.c` write the rows and run their intents | Literal editors, binding autocomplete, expression code, type picker, reorder grips | Done (W4 G1) |
| Outliner | Authored: `Outliner.rml`, `OutlinerHeader.rml` over KDL behaviors (OutlinerRows, OutlinerInput, OutlinerRename, OutlinerMenu, OutlinerHeaderInput) and the `studio` editor nodes (`studio_nodes.h`); no panel source | Selection (⌘/⇧), 10k-row virtualization, drops, rename, eye and lock, menu | Done (W2 P2); behavior-driven (spike) |
| Scene and Canvas headers | Authored: `SceneHeader.rml` over `scene` (`studio_scene.c`), `canvas` (`studio_canvas.c`) and `commands` | Commands, tool, snap, grid, zoom, Show toggles, Canvas size, actions log | Done (W3 C1) |
| Scene viewport | `Scene.rml` and `Canvas.rml` over `scene_view` and `canvas`; the `kit_surface` showing `kit:viewport` stays native in their slot | Texture, pointer input, camera labels | Done (C1): overlays authored; input stays in C |
| Assets | `Assets.rml`, `AssetsHeader.rml` and `NewAsset.rml` over `assets`, `asset_view` and `asset_menus` (`studio_assets.c`, P3) | Index, filters, `kit:thumb:` textures, thumbnail budget, columns, keys, drag, New menu | Done (P3) |
| Output, Find, Calls | Authored (P4): `Output.rml`, `OutputHeader.rml` over `studio_output.c`'s `output` source | | Done |
| Members | `Members.rml` and `MembersHeader.rml`; the `members` source in `studio_members.c` | Members, uses, rename, drag into the graph, menus | Done (P5) |
| Behavior area | `BehaviorHeader.rml` and `GraphFind.rml` (C2); the canvas, `studio_graph*.c`, a second context drawn to `kit:graph` | Nodes, pins, literals, comments, reroutes, wires, live chips | Header, breadcrumbs, debug target and find: done (C2). Add-node search: done (C4). Canvas menus (`GraphMenu.rml`) and the pins' choice and asset pickers: done (G1). Node bodies: postponed |
| Text Editor | `CodeHeader.rml`, `CodeFind.rml` (C2); the surface in `studio_code.c`, `kit_code.c` (Zep) | Buffer, intelligence, Vim | Chrome done (C2); the Zep surface stays a native element |
| Performance | `Performance.rml`, `PerformanceHeader.rml` over `<line-chart>` (C2) | Sample ring | Done (C2) |
| Shell | Authored: `Workspace.rml` over the `workspace` source (`studio_shell.c`); each area a document in its place | Ratios, open states, maximize, the columns' laid-out sizes | Done (W3 C3) |
| Palette, asset pickers, Add Component, refactor popover | Authored (C4): `Palette.rml` and `Picker.rml` over the `palette` source (`studio_palette_view.c`), `Refactor.rml` over `refactor` (`studio_refactor.c`); clients `studio_palette.c`, `studio_pick.c`, Add Component, the add-node search | Ranking (`support/text_rank.h`), recents, prefixes, thumbnails, the window | Done (C4) |
| Kit dropdowns and autocomplete | `kit_command.c` over `support/text_rank.h` | Graph literals, member types, `kit_select` | Done (G1): graph literals, pins' assets, member types and Properties' source fields use Studio's picker; `kit_select` (the Designer's keywords) and the source field on a node body (postponed with node bodies) keep the kit's |
| Popups, menus, tooltips | `src/ui/popover.c` (F3) stacks, places and dismisses authored popovers and the kit's popups and runs menus; `kit_popup.c` emits the kit's | Placement, stacking, dismissal, typeahead | Done for the controller (F3); kit menus move to Menu with their panels; tooltips stay attribute-driven |
| Toasts | `kit_toast.c` | Springs, swipe | Postponed |
| Designer: Hierarchy, Details, Library | Authored (D1): `Hierarchy.rml`, `Details.rml`, `Library.rml` and their headers over the `hierarchy*`, `details*` and `library` sources (`studio_designer_hierarchy.c`, `_details.c`, `_library.c`) | `ui_design` edits, drop zones, CSS value types | Done (W4 D1); the Canvas, its guides and resize handles stay native |
| UI Inspector | Authored: `Inspector.rml`, its tree a List of TreeRows over the `inspector` source's window of every element (`studio_inspect.c`); picking and the highlight native | Element tree walk, selection and pick keys, styles | Done (W4 I1) |

## Architecture

### Panels

A panel is `content/ui/<Panel>.rml` and `.rcss`, its prototype and an entity
in `scenes/Studio.kdl`, as today, plus a C source registered in one table in
`studio_sources.c` (`studio_panel_sources`, F1; `studio_internal.h`):

```c
typedef struct studio_panel_source {
    const char *name;  /* "outliner": its variable and action prefix */
    const char *panel; /* "Outliner": the entity whose showing syncs it; NULL: every refresh */
    void (*add)(yyjson_mut_doc *doc); /* variables exist before the data model */
    bool (*sync)(studio *s, const studio_panel_sync *at); /* true when it changed */
    void (*action)(studio *s, const char *action, const ui_activation *a); /* or NULL */
} studio_panel_source;
```

`studio_panel_sync` gives `sync` the host's doc, the source's variable, what
the source keeps for that host (`studio_sources`), whether bindings wrote
since the last sync, and whether the host is a preview (the Canvas showing
Studio's own ui assets, which applies nothing). `studio_ui_frame` syncs a
source while its panel shows (the last refresh mounted it), at every refresh
when it has no panel, and whenever a binding wrote, so an edit is never lost
to a closing panel; a panel's first mount syncs its sources before it shows,
so it never shows stale data. The host dirties a source's variable only when
`sync` reports a change. Today's sources: `inspector`, `world` (Data),
`theme` (Theme), `settings` (Settings), `shortcuts` (Shortcuts), `editor`,
`commands`, `collaboration`, `new_project` (NewProject) and `choices`. The shell mounts
the panel in each area that shows it, aliasing the area's view state as
`view`. A panel's own header controls move into its document as children of
its `AreaHeader`.

### Data

JSON over yyjson, as today, one top-level variable per panel because RmlUi
dirties by top-level name; rarely changing tables (choices) live apart. C
writes values in place and keeps spare items so the JSON doesn't churn. A row
names its one control in `control`, which the markup builds with
`data-when` (F1; the sketch below shows the earlier one array per control
kind). P1's `properties` is in `studio_properties_view.c`'s header comment:

```
properties: { mode, live, locked, lockTip, query,
  tabs: [{ id, icon, tip, on, sep }],
  identity: { name, prototype, chips: [{ label, count }] },
  panels: [{ key, title, icon, badge, open, menu,
    rows: [{ id: "e12/GameTransform.position", label, about,
             overridden, mixed, readonly, invalid,
             number: [], vector: [{ axes: [{ key, label, value, step, precision, min, max, unit }] }],
             bool: [], choice: [], color: [], text: [], asset: [], entity: [] }] }] }
choices: { fields: { GameBody: { type: TABLE } }, assets: { prototype: TABLE },
  editors: TABLE, panels: { label, icon, tools: [], others: [] } }
TABLE: { label, icon, groups: [{ label, options: [{ value, label, icon }] }] }
Outliner's self (its behaviors' Set UI Data): { root: { key, label, icon, parent, open, depth },
  rootShown, count, total, notice, isolateTip, menu: { label, actor, live, spawnBreak: MenuItem },
  rows: [SceneObjectRow: { key, label, icon, prototype, drop, depth, behaviors, active, marked, hidden, locked, renaming }] }
```

Authoring data never binds two-way. Two-way bindings are for UI-local state
and preferences only (`panel.open`, `view.query`, `save.*`); Properties sends
intents.

### Events

`emit(name, ...args)` passes its arguments as text (F1: `rml_emit_fn` takes
`argc` and `args`; the binding converts and forwards them). Its
`ui_activation` keeps up to `UI_ACTIVATION_ARGS` (4) in `args`, the first as
`value` when it is a number, and the DOM event's `modifiers`, `button`, `x`
and `y` (context pixels), and `anchor`, the sending element as an
`rml_element_ref` for popovers: it reads NULL once the element is gone and
lives until the mount's next take. `studio_panel_route` (`studio_panels.c`)
runs `studio.<command>` as a command, on the tab or area its first argument
names (palette-visible actions stay commands), and `<source>.<action>` as
that panel source's `action`; other names go to the panel's entity
behaviors. New Project's dialog still takes its own `newProject.*` and its
backdrop's bare `Close` until its migration.

Native controls follow the web: `input` while a gesture runs, `change` once on
commit with `ev.value`, `ev.canceled` and `ev.name` (the control's `name`
attribute, as a handler reads `event.target.name`). In Properties, `input` is
a live write with no history and `change` is one Undo step, as
`property_edit` already models. A control repeated by `data-for` needs an
end tag: RmlUi repeats an element's inner markup, which a self-closing tag
lacks. Text fields do so since F1 (`rmlui-input-events.patch`):
`input` (`RML_EVENT_INPUT`) on each edit, `change` once when an edit commits
with Enter or by leaving the field; `data-value` follows `input`, as Vue's
`v-model`, and `data-value-lazy` follows `change`, as `v-model.lazy`. The
kit's fields hear `input`. Both events' propagation can be stopped
(`rmlui-event-propagation.patch`), so a native control keeps its inner
fields' events to itself.

`data-when="expr"` (F1, `rmlui-data-when.patch`, named for Lit's `when` and
behaving as Vue's `v-if`) builds a copy of its element while the expression
is true and removes the copy with its views while false, keeping the element
as a hidden template as `data-for` does; `data-if` hides an element and keeps
its views bound (`v-show`). Use `data-when` for a row's one control of many.

`data-template` (F3 follow-up, `rmlui-data-template.patch`) is the structural
view for content that native code mounts: RmlUi builds none of the element's
content and keeps its markup unparsed in `rmlui-inner-rml`, for
`rml_element_set_inner_rml` to build and to remove with its views. It
cancels the element's other data views, as `data-for` does. `popover.c` uses
it for popovers: see F3.

```html
<number-field data-attr-value="n.value" data-attr-step="n.step"
  data-event-input="emit('properties.input', row.id, ev.value)"
  data-event-change="emit('properties.change', row.id, ev.value, ev.canceled)"></number-field>
```

The backend synthesizes `contextmenu` on a right press, after its
`mousedown`, at the element under the pointer, as browsers do
(`rml_backend.c`, F1). It is a custom RmlUi event with the press's button,
pointer and modifiers, so markup hears it with `data-event-contextmenu`.

### State

| Owner | State |
|---|---|
| C | Selection, active row, anchor; values, overrides, mixed values; history and dirty state; Play and live values; preferences; layout tree; ranking; thumbnails; drag session |
| View | Hover, focus, pressed (pseudo-classes); scroll position, which C reads; panel collapse; filter text; popover open state |
| Behaviors | Focus moves in modal content (NavKeys, MenuKeys, Dismiss). They fire a frame late, so editor panels never depend on them for data |

### Native elements (`src/ui`, C over `rml_element_hooks`)

| Element | Replaces | Contract |
|---|---|---|
| `<number-field>` | `kit_number.c` (done, F2: `src/ui/controls/number_field.c`; the parser is `src/support/number_expr.c`) | `value min max step precision integer float slider unit addon mixed readonly disabled invalid`; `input`/`change`; today's scrub threshold, Alt fine, Shift coarse, Ctrl snap, steppers, Tab; Escape cancels typing or a scrub; repaints text without layout |
| `<color-field>` | `kit_color` (done, F2) | `value mixed readonly disabled`; opens its own picker as `<input type=color>` opens the OS one: `input` while open, `change` on close. Its picker is inside the field (`position: fixed; clip: none`), stacked, placed and dismissed by `popover.c` (F3), so it escapes its area |
| `<choice-list>` | `kit_select`'s list (F3; the kit's still runs its own) and the palette's (C4) | `[role=option]` descendants with `value`, `data-label` or `.label`, `data-keywords`; its first `<input>` filters them by `text_search_score` (`data-filtered`, `data-empty`); Up, Down, PageUp, PageDown, Home, End, cmdk's Ctrl+N/P/J/K and the pointer move `data-highlighted`; Enter or a click sends `change` with the option's value. `virtualized` (C4): the app filters, orders and windows the options (`aria-posinset` each, `aria-setsize` and `highlight` on the list) and hears `highlight` (`ev.value`, `ev.pointer`) as keys and the pointer move it |
| `<splitter>` | `kit_splitter` (done, F2; the kit's wrapper went with C3) | `axis min max value invert`; `input`/`change`; cursor |
| `<live-text>` | `kit_live_label` (done, F2) | `value`; glyphs update without layout |
| `<line-chart>` | `kit_chart.c` (done, C2: `src/ui/controls/line_chart.c`) | `series` names the series C publishes with `line_chart_publish` (times, values, color, `now`), as textures are named `kit:...`; `window unit precision reference min max fill live`; parts `.chart-tick`, `.chart-badge`, `.chart-hover`; a publish redraws the charts naming it without layout |
| Existing | | `<img src="kit:...">`, `<number-flow>`, `<morph-text>`, `presence` |

The kit's number, color, drag and popup calls become thin wrappers
over these, so native and authored panels share one implementation, one popup
stack and one drag session while the migration runs.

### Lists, drag, focus, popovers

- **Virtualization (F4, done):** the authored `List` widget (a scroller and a
  spacer holding the rows) and `ui_list_window(...)` (`src/ui/list.c`), a
  port of `kit_list.c` that works on any scroller; `kit_list` is a wrapper
  over it. The panel source finds its List by id, calls `ui_list_window` at
  sync, which reads `scrollTop`, sizes the spacer and sets its padding, and
  fills the window's rows in the same frame; the List's
  `data-event-scroll` emit wakes the source. Row height comes from `--row`.
  The window keeps its length as the list scrolls, so `data-for` reuses its
  elements.
- **Drag (F4, done):** `src/ui/drag.c`, one drag for every context.
  Sources use `data-drag` and `data-drag-payload`, targets `data-drop` (with
  `data-drop-zones="tree"` giving before/inside/after by quarters, `"list"`
  before/after by halves); the marks are `data-dragging`,
  `data-drop-accept`, `data-drop-over` and `data-drop-zone`; a bubbling
  `drop` event carries `ev.type`, `ev.payload` and `ev.zone`. C asks
  `ui_drag_dropped(...)` for drops on surfaces such as the viewport and
  graph. It hears each context's root first, as events capture (an RmlUi
  plugin names new contexts, `rml_add_context_hooks`), so it spans the
  area documents. The kit's `kit_drag_source`, `kit_drop_target`,
  `kit_drop_hover` and `kit_drag_hint` set and read the same attributes.
- **Keys:** area keys stay in C (`studio_shortcuts`, editors' `*_key`); widget
  keys are the element's; modal content uses `nav`/`tabindex` and behaviors.
  Rename in place uses `<input autofocus data-value-lazy>`.
- **Popovers (F3, done):** `src/ui/popover.c` gives the HTML popover
  attribute's behavior: content stays in its document, shows above all with
  `position: fixed; clip: none`, anchors to its target or a point, closes on
  an outside press or Escape and returns focus. `role="menu"` adds highlight,
  typeahead and submenu delay; `<menubar>` switches menus on hover. Widgets:
  Menu, MenuItem, MenuGroup, MenuSeparator, Submenu, MenuCommand and
  ChoiceList (palette, pickers, Add Component, editor types, debug target),
  with EnumField and AssetField over ChoiceList. A closed popover is
  `display: none`, so nothing in it lays out or draws. One marked
  `data-template` (Menu and Submenu) also holds no elements and no data
  views: it builds its content when it opens and removes it after its exit
  transition (F3 follow-up, below).

### Live values in Play

The host refreshes the UI at 20 Hz while playing and 4 Hz idle. C updates
values in place and dirties `properties` once per refresh; `<number-field>`
repaints without layout. S0 measured 0.19 ms native and 0.32 ms web for the
Object tab's 166 views per refresh, under 1 ms, so fields need no live
sources from a C registry.

### While both systems run

`kit_slot` keeps native content inside authored panels until its wave. Tests
find authored elements by selector (`studio_find(s, area, selector, &rect)`),
and each wave retargets its tests from kit paths.

## Behavior-driven panels

A panel's markup and styles should work with KDL behaviors on its prototype,
as a game's UI does, not with a C panel source: C keeps capabilities, which
behaviors call as nodes, and the native pieces that need native speed. The
Outliner is the spike that proves it (branch of `editor-polish-design`,
2026-10-03); its numbers are below.

**References.** Unity's editor windows load UXML and USS, and a C#
controller queries elements, registers callbacks and calls editor APIs
(`Selection`, `Undo`); its `ListView` virtualizes natively, and windows hear
`EditorApplication.hierarchyChanged`, `Selection.selectionChanged` and
`OnInspectorUpdate` (10 Hz). Unreal's Editor Utility Widgets are UMG layouts
whose Blueprint graphs call editor-only nodes (the Editor Actor Subsystem's
Get and Set Selected Level Actors, Set Actor Label) that exist only in
editor builds; its `ListView` and `TreeView` virtualize natively. Unreal's
own Scene Outliner stays Slate C++. **Choice: Unreal's Editor Utility
Widget**: a document (UMG) and behavior graphs (Blueprint) over an
editor-only node pack, with Unity's editor events (`Refresh` as
`OnInspectorUpdate`, `RevealObject` as `PingObject`) and native list
windowing (Virtuoso's, `ListView`'s).

**The editor node pack** (`src/editor/studio/studio_nodes.h`): the `studio`
pack, registered only in the editor targets' registry (`EDITOR_REGISTRY`:
chibi, chibi_ctl, the UI bench, test_suite), so games' runtimes never list
it. Its nodes run on the `studio` service, Studio and one call
(`studio_node_run`, a switch in `studio_node_calls.c`), which only Studio's
own world binds (`game_behavior_bind_service`); elsewhere they fail with
"Editor nodes require Studio". searchNodes offers them (category Studio)
only in the Studio project. They are named for capabilities and name
objects by key, the entity as text, as rows carry it:

| Kind | Nodes |
|---|---|
| Reads | Scene Info (title, objects, filter), Selection (active, count, name, prototype), Object Selected, Object Row, Renaming Object, Editor State (editable, live, mac), Breaks When Spawned |
| Queries, impure: they run once as they execute, as Unreal's Get All Actors is impure, where a pure read runs again for each node that takes it | Scene Objects (a window of `SceneObjectRow`s of the objects the filter shows, and how many it shows), Prototype Choices (`Choice`s for a ChoiceList) |
| Effects | Select Object (Toggle, Extend), Rename Object (one Undo step), End Rename, Set Object Hidden, Set Object Locked, Isolate Object, Add Object, Drop on Object, Set Object Filter, Toggle Spawn Break |

Menus keep running Studio's commands through MenuCommand
(`emit('studio.<id>')`), so no Run Command node was needed.

**Data.** Behaviors publish with Set UI Data on `self`; an array of structs
becomes a list of objects, and a value that did not change changes nothing.
Each entity's data is now its own data variable, `e<entity>`, bound when a
document binds `self` (`rml_data_model_bind`): before, every entity shared
`entities`, so any Set UI Data re-evaluated every `self` view in Studio. A
panel's own data is one variable: the Outliner's selection travels in its
rows (`active`, `marked`), so a click re-evaluates the window's rows (607
views, in the frame of the click), where the C panel's `outliner` and
`outliner_ui` split read 624 over the two frames it took. A hot value that
must stay apart goes on an entity of its own, as OutlinerHeader holds the
Add list.

**Events.** Studio runs a panel's UI events on its entity's behaviors in the
frame that sent them (`dispatch` in `studio_ui.c`, over
`game_behavior_dispatch`), then `Refresh` when it is due (next paragraph),
as a source syncs, then the events behaviors sent each other (Send Event),
shows what they set and applies their UI effects before RmlUi lays the
frame out; an effect that sends events of its own (a focus move blurs a
field, which commits it) runs in up to four more rounds of the same frame. An event asset declares the
payload: `ui_activation_payload` fills Shift, Alt, Mod (Command on a Mac,
Control elsewhere) and Pointer (`CF_V2`, the document's pixels) from the
DOM event, as a web handler reads `event.shiftKey` and `event.clientX`, and
the rest from emit's arguments in order, read as each value's type. Studio
sends `RevealObject` to every panel behavior after Up, Down, a Find jump or
F2. `data-area-keys="Left:FoldRoot"` gives a panel the unmodified keys of
its area, Blender's per-editor keymap. Events in a header's controls now
reach the controls' panel, the innermost mount, not the AreaHeader. Games
still queue UI events to the next tick.

**Refresh is change-driven.** `Refresh` is where a panel's behaviors query
and publish, as a source's sync does, and it runs only when something the
panel depends on changed, with no plumbing in the behaviors. Studio keeps one
coarse editor revision for what the editor nodes read. Its stamp
(`studio_nodes_stamp`, in `studio_node_calls.c` beside the reads it stands
for) mixes counters their owners keep and none walks objects: the asset
index's generation, which every scene edit, rename, add, delete, behavior
attach, repoint and prototype edit invalidates, and its revision; the
history's Undo and Redo heads; the object and prototype counts; the
selection (`selection_revision`, which `scene_selection_track` counts in each
frame that begins with another selection or active object); the view store's
revision (hidden and locked); editable and live; the rename in place; the
object filter; the scene's path; and the spawn breaks. `studio_ui_tick`
takes it every frame; when it moved the revision grows and a UI frame is due,
so a change nobody sent an event for (an Undo, a remote edit) shows by the
next frame.

A panel's entity refreshes in `show_panel` when its revision is behind, when
its document is new to it (the first mount, another document in its host) and
after any UI event its mount sent in that frame's rounds, `window` events
among them, so a scroll refreshes and a click shows its effect in its own
frame. `studio_ui_send` (RevealObject) and a reloaded behavior make every
panel due. Events behaviors send each other (Send Event) do not: a behavior
publishes in the handler of the event that changed its state. A node that
reads something new adds its counter to the stamp; one that reads values
changing while the scene plays (none yet) adds a tick at the live rate, so
Play refreshes a panel only when what it reads moved (the stamp held across
120 Play frames of the Sandbox; Play and Stop move it). Tests, one per
thing the Outliner shows (`refresh_follows_changes`, `stamp_follows_reads`,
`--studio-outliner`): a forced idle UI frame runs no Refresh; a name Properties commits and its Undo, a selection the Scene
makes, Hide in Editor, an added object, the filter Studio sets, an asset the
project gains and Play and Stop each reach the rows or Add; the stamp moves
with the selection, a spawn break, a rename in place, an attached behavior,
Undo and Play. A row added or a filter cleared reaches the List's rows one
frame after, when the List has laid out its new length, as before.

**What stays native.** List windowing: a List marked
`data-list-rows="self.total"` is windowed by its host (`ui_list_window` in
`ui_mount_measure`), which sends `window` (`ev.first`, `ev.count`), as
Virtuoso's `rangeChanged`; Reveal UI List Row is its `scrollToIndex`.
Filtering ten thousand objects, drop types, selection ranges, rename,
history and view flags stay C behind nodes; popovers (Show UI Popover),
drag (F4) and the Scene's drop preview over an element marked
`data-object` stay in the host.

**The Outliner's behaviors** (`projects/studio/content/behaviors`), on the
Outliner and OutlinerHeader prototypes and their `Studio.kdl` entities:

| Behavior | Events | What it does | Nodes, edges |
|---|---|---|---|
| OutlinerRows | Begin, ShowRows, Refresh, FoldRoot, UnfoldRoot, ToggleRoot, RootClicked, RevealObject | The root row, the window's rows, the count, the notice; folds the root (a twisty click is the only one that flips the state its row sends); unfolds and scrolls a revealed object into view | 59, 65 |
| OutlinerInput | RowClicked, RowMenu, EyeClicked, LockClicked, RowDropped, ListDropped, SpawnBreak | Selects (Mod toggles, Shift extends), keeps a selection a right click lands in and opens the menu at the pointer, hides or isolates, locks, drops | 24, 35 |
| OutlinerRename | RenameCommitted, RenameCancel, RenameEnter, RenameLeft, Refresh | Commits only while its row still renames (Escape ends it first), returns the keyboard to the list, closes a field left without a commit at the refresh after its events | 20, 20 |
| OutlinerMenu | Begin, Refresh | The menu's heading, Break When Spawned's check, the Live items, the eye's Alt+click tip | 26, 30 |
| OutlinerHeaderInput | Refresh, FilterTyped, AddObject | The filter (Set Object Filter, shown back when Studio clears it), Add's prototypes and editability | 11, 10 |

The UI events with payloads are event assets in `content/events`
(RowClicked, RowMenu, EyeClicked, LockClicked, RowDropped, ListDropped,
RenameCommitted, RenameLeft, FilterTyped, AddObject, RootClicked,
RevealObject, Refresh); FoldRoot and the others without one are local.
The graphs were written through the native controller's `edit_graph`
(buildGraph's operation) on this worktree, auto-arranged, then
`optimizeGraph` and a comment pass.

**Gaps closed, in shared code:** per-entity `self` variables (render/ui.c,
rml.cpp); same-frame UI events with typed payloads, Refresh, settling and
effect rounds (studio_ui.c, render/ui.c's `ui_activation_payload` and
`ui_mount_pending`); native list windows with `window` events and one-shot
reveal (render/ui.c); Show UI Popover and Reveal UI List Row (game.ui); area
keys (`ui_mount_area_key`); events reaching the innermost mount
(studio_ui.c); host services for behavior worlds (`game_behavior_bind_service`);
Text Equal (expressions cannot compare text); flow nodes' outputs in the
run's arena (`flow_context_scratch`).

**Found, not fixed (tooling):** edit_graph fails with no error (`ok:false`)
when a batch adds a second Behavior Expression with the same source; a
`connect` from a bool to a number input is dropped without a diagnostic;
`add_variable_node` ignores `inputs` (use `set_input`); a `connect` from an
event's `CF_V2` payload to a `CF_V2` input chose a GameTransform-to-CF_V2
conversion (`game_transform_to_cf`), which ran right only because a
GameTransform starts with its position (removed by hand); a bool cannot feed
arithmetic or Select (no bool to number conversion), so folds are Branches.
Studio's Add menu still lists the studio pack in every project.

**Results** (`--ui-bench studio`, case 23, `UI_BENCH_OUTLINER=1`): the
Outliner over ten thousand objects, scrolled to the middle, each step
through the UI as a hand would, until it shows; release builds of the base
(`280dd327`) and the spike alternated process by process three times at a
1-minute load of 7.8-8.4 (provisional; counts hold), whole Studio UI
frames, medians:

| Step | Base, C source (ms) | Spike, behaviors (ms) | Spike ÷ base | UI frames to show it | Views | Layouts | Behaviors (ms) |
|---|---:|---:|---:|---|---:|---:|---:|
| A UI frame with nothing changed | 1.007 / 0.891 / 0.913 | 1.141 / 1.262 / 1.148 | 1.26× | 1 / 1 | 0 / 0 | 0 / 0 | 0.19 |
| Scroll step of 3 rows | 3.564 / 3.538 / 3.469 | 3.820 / 3.825 / 3.710 | 1.08× | 1 / 1 | 607 / 607 | 2 / 2 | 0.21 |
| Click a row in view | 9.967 / 10.311 / 10.178 | 4.656 / 4.639 / 4.660 | 0.46× | 2 / 1 | 624 / 607 | 8 / 2 | 0.25 |
| Rename in place: Enter until the row shows it | 4.716 / 4.569 / 4.533 | 4.941 / 4.852 / 5.053 | 1.07× | 1 / 1 | 1,189 / 1,190 | 2 / 2 | 0.40 |
| A filter keystroke, "Marker 5" to "Marker 51" | 4.699 / 4.918 / 4.650 | 5.198 / 5.183 / 5.343 | 1.10× | 1 / 1 | 607 / 653 | 2 / 2 | 0.89 |

Behavior time is `game_behavior_profile`'s inclusive time over the step's
frames. The C source shows a click's selection a frame late (it syncs
before the click's intent runs), so its click takes two UI frames; the
behaviors show it in the frame of the click. Every refresh re-runs Scene
Objects, a walk of every object through the filter (as the C source's sync
did), and the five behaviors' Refresh: 0.19 ms idle, 0.89 ms when the
filter changes what the walk matches. Scroll steps read 1,735 lookups to the
C panel's 2,831. Scene Objects was first a pure read, which the
interpreter runs again for each node taking it (four walks a refresh, 0.30
ms idle and 3.0 ms per keystroke); as a query it runs once.

**Change-driven Refresh** (fx-refresh, release builds of `168677b4`, the
base, and this branch, alternated three times at a 1-minute load of 12-13,
so ratios, not milliseconds, hold; whole Studio UI frames, medians):

| Step | Base, C source (ms) | Behaviors (ms) | Ratio | Views | Lookups | Layouts | Behavior time, runs |
|---|---:|---:|---:|---:|---:|---:|---|
| A UI frame with nothing changed | 1.071 / 1.021 / 0.977 | 0.906 / 0.962 / 0.893 | 0.89× | 0 | 0 | 0 | 0 ms, 0 (was 0.19 ms, 4 Refresh events) |
| Scroll step of 3 rows | 3.462 / 3.511 / 3.422 | 3.418 / 3.678 / 3.622 | 1.05× | 607 | 1,735 (base 2,831) | 2 | 0.16 ms, 4 |
| Click a row in view | 10.259 / 10.643 / 10.179 | 4.722 / 4.848 / 4.632 | 0.46× | 607 | 1,735 | 2 | 0.22 ms, 4 |
| Rename in place | 4.438 / 4.713 / 4.601 | 4.753 / 4.842 / 4.876 | 1.05× | 1,190 | 3,404 | 2 | 0.35 ms, 9 |
| A filter keystroke | 4.958 / 4.557 / 4.819 | 4.953 / 5.094 / 5.146 | 1.06× | 653 | 1,882 | 2 | 0.92 ms, 6 |

The idle Outliner is now below the C source (0.89×) and runs no behavior;
the other steps hold their spike ratios, their Refresh being due by the
event or the list window that sent it. The idle frame also lost a cost every
panel paid since the spike: `preview_drop` queried each mount's DOM for
`[data-drop-over][data-object]` every frame, and now asks `ui_drag_active`
first (a mount's `show_panel` fell from 64 to 45 µs for Properties and
Outliner, 25-30 µs in the base). What remains above the base in `show_panel`
is `ui_mount_measure`: since the spike it also runs `window_lists`, a
selector query for `[data-list-rows]` in every mount every frame, beside the
older `[id][data-event-scroll]` one. On the idle frame over fourteen mounts
the two cost 123 µs (the Outliner's `ui_list_window` among it) and 51 µs, of
0.9 ms. A mount whose expanded markup holds neither attribute needs neither
query: search the text once when it loads (shared code in `render/ui.c`,
left to its owner).

**Parity:** native shots of the rows (a selection of two, a hidden and a
locked object, a hovered row), the row menu and rename in place, dark and
light, compact and comfortable (`UI_BENCH_OUTLINER_CAPTURE`, release builds
of both), are identical at a 2% fuzz but for the caret's blink; the web
build shows the same in both themes and densities. `--studio-outliner`,
`--studio-documents`, `--studio-panels`, `--studio-lists`,
`--scene-documents` and `make test` pass unchanged but for three
checks that read state the behaviors now own: the root's fold is read from
the rows shown, rows are found by `data-object`, and a game's preview binds
`e<entity>` variables in place of `entities`.

**Authoring cost:** the Outliner's C went from 800 lines to 224
(`studio_outliner.c`: mounting, rename, Hide in Editor, ranges), and its
three panel sources left the table. The behaviors are about 140 nodes and
160 edges (1,014 lines of KDL, which tools write), plus 13 event assets
(148 lines). The node pack is 796 lines of C (`studio_nodes.c` 407,
`studio_node_calls.c` 300, the header 89) that other panels reuse; the
shared gaps are about 450.

**Recommendation.**
- Convert next, with nodes of their own capability: Members (member rows,
  select, rename, references; the Outliner's shape), Output (a log query
  and filters; rows are already a native window), Assets' header and New
  menu, and the header-only panels (SceneHeader, CodeHeader,
  PerformanceHeader, BehaviorHeader's controls), whose state is mostly
  `commands` and whose few intents become nodes.
- Need more first: Properties (reflection rows and `write_field` gestures:
  keep its C view model as a query node, `Object Fields`, and move only
  its UI logic), the Palette and pickers (ranking stays native: a `Rank`
  query), Assets' grid (thumbnails native, a query for the window).
- Stay native: the graph canvas, Zep, the Scene viewport and gizmos,
  ranking, thumbnails, list windowing, the Inspector's DOM walk.
- Before more panels: Studio's Add menu gating, a bool Select or
  conversion, and the controller fixes above (change-driven Refresh is done:
  a panel's nodes add their counters to `studio_nodes_stamp`; sources
  follow the shared follow-up below).
- Open: two areas showing the Outliner share one entity and window (the
  Inspector's limit too); the Canvas preview of Studio's own Outliner shows
  no rows, as nothing runs its behaviors there.

## Performance

What RmlUi 6.3 and our binding do (checked in source):

- Dirtying is per top-level variable; every view bound to it re-evaluates.
- Our JSON bridge looks up array items in constant time
  (`src/ui/json_items.c`, since S0; a dirty variable relists arrays) and
  object members linearly. Before S0 an unwindowed list of n rows cost
  about n²/2 item steps per dirty.
- `data-for` builds new items with a parse and matches by index.
- `data-if` hides and keeps views (Vue's `v-show`); `data-when` (F1)
  removes its copy and its views (`v-if`), and builds it again with a parse.
- Any dirty layout re-lays out the whole document. Since F5 each area, the
  top bar, the status bar and the overlay layer is a document of its own,
  so a change lays out only the one it is in.
- Same-value style writes lay nothing out, and live labels avoid layout
  (existing patches).

Authored panels have one structural advantage: the kit re-emits the whole
editor on every frame with UI work, while an authored panel works only when
its variable is dirty.

Estimates, measured by S0 where it could (S0 results; about 0.4 µs per view
natively and 1.3-1.7× that on web, provisionally):

| Surface | Views | Updates | Risk |
|---|---|---|---|
| Properties, Object tab (the Player: 4 panels, 14 rows, 309 elements) | Measured: 340 per selection, 166 per refresh; authored (P1): 2,733 per selection, 20 per value refresh | Selection rebuilds rows; edits dirty once; Play refreshes at 20 Hz | Low for views (0.19 ms native, 0.32 ms web per refresh); each text change lays out its document; selection 1.15× native (P1 results) |
| UI Inspector (10k elements, 19-row window) | Measured: 308 per selection, 494 per scroll step; the old 1,000-row panel 17,176 per selection | Selection, pick, scroll | Low (4.2-4.8 ms idle, 6.0-7.1 ms per selection, 0.2-0.45× the old panel; I1 results) |
| Outliner (10k objects, 60-row window) | Measured: 362 per scroll step; in Studio (P2), 518 per scroll step or selection over a 22-row window | Scroll, selection | Low (0.69 ms native, 0.91 ms web per step; 0.93× the kit's frame, P2 results) |
| Assets (8×8 tiles) | ~400 | Scroll, thumbnails ready | Low |
| Output following a log | Measured: 40 markup parses per batch | Logging | Medium (1.3 ms native, 1.9 ms web; 6× plain text) |
| Members (<300 rows) | Measured: 284 per selection, 815 per scroll step or filter over a 28-row window | Selection, scroll, filter | Low; a scroll step 1.2× native until `data-for` is keyed (P5 results) |
| Shell and headers | ~400 | Command state | Low |
| Palette (50-item window) | ~300 | Each keystroke | Low |
| Graph node bodies (300 nodes) | 5-10k+ | Node drags each frame, chips at 10 Hz | High: postponed |

### S0: measure first

`test/editor/ui_bench.c` (headless, native and as WASM under Node) and
`test/editor/ui_bench_studio.c` (native Studio in a hidden window):

1. Full layout of today's kit document on Sandbox at 1440×900, both densities.
2. Dirty cost of one variable with 300 and 3,000 views, one value changed and all changed.
3. Instancing a `data-for` from 0 to 50 `PropertyRow` items.
4. One scroll step of a 60-row window over 10k rows.
5. An authored prototype of the Properties Object tab against the native one.
6. `data-rml` for 40 Output rows.
7. Whether `clip: none` popovers escape the areas' clipping and stacking.

| Measurement | Threshold | If exceeded |
|---|---|---|
| Dirty of 3,000 views | > 1 ms | Split variables or use live sources |
| Full layout | > 2 ms native, > 4 ms web | Layout containment, or one RmlUi document per area, becomes a prerequisite |
| Instancing 50 rows | > 4 ms | Stable window lengths, pooled rows |
| JSON array lookup | | Fix regardless (O(1)) |

### S0 results

**Provisional: re-measure on an idle machine.** S0 ran while five or more
agents built and drove browsers on the same machine; each table gives the
1-minute load average. Counts don't depend on load. Ratios come from two
arms alternated run by run in one process, or process by process for the
lookup fix. Absolute times decide nothing until they're re-measured with:

- Native: `make build-release BUILD_TARGET=test_suite`, then
  `./build-release/test_suite --ui-bench` from the repository root. Add
  `headless` or `studio` to run one half; `studio` opens a hidden window.
- Web: `make web-configure`, `cmake --build build-web --target ui_bench`,
  then `node build-web/ui_bench.js` from the repository root (the headless
  half as WASM under Node).
- Web Studio: a web build with `-DCHIBI_WEB_PROFILING=ON` (as
  `make web-profile` makes) served at 1440×900, DPR 2. Read the User Timing
  measures `Rml retained update`, `Studio host emit` and
  `Rml retained capture`/`output` after each 1 px viewport resize (full
  layout) or Outliner click (selection).
- Lookup A/B: build once with `WALKED_ITEMS` in `src/ui/json_items.c` set to
  `SIZE_MAX` (every lookup walks), keep that binary, rebuild, and alternate
  the two processes.

Each case runs 41 times (Studio cases 31) after 3 warm-ups; times are the
median and the 25th-75th percentile in ms. Headless documents each have a
context of their own and Studio's theme (the StudioShell page, compact).
Counts per run: values views evaluated and members and items looked up
(`rml_data_model_reads`), layouts of the measured document (a probe
element's `layout` hook) and elements in it. The spike is
`projects/studio/content/ui/spike/PropertiesSpike.rml`, which Studio never
shows. It is driven from reflection: the Player's Object-tab components,
with Transform as one Position row as native Properties shows it.

Headless, load 21 (native) and 19 (web):

| # | Case | Native | Web | Views | Lookups | Layouts | Elements |
|---|---|---:|---:|---:|---:|---:|---:|
| 2 | 300 text views, one value changed | 0.178 (0.175-0.182) | 0.309 | 300 | 901 | 1 | 302 |
| 2 | 300 text views, all changed | 0.183 | 0.331 | 300 | 901 | 1 | 302 |
| 2 | 3,000 text views, one value changed | 2.01 (1.81-2.25) | 3.26 | 3,000 | 9,001 | 1 | 3,002 |
| 2 | 3,000 text views, all changed | 2.10 | 3.36 | 3,000 | 9,001 | 1 | 3,002 |
| 2 | 300 attribute views, one value changed | 0.120 | 0.160 | 300 | 901 | 0 | 602 |
| 2 | 3,000 attribute views, one value changed | 1.21 (1.18-1.27) | 1.69 | 3,000 | 9,001 | 0 | 6,002 |
| 2 | 3,000 attribute views, all changed | 1.38 | 1.83 | 3,000 | 9,001 | 0 | 6,002 |
| 3 | 50 property rows instanced (0 to 50) | 7.56 (7.35-7.85) | 9.90 | 710 | 6,736 | 1 | +950 (19 per row) |
| 3 | 50 property rows removed | 1.53 | 1.77 | 20 | 66 | 1 | 27 |
| 4 | Scroll step, 60-row window over 10k rows | 0.689 (0.675-0.738) | 0.907 | 362 | 1,083 | 1 | 305 |
| 5 | Spike: select the Player (14 rows) | 1.66 | 2.45 | 340 | 2,648 | 1 | 309 |
| 5 | Spike: select a Swarmer (13 rows) | 1.50 | 2.15 | 324 | 2,526 | 1 | 288 |
| 5 | Spike: edit one shown field | 0.193 | 0.318 | 166 | 1,281 | 1 | 309 |
| 5 | Spike: Play refresh, 27 floats changed | 0.193 | 0.318 | 166 | 1,281 | 1 | 309 |
| 6 | 40 Output rows, highlighted `data-rml`, all new | 1.29 (1.24-1.38) | 1.86 | 40 | 121 | 1 | 603 |
| 6 | 40 Output rows, plain text, all new | 0.215 | 0.315 | 40 | 121 | 1 | 83 |

The O(1) item lookups (`src/ui/json_items.c`), as 9 alternating process
pairs, load 18-30:

| Case | Native, linear to O(1) | Web, linear to O(1) |
|---|---|---|
| 300 text views | 0.207 to 0.175 (0.85×) | 0.385 to 0.310 (0.81×) |
| 3,000 text views | 9.88 to 2.24 (0.23×) | 13.47 to 3.37 (0.25×) |
| 300 attribute views | 0.152 to 0.118 (0.78×) | 0.224 to 0.163 (0.73×) |
| 3,000 attribute views | 9.33 to 1.50 (0.16×) | 11.73 to 1.68 (0.14×) |
| Scroll step, 60-row window | 0.718 to 0.701 (0.98×) | 0.932 to 0.916 (0.98×) |
| 50 rows instanced | 7.95 to 7.71 (0.97×) | 9.50 to 9.63 (1.01×) |
| Spike refresh | 0.195 to 0.195 (1.00×) | 0.308 to 0.305 (0.99×) |

Studio over the Sandbox at 1440×900 (native: hidden window, pixel scale 2,
load 21; web: headless Chrome, DPR 2, load 28). The Properties area is
437×451 dp, showing the Player's Object tab (4 panels, 14 rows). Native
and spike arms alternate run by run after the same change; phases are sync,
emit, update (data views, styles, layout) and capture:

| # | Case | Native kit | Authored spike | Spike ÷ native |
|---|---|---|---|---:|
| 1 | Full layout (1 px resize), compact | 3.31 (3.18-3.45); web 5.2 (5.0-5.3); 704 elements, 1 document | | |
| 1 | Full layout, comfortable | 3.17 (3.09-3.30) | | |
| 5 | UI frame, nothing changed | 1.36: 0.11 / 0.28 / 0.49 / 0.43 | Nothing to do | |
| 5 | Select the Player or a Swarmer | 10.32: 0.10 / 0.28 / 5.97 / 3.92; 1 layout of 1,064 elements; web, the Player or the Camera: 0.2 / 0.7 / 7.5 / 2.2 | 3.49: 0.01 / - / 2.08 / 1.43; 340 views, 1 layout of 309 elements | 0.34× |
| 5 | Edit one shown field | 1.86 (1.06-9.41): 0.11 / 0.27 / 0.50 / 0.76; 0 layouts | 1.40: 0.01 / - / 0.47 / 0.91; 166 views, 1 layout | 0.75× |
| 5 | Play refresh, 27 floats changed | 1.18: 0.12 / 0.31 / 0.41 / 0.32; 0 layouts | 1.60: 0.01 / - / 0.50 / 1.09; 166 views, 1 layout | 1.36× |

The spike lays out only its own document, as one document per area would;
in today's single document each of its layouts would be Studio's full
layout. Its capture repaints a context of its own, which one document per
area would not have, so that column doesn't carry over.

Popovers, rendered natively and read back (web runs the same RmlUi clipping
code but wasn't rendered). Two areas like Studio's (relative, rounded,
`overflow: hidden`, a scrolling panel inside); a `position: fixed;
z-index: 10` popover in the first reaches over the second and below both:

| Variant | Inside its area | Over the next area | Outside both |
|---|---|---|---|
| `clip: none` | Popover | Popover | Popover |
| No `clip: none` | Popover | Next area | Clipped |
| `clip: none`, next area `z-index: 1` | Popover | Popover | Popover |
| `clip: none`, its area `z-index: 0` | Popover | Next area | Popover |
| `clip: none`, its area `transform` | Popover | Next area | Popover |
| `clip: none`, its area `filter` | Popover | Next area | Clipped |

Decisions, provisional until re-measured:

| Measurement | Threshold | Native / web | Decision |
|---|---|---|---|
| Dirty of 3,000 views | > 1 ms | 2.0 / 3.3 ms as text (with one layout), 1.2 / 1.7 ms as attributes | Exceeded: keep each variable well under 2,000 views (one per panel, windowed lists, choices apart). Live sources aren't needed |
| Full layout | > 2 ms native, > 4 ms web | 3.3 / 5.2 ms | Exceeded: F5 becomes a prerequisite of the panel waves |
| Instancing 50 rows | > 4 ms | 7.6 / 9.9 ms | Exceeded: stable window lengths, pooled rows, lean row markup |
| JSON array lookup | Fix regardless | 4-7× faster at 3,000 views; no change at 60 rows or fewer | Done in S0 |

What the numbers confirm or change:

- **F5 moves from optional to required** (provisionally), as one document
  per area. Every authored text change lays out its whole document (one
  layout per dirty in every text case), and Studio's document takes 3.3 ms
  native and 5.2 ms web. A native selection change already costs 10.3 ms,
  6.0 of it update; the spike's own document takes 2.1. One document per
  area needs no RmlUi patch; containment would. It makes the cut Unreal's
  invalidation panels and Godot's per-Control layout make at a subtree.
- **F2's `<number-field>` and `<live-text>` must land before P1.** Kit
  number fields repaint without layout (0 layouts per edit and refresh);
  the spike's text bindings lay out once per change. Changes that touch no
  layout cost 0.40 µs per view natively (3,000 attribute views: 1.2 ms).
- **Live sources: not needed.** A Properties Play refresh evaluates 166
  views in 0.19 ms native and 0.32 ms web, under the 1 ms gate. Values
  change in place and `properties` is dirtied once, as planned.
- **Lists need pooled rows.** Instancing costs about 150 µs per 19-element
  row natively; keep `data-for` lengths stable and rows lean. A 60-row
  window's scroll step meets the 1 ms gate (0.69 ms native, 0.91 ms web),
  layout included.
- **P4 Output: write each new line into one recycled row.** `data-rml`
  costs 6× plain text, and `data-for` matches by index, so a log that
  shifts its rows re-parses all 40. Rows ordered by position, with only new
  lines rewritten, parse one row per line.
- **F3 popovers: `position: fixed; clip: none` works** in today's areas.
  It escapes the areas' scissor and stencil clip masks and stacks above
  later areas, even one with `z-index: 1`. It breaks once an area forms a
  stacking context: with a `z-index`, `transform` or `filter` the popover
  falls under later areas, and a `filter` also clips it. Areas keep those
  off (no spring may animate an area by `transform`); otherwise the
  teleport fallback is needed.
- **Web costs 1.3-1.7× native**, and 1.6× for Studio's full layout, not
  2-4×.
- The kit's idle UI frame costs 1.4 ms at each 4 Hz or 20 Hz refresh
  (emit 0.3, update 0.5, capture 0.4). An authored panel with nothing dirty
  costs nothing.

### S0 re-measured

On 2026-10-03 at 03:04, with the agents finished (1-minute load 8-12, most
of it the release build that had just run; p25-p75 spreads within a few
percent), `./build-release/test_suite --ui-bench` gave, natively:

| Case | Median ms | Threshold | Decision |
|---|---:|---:|---|
| Full layout of Studio, compact / comfortable | 3.96 / 3.84 | 2 | One RmlUi document per area (F5) is required |
| 3,000 text views, one value changed | 1.75 | 1 | One variable per panel, hot values apart |
| 50 property rows instanced | 6.87 | 4 | Pooled rows, stable window lengths |
| Scroll step, 60-row window over 10k rows | 0.69 | | Fine |
| 3,000 items looked up: walk / `json_items` | 3.65 / 0.01 | | The O(1) fix holds |
| 40 Output rows as `data-rml` / plain text | 1.21 / 0.21 | | Output rewrites only new rows |

Properties against the native kit, alternated run by run in Studio:

| Case | Native | Authored | Ratio |
|---|---:|---:|---:|
| Select another object | 8.15 | 3.29 | 0.40× |
| Edit one field | 1.42 | 1.32 | 0.93× |
| Play refresh, every float changed | 2.16 | 3.94 | 1.82× |

These confirm the provisional decisions: one document per area, and
`<number-field>` and `<live-text>` (repainting without layout) before P1,
since the Play refresh is the one case authored text is slower. Live
sources stay unnecessary. Popovers escape clipping with `clip: none` unless
their own area sets `z-index`, `transform` or `filter`.

### F2 results

`<number-field>`, `<color-field>`, `<splitter>` and `<live-text>` live in
`src/ui/controls` and run today's native Properties, Settings, graph literals
and shell splitters through `kit_number`, `kit_color`, `kit_splitter` and
`kit_live_label`. Web shots of Properties (with a scrub, a typed edit and the
color picker open) and Settings, dark and light, compact and comfortable,
match the build before them pixel for pixel. The spike's number and vector
rows are `<number-field>`s bound to numbers. RmlUi's `change` event can now
be stopped (`cmake/patches/rmlui-change-propagation.patch`), so a field's
inner input keeps its per-keystroke changes to itself.

`./build-release/test_suite --ui-bench` on the commit before F2 and after it,
alternated process by process (two runs each, 1-minute load 7-8), in Studio
with each arm alternated run by run as in S0 (ms, the mean of the two runs' medians):

| Case | Native before / after | Spike before / after | Spike ÷ native before / after |
|---|---:|---:|---:|
| Play refresh, every float changed | 0.81 / 0.79 | 1.33, 1 layout / 0.80, 0 layouts | 1.64× / 1.01× |
| Edit one field | 0.93 / 0.90 | 1.27, 1 layout / 0.52, 0 layouts | 1.37× / 0.58× |
| Select another object | 7.77 / 7.84 | 3.15 / 3.43 | 0.41× / 0.44× |

The spike's Play refresh spends about 0.4 ms in update and the rest
capturing its own context, which one document per area would not have.
Headless, its document small, the spike edits a field in 0.07 ms (0.19
before) and refreshes 27 floats in 0.27 ms (0.19 before, which laid out
once): style recomputation for each changed attribute replaces one cheap
layout there. A `<number-field>` builds its parts (29 elements a row against
19), so selecting rebuilds 9% slower and 50 rows instance in 8.0 ms against
7.7: pooled rows matter more.

## Waves

Every wave meets:

1. **Visual parity:** goldens before the wave in dark and light, compact and
   comfortable, UI Scale 100% and 150%, native and web; diffs reviewed and
   intentional changes listed.
2. **Behavior parity:** retargeted tests pass; new tests cover one Undo step
   per gesture, Escape restores, Enter or blur commits, ⌘/⇧ clicks, drops,
   context menus and keys.
3. **Frame time:** an idle editor lays nothing out; an interaction's UI time
   (sync + emit + update) is at most the native time +10% and at most 2 ms
   desktop / 4 ms web; a 20 Hz Play refresh with 100 fields costs at most
   1 ms; a 10k-row scroll step at most 1 ms plus layout.
4. **Studio edits itself:** an `.rml`/`.rcss` edit reloads within a second
   (keeping the last valid version); the UI Inspector shows the panel's
   elements with Edit Source; the guest Studio and Canvas preview show it.
5. **Gates:** `make build`, `make test`, `make test-render` with desktop
   render tests allowed, `make check-format`, `chibi_ctl --format`,
   `make web` and a browser check.

Shared files are append-only, one section per workstream: `studio_internal.h`,
`kit.h`, `rml.h`, `rml.cpp`, the panel table in `studio_sources.c`, the action
table in `studio_ui.c`, `scenes/Studio.kdl`, `test_suite.c`, `sources.cmake`,
`CMakeLists.txt`.

### Wave 0

- **S0**, the spike above: done, with provisional numbers in S0 results and
  the O(1) item lookups landed.
- **F0:** done. The Outliner moved from `studio_scene.c` into
  `studio_outliner.c`; `test/editor/studio_documents.c` keeps the fixtures,
  input helpers and runner (`studio_documents.h`), and each surface's checks
  live in `studio_documents_<surface>.c` (graph, shell, panels, outliner,
  properties, assets) behind one entry the runner calls.

### Wave 1: foundation (4 parallel)

| | Scope | Done when |
|---|---|---|
| F1 Contracts (done) | `emit` with arguments and event context; the panel-source table and `<panel>.<action>` router; `contextmenu`; RmlUi patches for a structural conditional (Vue's `v-if`) and for `input`/`change` on text inputs | Done: Settings builds its row controls with `data-when` and records through `settings.record`; binding tests (`rmlui_binding.c`, `ui_activations.c`) and the router check (`studio_documents_panels.c`) pass |
| F2 Controls (done) | `<number-field>`, `<color-field>`, `<splitter>`, `<live-text>`, `number_expr.c`, kit wrappers; widgets NumberField, VectorField, ColorField, Checkbox, TextField, PropertyRow, Panel, Splitter | Native Properties runs on the wrapped elements with the kit tests green: F2 results |
| F3 Popovers (done) | `popover.c`, `<menubar>`, one popup stack; widgets Menu*, MenuCommand, ChoiceList, AssetField, EnumField; a `choices` source | Done: the Behavior header's View menu and each area header's editor type menu are authored and at parity: F3 results |
| F4 Lists and drag (done) | `drag.c`, `ui_list_window`, kit wrappers; widgets List, TreeRow (drop zones), Tile | Done: a drag from the gallery's authored List lands on the native Outliner and the Scene, and one from native Assets in its authored Collected list, across area documents: F4 results |

#### F3: popovers (done)

`src/ui/popover.c` is the HTML popover attribute for every context, the
editor's and games'. `<button popovertarget="id">` toggles `<div popover
id="id">`, looking the id up from the button outward, so a widget repeated
by `data-for` finds its own (Submenu, EnumField and AssetField use a fixed
inner id). One stack per context holds what is open, authored popovers,
the kit's popups and `<color-field>`'s picker alike: showing one closes those
above the one it opened from (the popover holding it or its trigger), so a
context's stack is a chain, and an outside press, Escape and an item's
activation close the top first. Listeners on each context's root element,
the parent of its documents, hear every event before any document does;
`popovers_settle`, after each backend update, places what opened, opens a
submenu after its delay and ends fades.

- **Above every area:** the owning document of an open popover takes
  `z-index: 9` until nothing in it is open, above the areas (whose
  documents leave it auto, which RmlUi raises on focus) and below the kit's
  layer (10). The content keeps its document, so its data bindings work, and
  it is placed against the window (the context's root box), not its area.
  Blender draws menus in a window-level region after every area, and
  Unreal's `FMenuStack` puts each menu in a window of its own above its
  parent; raising the document gets the same result without moving the
  content or rebinding it (the teleport the Risks named).
- **Behavior:** placement below the anchor (flipped above), beside a
  submenu's row (flipped left, 4 dp up) or at a point, 2 dp from the anchor
  and 4 dp inside the window, to a tenth of a pixel as the kit placed its
  popups; `--anchor-width` for a list as wide as its field. `role="menu"`:
  Base UI's highlight (`data-highlighted`, the pointer or Up, Down, Home,
  End), typeahead within 0.5 s, Enter or Space clicks, a submenu after a
  100 ms hover or Right, Left closes it, Tab closes the menus; a `menuitem`
  or `option` click closes its popover and the menus it opened from, a
  checkbox or radio item stays open, and a disabled item never highlights
  (the pointer on it leaves none; keys and typeahead pass it) and its click
  reaches nothing. Other popovers are Base UI's nonmodal
  Popover: focus to the first tabbable control, Tab within, past the ends
  closes, focus moving outside closes. `<menubar>` (or `role="menubar"`):
  hovering another trigger switches menus, Left and Right step. A text
  field keeps its keys; a `role="combobox"` field keeps all of them.
- **The kit:** `kit_popup.c` emits its popups and shows them with
  `popover_show` (role `menu`, `listbox` for dropdowns and autocompletes,
  else `dialog`); what popover.c closes closes in the kit at the end of the
  frame, so an activated item still reports. It lost its placement, outside
  press, Escape, Tab, focus model, menu keys, typeahead, submenu timer and
  menubar (1,219 to 946 lines); a dropdown's search keeps its highlight.
  `<color-field>`'s picker lost its placement and outside press (731 to 626
  lines). `kit_has_popup`, `kit_wants_keyboard` and `kit_wants_pointer` see
  authored popovers too.
- **Widgets** (projects/studio): Menu, MenuItem (`data-alias-item`: icon,
  label, shortcut, checked, disabled; `data-current`; a chevron with
  `aria-haspopup`), MenuCommand (`data-alias-command="commands.<id>"`; the
  `commands` source gained `id` and `checkable`), MenuGroup, MenuSeparator,
  Submenu, MenuCheck and MenuOption (`menuitemcheckbox` and `menuitemradio`),
  ChoiceList over the native `<choice-list>`, EnumField and AssetField
  (`data-alias-table`, `data-alias-value`, `change` with `ev.value`). An
  alias named as a global variable reads the global, so widgets alias
  `table` and `value`, never `choices`. The widget gallery shows them.
- **`choices`:** every reflected enum or named choice field's values
  (`component_properties.h`), each asset kind's assets, refreshed when they
  change, and the editor types and Studio panels.
- **Parity:** each AreaHeader's editor type menu is a Menu over
  `choices.editors` (`emit('editor.type', area.index, type)` and
  `editor.panel`), and GraphView is the View button and its Menu, mounted
  per area (`studio_ui_area_controls`); their kit code and the MenuKeys
  behavior are gone. Shots before F3 (`d68a5fca`) and after, of the View
  menu and the side area's editor type menu, and its Studio UI submenu,
  open: web at 1600×1000 (dark and light, compact and comfortable; the
  submenu dark compact), 9 pairs, and native in a hidden window at
  2560×1600 (all four for each menu and the submenu), 12 pairs, all
  identical pixel for pixel. Both menus draw over the neighbouring area, as
  the kit's did from its layer.
- **Tests:** `--ui-popovers` (a menu over another area whichever has
  focus, Escape and focus, an outside press, typeahead, the submenu's
  delay, Left and Right, Enter, a checkbox item, a disabled item, the
  menubar, a popover's focus and Tab, a color picker escaping a short area,
  EnumField and ChoiceList filtering and keys, `choices.fields` listing
  every value, the widgets checking clean); `--kit-documents` (a kit
  menu's disabled row; an authored popover a kit menu holds: Escape closes
  it, then the menu; an outside press both);
  `--studio-documents` (the editor type menu draws over the next area and
  shows the chosen editor; the View menu toggles and stays open). The kit's
  menu, dropdown, autocomplete and popover checks in `--rmlui-kit` pass
  unchanged.

**Closed popovers mount nothing (follow-up, done).** C3 measured the cost of
the top bar's closed menus: about 1,100 elements and their data views stayed
bound, and a selection changed rows inside them (the commands' checked and
enabled states), laying the bar out once more. Base UI unmounts a Popup while
it is closed; Blender builds a menu's items when it opens. `popover.c` does
the same for a popover marked `data-template` (Menu and Submenu have it):

- RmlUi builds none of its content (`rmlui-data-template.patch`, above) and
  keeps the markup in `rmlui-inner-rml`. `popover_show` builds it with
  `rml_element_set_inner_rml`, so its views bind the data of that moment, and
  the end of the popover's exit transition (`end_fades`) removes it with its
  views. Opening again during the fade removes the pending end and keeps the
  content. A popover without the attribute (the kit's, a games', EnumField's
  and AssetField's) is unchanged.
- The data model could not carry the flag: its variables are the host's, an
  expression reads no element's attributes, and a view re-evaluates only when
  a variable it reads is dirtied. A `data-when` on an open flag would need a
  model variable per popover (ids repeat, as Submenu's `submenu`) and
  popover.c would have to dirty it.
- Keys, typeahead, the submenu delay, menubar hover switching, focus return
  and exit transitions needed no change: they work on the content that is
  built while open. A menu is placed in the update after it opens, as before.
- `data-*` bindings written on a Menu's use element land on the popover and
  are cancelled by `data-template`, so they go on an element around it. No
  Studio document had one.
- Not done: the dropdowns over ChoiceList (EnumField, AssetField, the
  Outliner's Add list, the Behavior header's debug targets) stay built; their
  owner can add `data-template` to the popover element when C4 lands.
- `ui_check` and the inspector see a closed menu's content only once it is
  open: a binding error inside a menu shows when it opens.

Numbers, `UI_BENCH_SPLITTER=1 ./build-release/test_suite --ui-bench studio`
(case 15) and `UI_BENCH_MENUS=1 ... --ui-bench studio` (case 19, new: the
File menu opened by a press on its trigger), the same binary over `3160b5f9`'s
Menu and Submenu and over these, alternated, 1-minute load 30-57 (provisional;
the counts do not depend on load):

| Case | Before | After |
|---|---:|---:|
| Elements in Studio's documents, at rest | 4,371 | 993 |
| Elements in the top bar's document, menus closed | 1,212 | 99 |
| A UI frame with nothing changed | 1.0-1.6 ms | 0.5-0.7 ms |
| Select another object: views evaluated / lookups / layouts | 862 / 3,057 / 5 | 267 / 851 / 4 |
| Select another object | 13.9-16.5 ms | 7.7-8.5 ms |
| Splitter drag 3 dp, the two UI frames after | 3.6-5.1 ms | 2.6-3.0 ms |
| Open the File menu: press to first paint | 12.5-14.6 ms, 1 UI frame | 5.1-5.8 ms, 1 UI frame |
| Elements in the top bar's document with File open | 1,212 | 169 |

Opening still paints in one UI frame: the press builds the items, and the
frame's update binds, styles and lays them out and `popovers_settle` places
the menu in it. The top bar's document, now 99 elements, lays out faster than
the 1,212 it held, which pays for the parse. A selection still lays out four
documents (five before): what it changes is shown outside the menus. Pixel parity: the 72 native shell shots of C3's capture
(`CHIBI_SHELL_CAPTURE_DIR`, dark and light, compact and comfortable, 150% UI
Scale: File, View, Object ▸ Add, the play target's, layout's, speed's and UI
Scale's menus) are identical to the shots before, and so are 32 web ones
(1600×1000, dark and light, compact and comfortable: File, Edit, View,
Object ▸ Add, the editor type menu, the Scene's View menu and UI Scale, and
a whole-window shot each). Checks: `--ui-popovers` (a closed menu holds no
elements and a change to what its items show evaluates no view; opening
builds them with the current values and the submenu in it stays unbuilt; the
items stay through the fade and survive opening again during it; the fade's
end removes them; a submenu builds when it opens and closing its menu removes
both) and `--studio-shell` (every menu in Studio's documents is empty while
closed; the File menu builds its items and removes them).

#### F4: lists and drag (done)

`src/ui/drag.c` is dnd-kit's drag and drop for every RmlUi context, over
attributes (`src/ui/drag.h`): a press on a `data-drag` source picks it up
after `FEEL_DRAG_DP`; every `data-drop` target in the context's documents
that takes its type is marked, the nearest under the context's hover
element is over, with a zone where it has `data-drop-zones`; an overlay
(`<drag-overlay class="drag-overlay">`, entering and leaving as presence
does) follows the pointer in the topmost document by transform; release
sends a bubbling `drop` and keeps the drop for `ui_drag_dropped`; Escape
cancels; the release's click is swallowed and the next press is a click
(the press-after-drag rule moved from the kit to the element level). It
listens on each context's root element in the capture phase, which an
RmlUi plugin reaches for every context (`rml_add_context_hooks`), so the
backend registers it with one call. One session spans every document,
as Unity's `DragAndDrop` spans its windows and Unreal's
`FDragDropOperation` its panels; a tree row's zones are Unreal's
`EItemDropZone` (`STableRow`'s top and bottom quarters above and below,
the middle onto), and a row that holds nothing splits in halves, as
Unity's tree view drops between leaves.

The kit's drag calls are wrappers: a node's source and target types
become attributes at `kit_end`, a drop is taken in the frame after its
release, and `kit_begin` ends a drag whose release the context missed
outside every element. The designer's own zone mark is `data-drop-zone`
now (`data-drop` names a target's types). `ui_list_window`
(`src/ui/list.c`) is `kit_list.c` on any scroller, `kit_list` its
wrapper; it also keeps a following list pinned when it set the scroll
before the grown content laid out, which `kit_list` lost (RmlUi clamps a
scroll to the content laid out).

projects/studio gains `List`, `TreeRow` (indent, twisty writing
`item.open`, icon, label, meta, a trailing slot, and drop marks with
zones from the use's attributes; seven elements a row) and `Tile`
(picture over label, badge, skeleton icon and spinner), styled by the
theme's `.tree-row` and `.tile`, so they match the native rows and tiles.
The widget gallery's `gallery` source (`studio_gallery.c`) windows the
project's assets into a List of draggable TreeRows and keeps Collected,
which takes drops before or after its rows and shows them as Tiles.

`./build-release/test_suite --ui-bench headless` case 9 (spike
`ListSpike`: a List of TreeRows over 10,000 rows, 330×820 dp, a 42-row
window), two runs natively (1-minute load 6.7 and 11.4) and one under
Node (load 12-15); arms alternate run by run, each in a document of its
own:

| Case | Native ms | Web ms | Views | Lookups | Layouts | Elements |
|---|---:|---:|---:|---:|---:|---:|
| 50 TreeRows instanced (0 to 50) | 2.54-2.65 | 3.04 | 613 | 1,841 | 1 | +350 (7 per row) |
| 50 TreeRows removed | 0.73-0.76 | 0.70 | 0 | 7 | 1 | |
| Scroll step of 3 rows, window kept | 0.90-0.95 | 1.19 | 515 | 1,546 | 1 | 300 |
| The same window made anew each step | 2.62-2.77 | 3.04 | 515 | 1,548 | 2 | 300 |
| Made anew ÷ kept | 2.90× | 2.55× | | | | |

A TreeRow instances at about a third of a PropertyRow's cost (50 rows
2.6 ms against 8.5 ms in the same runs, 7 elements against 29), and
keeping the window's rows makes a scroll step about 2.5-2.9× cheaper than
making them anew, within the 1 ms scroll gate natively.

Web shots (1600×1000) and native ones (hidden window, 3840×2160) of the
Assets list and tiles and the Outliner, dark and light, compact and
comfortable, before F4 (`d68a5fca`) and after: the Outliner and the Assets
list match pixel for pixel in all 16 pairs; tiles differ only in a
loading spinner's angle and which streamed thumbnails had arrived; whole
native frames differ in the status bar's report, which names the
workspace's path. Checks: `--ui-lists` (the drag contract in a bare
context across documents, the list window and the widgets),
`--studio-lists` (the List's window in Studio and the cross-list drags),
`--rmlui-kit`'s drag checks through the wrappers, and the Properties asset
drop and Hierarchy carries in `--studio-documents`.

Open: a keyboard sensor and auto-scroll for lists other than the
Hierarchy; a drag between contexts (the guest Studio, the graph's own
context); two areas showing one panel share its source's window.

#### F5: one document per area (done)

Each area, the top bar and the status bar is an RmlUi document of its own
in Studio's context, shown over a box the shell document (`studio.rml`)
lays out, as a page shows an `<iframe>` and as Blender tags and lays out
each area's regions on their own: `kit_document_begin` in the kit,
`src/ui/iframe.c` for the placement. After each update a document takes
its box's border box in pixels; a bar's box takes its document's content
height instead. The backend settles before each update as well, so a
window resize or a moved splitter lays each document out once. The kit's
overlay layer (popups, menus, tooltips, toasts, palette, dialogs, drag
overlay) is a document above them all (`z-index: 10`). Documents share the
context, so focus, keys, hover, data models and textures cross between
them; kit paths are unchanged. No RmlUi patch: RmlUi 6.3 has no layout
boundary (`Element::DirtyLayout` dirties the owner document, which
formats from its root), and containment would have meant re-entering the
flex and absolute-positioning code with a fixed box.

What changes: the kit's style sheets are read once and copied into each
document (about 1.4 ms per document, on a restyle or when one opens, such
as the drawer); Tab in a number field steps within its document, as
RmlUi's own Tab does; the kit's own document keeps `z-index: 0`, so
focusing it never raises it over the areas. An authored popover inside an
area draws above the other areas only while its document is in front:
RmlUi raises a document as it takes focus. Goldens (dark and light,
compact and comfortable, UI Scale 100% and 150%, native and web) match
except the Scene's live render and at most 1/255 in a status-bar text.

Native, 1440×900 at pixel scale 2, `--ui-bench studio`, the two builds
alternated twice, 1-minute load 12-19:

| Case | Before | After | Laid out |
|---|---:|---:|---|
| A text change in the Outliner | 4.09-4.29 | 1.02-1.06 | 1 document of 242 elements (was 655) |
| Full layout (1 px resize), compact | 4.13-4.16 | 2.83-2.88 | 8 documents, once each |
| Full layout, comfortable | 3.96-4.09 | 2.75-2.81 | 8 documents |
| Select another object (native Properties) | 8.44-8.52 | 6.13 | 4 documents; update 5.7 to 3.5 |
| A UI frame with nothing changed | 0.77-0.81 | 0.74-0.77 | none |

Web (`-DCHIBI_WEB_PROFILING=ON`, headless Chrome at 1440×900, DPR 2,
`Rml retained update` medians of 31, load 10-14): a keystroke in the
Outliner's filter 6.1-6.3 to 2.2 ms; a 1 px resize 5.5-5.6 to 4.0-4.1 ms.

`test/editor/kit_documents.c` checks the isolation, placement and fit,
popups above areas, focus across documents, inspection and lifetime;
`--studio-documents` checks the Outliner and the guest Studio.

### Wave 2: panels (5 parallel)

| | Scope | Deletes |
|---|---|---|
| P1 Properties (done) | Object, Prototype, Scene, Behaviors tabs; header with Live and Keep; search; field menu (Node and Member stay in a slot) | ~1,200 lines of kit UI; `write_field`, reset, Keep and multi-object logic stay: P1 results |
| P2 Outliner (done) | Rows, root, eye and lock, rename, drops, menu, filter | ~350 lines |
| P3 Assets (done) | Grid and list, header, New menu, name popover, drag sources, preview tooltip | Its kit UI, `kit_tile` and `kit_tip_image` (P3, below) |
| P4 Output (done) | Six views, follow-output, highlighted rows, Calls tree | ~450 lines |
| P5 Members (done) | Sections, rename, uses, menus, drag into the graph | ~550 lines of kit UI: P5 results |

#### P5: Members (done)

`Members.rml` is a List whose rows the `members` source
(`studio_members.c`) windows with `ui_list_window`, and `MembersHeader.rml`
is the header's filter (`members.filter`), mounted in the AreaHeader's
controls as GraphView is. What changes as the user selects and names
lives apart, in `members_ui` (the selected member's, the active
member's and the named member's row keys, and the selected member's
menu): rows bind it by key, so a selection re-evaluates those views and
not every row's. The rows are TreeRows, which compare their item's `key`
with the keys a panel aliases as `data-alias-selection` and
`data-alias-active` (added in P2; the rows had been TreeRow's anatomy
written out); the open graph's row is active by its own `active` flag, as
opening a graph rewrites the rows anyway. Rows keep the kit's
anatomy, so web shots before
(`db679e48`) and after match pixel for pixel in dark and light, compact
and comfortable: rows with a selection and a folded section, the name
field and a filter. The kit's rows, list, menu, search and text field
that Members emitted are gone; selection, renames across uses, deletes
with their Undo toast, history and the canvas drop stay in C, behind
`members.click`, `.edit`, `.menu`, `.uses`, `.add`, `.renamed`, `.cancel`
and the menu's `.open`, `.rename`, `.duplicate`, `.refs`, `.calls` and
`.delete`. Intentional differences: a menu the pointer opens highlights
nothing (Base UI; the kit highlighted its first item after a key), and a
drag held over the graph names its drop (Get, Set with Alt, Call).

What it needed beyond the panel, each usable elsewhere:

- **Autofocus** (`src/ui/autofocus.c`): an element created with
  `autofocus` after its document shows, as `data-when` builds a rename
  field, takes focus at the end of that update; a text field shows its
  ring and selects its text. An RmlUi plugin reports element creation
  (`rml_add_element_created`).
- **`stop()`** in a data-event expression stops the event (Vue's `.stop`),
  so a row's use count and add button keep their clicks from the row.
- **`<kit-dot shape fill>`**: the socket swatch from attributes.
- **Area keys**: focus in an editor's authored area passes Studio's
  shortcuts as a native area did (`studio_ui_shortcut_focus`), so F2, X and
  Shift+D work after a click on a row; a field keeps its keys. One rule
  covers every authored editor and its header's controls (P1, P2, P5):
  only a text input or textarea keeps every key, as in the kit, so a
  checkbox, a choice or a number field not being typed in leaves Studio
  and the area theirs.
- **Binding writes**: a source takes `wrote` only while its panel shows;
  Settings, hidden, had applied stale rows over a density a command set
  whenever any binding wrote, as the filter does on each keystroke.
- **MenuItem** shows its shortcut (`data-if` read "F2" as false).
- The canvas takes member drops right after its surface, so an empty
  graph's hint label no longer shadows it.

`./build-release/test_suite --ui-bench members` (case 10: 192 more
variables in 12 categories, Members 372×458 dp, a 28-row window, the
Sandbox at 1440×900, pixel scale 2), the binary before (`db679e48`, with
`CHIBI_RESOURCE_DIR` over that commit's resources) and after alternated
three times, 1-minute load 45-57 (the machine was busy, so counts and
ratios matter more than ms); ms, the mean of the runs' medians:

| Case | Native | Authored | Ratio | UI time (sync + emit + update) | Views evaluated | Layouts |
|---|---:|---:|---:|---|---:|---:|
| A UI frame with nothing changed | 1.01 | 1.06 | 1.05× | 0.77 / 0.81 (1.06×) | 0 | 0 |
| Select another variable | 3.19 | 3.26 | 1.02× | 2.00 / 2.14 (1.07×) | 284 | 2 |
| Scroll 3 rows | 4.06 | 4.94 | 1.22× | 2.90 / 3.28 (1.13×) | 815 | 2 |
| Filter by a keystroke | 5.92 | 5.17 | 0.87× | 3.37 / 3.35 (0.99×) | 815 | 2 |

Before `members_ui`, a select evaluated 768 views (every view of every
window row, as a filter does) and cost 1.23× native (load 16-22). The
source's sync costs about 0.05 ms over the native's at every refresh.
**Follow-up, shared:** a scroll step still rewrites every window row,
because `data-for` matches items by index where the kit kept each
member's elements by key; a keyed `data-for` (Vue's `:key`, an RmlUi
patch) would let the Outliner, Assets and Members lists move rows instead
of rewriting them.

Rows as TreeRows (P2's follow-up), the same release binary alternating
the two `Members.rml` (written-out rows over the same data) three times,
load 16-17: a select 3.09 → 3.06 ms (0.99×, 284 → 368 views), a scroll
step 4.59 → 4.74 ms (1.03×, 815 → 955), a filter keystroke 4.63 → 4.82 ms
(1.04×). The added views are TreeRow's `marked` and `hidden` and its
alias views (`Update` returns at once), three a row on a select and five
on a scroll. `CHIBI_MEMBERS_CAPTURE_DIR`'s 16 shots match pixel for pixel
in the Members area. Checks:
`--studio-members` (also in `--studio-documents`): sections, swatch and
use counts, selection, folding, a use count's press, F2 and double-click
rename with Enter, Escape and one Undo step, the add buttons, the context
menu's Duplicate, X with its Undo, a drag onto the graph and its hint,
reveal and the filter, which leaves preferences alone; with
`CHIBI_MEMBERS_CAPTURE_DIR` set it writes native shots of rows, rename,
menu and drag in the four variants. Open: two areas showing Members share
one window and one List.

#### P1: Properties (done)

Properties' Object, Prototype, Scene and Behaviors tabs are
`projects/studio/content/ui/Properties.rml`, a mount per area that shows
it (`studio_ui_area`), and its header's controls `PropertiesHeader.rml`
(mounted in the area header as GraphView is), over
the `properties` source: `studio_properties_view.c` writes the variable,
and `studio_properties.c` keeps the model (`write_field`, merged vector
writes, Reset and Apply to Prototype, Keep Changes, components, rename)
behind intents, `emit('properties.<action>', ...)`. A row's id names what
it edits (`o<entity>.<component>.<field>`, `p<component>.<field>.<prototype>`
or `s.<setting>`), so an intent that arrives after the selection moved still
edits its own object. The row widgets: PropertyRow (its tip and label
`for`), NumberField, VectorField (per-axis bounds, units, mixed and override
marks; each axis's value is `values[axis.slot]`, a variable its user
aliases), ColorField, Checkbox, EnumField for every choice integer and
AssetField for references (both gained `data-mixed`, `data-readonly` and,
for AssetField, drop marks and `table.none`). Gestures: `input` writes live
without history, `change` records one Undo step, Escape cancels a scrub;
Play writes the running world only. RmlUi sends change when a binding sets
a checkbox's `checked` too, so `properties.check` carries the state
(`ev.value`) and leaves a field that holds it: a flip there had flipped
bound fields back every frame. Drops: an asset dragged from Assets
over an AssetField row (`data-drop`, F4) shows in it and on the object in
the Scene and lands as one step through `properties.drop`. The field menu
(a right press on a row) and the component menu (⋯ or a right press on a
panel's head) are Menus that `popover_show` opens at the pointer.

What it takes to be fast:

- Numbers are text in room each member owns (32 bytes), rewritten in place:
  RmlUi writes a double into an attribute to three decimals, and copying
  each refresh's text would grow the doc's string pool.
- Numbers live in `propertyValues`, a variable of their own: a number row
  or axis names its slot there (`propertyValues[row.slot]`), so a refresh
  that moves only numbers dirties that variable alone and evaluates 20
  views for the Player, where dirtying `properties` evaluated 510 (RmlUi
  dirties by top-level variable). A source's sync gets the data model to
  dirty such a second variable.
- A choice or asset row lists only its current option until it is opened
  (`properties.choices`), so closed lists build nothing; `<choice-list>`
  highlights the current option when options arrive after it opened.
- A panel keeps its slot by key (its component) for the session, as the
  kit keyed its panels, and a panel's rows past those shown stay hidden
  rather than removed, so a selection rebinds rows instead of building
  them; a hidden panel lets its rows go so their views cost nothing.
- `.prop > .label` (`panel.rcss`) keeps the property row's label rule off a
  row's popover items, which have labels too.

`./build-release/test_suite --ui-bench studio`, this branch against the
commit before it (`80bd2538`, its bench given the same fix below),
alternated process by process, eight pairs (1-minute load 16-30; the mean
of the runs' medians of 31, ms, Studio's whole UI frame: sync, emit,
update and capture). The bench's edit and Play cases had moved values back
and forth, so every other timed frame showed the value already shown and
the authored panel timed an idle frame; values now only grow.

| Case | Before (kit) | After (authored) | After ÷ before | Views evaluated | Layouts |
|---|---:|---:|---:|---:|---:|
| Select the other object or the Player | 7.83 | 9.02 | 1.15× | 2,733 | 4 |
| Edit one field of the Player | 1.59 | 1.50 | 0.94× | 20 | 0 |
| Play refresh, every float of the Player changed | 2.04 | 2.11 | 1.04× | 20 | 0 |

Play refresh pairs ranged 0.88-1.26×: the sync costs 0.04 ms more than
the kit's (it rereads every shown field) and RmlUi's update 0.07 ms more
(20 views that index `propertyValues`, and the fields they set). Against
the integration head before P1 (`ed267081`, with Output and Members
authored), four pairs at load 17-21: select 1.18×, edit 0.99× and Play
refresh 1.14×, Studio holding 2,351 elements to the kit's 2,039. A
value-only refresh lays the Properties document out zero times
(`--studio-properties` checks it with a layout probe). Selecting another
object still costs more than the kit's, over the +10% gate: with the
Properties content removed the same case takes 4.6-4.9 ms, so the authored
panel's share is about 4.3 ms against the kit's 3.1, most of it layout and
style of the rows that rebind and the views of every row a panel keeps.
Web shots (1600×1000; dark and light, compact and comfortable) of the
Object tab, several objects' mixed values, Play with Keep, the field menu,
the Shape choice and the Document picker open, the Scene tab and the HUD's
Ui panel match the kit's pixel for pixel over Properties but for the open
choice lists and a hovered panel head's grip, 3 px lower: the ChoiceList's
search reads "Search options", and its list is the field's width, where
the kit's was 8 dp wider.

Intentional differences: every choice integer is an EnumField, also those
with three or fewer values the kit showed as segments; search opens every
panel that has no fold of its own; a panel's fold is kept per component for
the session, as Unity keeps a component's foldout; the field menu opens on a
right press anywhere on the row, and Reset to Prototype on Position resets
both axes; the Scene tab's settings record once a gesture ends, where the
kit recorded every frame of a scrub. Since C4 an asset field's picker
offers a name no asset has yet again, as `Use "name"`.

#### P2: the Outliner (done)

Since the Behavior-driven panels spike, its behaviors replace the
`outliner`, `outliner_ui` and `outliner_header` sources described here; the
markup, keys and checks are the same.

The Outliner is `Outliner.rml` in each area that shows it (a List of
TreeRows, `studio_ui_area_view` aliasing the area's item of
`outliner.views` as `view`) and `OutlinerHeader.rml` in its AreaHeader's
controls (the filter and Add, a ChoiceList of the scene's prototypes). The
`outliner` source publishes the root, the count, the notice, the filter
(`query`, two-way, view state) and each view's window (`root`, `rows`:
key, label, icon, behaviors, drop, marked (the rest of a
multi-selection), hidden, locked, renaming); `outliner_ui`, apart so a selection reads only its views,
holds the active row's key (TreeRow's `data-alias-active`), the rename
field's text and the row menu; `outliner_header`, apart so a scroll or a
selection reads none of its views, holds what rarely changes (the Add
list, editable, the eye's tip). It takes `outliner.click`,
`.menu`, `.eye`, `.lock`, `.drop`, `.rename`, `.renameEnter`,
`.renameCancel`, `.collapse`, `.add`, `.openPrototype` and `.spawnBreak`.
The filter stays one per Studio, as the kit's was, since Up, Down and Shift
ranges walk the filtered rows. Blender's Outliner and Unity's Hierarchy are
the references: click, Mod and Shift selection, F2 and double-click rename,
eye and lock; and Unity's focusable list for the keyboard.

- **Interaction:** a click selects on release, Mod toggles, Shift ranges
  from the anchor; a right click keeps a selection it lands in and opens
  the Menu at the pointer (`popover_show` at a point) with the kit menu's
  items; the eye and lock show on hover or when set, Alt+click isolates,
  and their buttons stop the click from the row (P5's `stop()`). Rename
  is `<input autofocus data-value-lazy>` in place of the row's name
  (`data-when`, focused as it is built by P5's autofocus): Enter commits
  as one Undo step and gives the keyboard back to the list, Escape ends it
  first so nothing commits, and leaving the field commits. Drops:
  prototypes onto the list add an object; behaviors and the asset kinds an
  object's fields name onto its row, which previews the asset in the Scene
  while it is over (polled at emit).
- **Keyboard (UX review):** the List is one Tab stop. `kit_wants_keyboard`
  now claims the keyboard only for fields, the code editor and a modal's
  hosted content, so a focused row, list or button in an area or the
  chrome passes keys on: Studio's, the graph's and the Scene's still reach
  the area under the pointer after a click on an Outliner or Members row
  (the kit's claim had blocked them, and P5's `in_editor_area` covered only
  Studio's shortcuts). When Tab moved the focus to a list
  (`:focus-visible`), its area takes the keys wherever the pointer is
  (`studio_ui_keyboard_area`), and the active row shows the focus ring.
  Left and Right fold the root.
- **Hit targets (UX review):** the eye and lock stretch to the row's full
  height, so a press above or below their icon toggles rather than selects.
- **Shared fixes:** Menu's wrapper is now the 480 dp containing block its
  fixed popup is placed against (RmlUi places a fixed box against its
  nearest positioned ancestor), so authored menus fit their widest row
  instead of clipping long labels at 180 dp; a disabled popovertarget
  button opens nothing; a choice-list's `placeholder` names its search.
- **Parity:** web shots before (`80bd2538`) and after, dark and light,
  compact and comfortable, of the rows with selection, hover, a hidden and
  a locked object, rename in place, the row menu and the Add list, compared
  over the Outliner area with a 2% fuzz: identical but for the caret's
  blink. The row a drag is over now fills with the soft primary as the
  TreeRow's drop mark does (the kit row kept its hover gray).
- **Bench:** `--ui-bench studio` case 12 adds 10,000 objects and scrolls
  the Outliner three rows a run from the middle, then selects one of two
  objects in view. Release builds before and after, alternated process by
  process three times at a 1-minute load of 35-39 (provisional), whole
  Studio UI frame (sync + emit + update + capture), medians (views and
  layouts from the final build, with `outliner_ui`):

  | Case | Before (kit) | After (authored) | After ÷ before | Rows instanced | Views | Layouts |
  |---|---:|---:|---:|---|---:|---:|
  | Scroll step of 3 rows, 22 rows shown | 6.30 / 8.29 / 7.93 | 7.47 / 6.57 / 6.87 | 0.93× | 3 → 0 | 606 | 3 |
  | Select another object in view | 7.61 / 9.54 / 9.46 | 9.08 / 7.93 / 8.70 | 0.97× | | 625 | 9 |
  | A text change in the Outliner (case 13) | 2.27 / 2.13 / 1.77 | 1.54 / 2.16 / 2.13 | 0.94× | | | 1 |

  A scroll step or a selection reads the window's row views (about 23 a
  row) and the root's; a selection also reads the chrome's (`editor`,
  `commands`). The header's Add list (160 views) and the eye's tip moved
  to `outliner_header`, from 696 views a step. The active row's key moved
  to `outliner_ui`, which TreeRow compares with each row's `key`
  (`data-alias-active`; `data-alias-selection` likewise), so a selection
  reads 625 views, from 927; a scroll step reads 606, from 518, for the
  key comparisons and the alias views. The rest of a multi-selection
  (`marked`), hidden and locked stay row flags, and so does renaming: in
  `outliner_ui` it put the window's 44 `data-when` views under every
  selection (814 views). A parallel array of flags can't replace them, as
  RmlUi addresses take only literal indices, not the `data-for` index.
  The first count, before the integration merges, laid out 2 documents a
  step and 6 a selection. `data-for` matches by index, so
  a step rewrites each row's values as the window slides (as the gallery's
  List does), and instances none.

  The Outliner's document held 929 elements against 558: closed popovers
  (the row menu, the Add list's options) and `data-when` templates add
  elements that never lay out. (Since the F3 follow-up the row menu holds
  none while closed.) Sync rose 0.3 to 0.5 ms (the filter walks
  every object at each refresh, as the kit's emit did); emit and capture
  fell.
- **Tests:** `--studio-outliner` (new; also in `--studio-documents`):
  rename in place, click, Mod and Shift selection, eye and lock at the
  row's top and bottom edges without selecting, Alt+click isolation, the
  menu at the pointer and its heading, the filter and No matches, folding
  by twisty, double click, Left and Right, a 2,000-row window that keeps
  its length and elements and reveals Down's row, and Tab with Down, F2
  and Enter while the pointer is elsewhere. `--studio-lists`: a prototype
  onto the Outliner and a behavior onto its row.
- **Deleted:** the kit Outliner (header, rows, rename row, view toggles)
  and `studio_drop_row`, about 280 lines of kit UI.

#### P4: Output (done)

`Output.rml` holds the six views and `OutputHeader.rml`, mounted in the
area header's controls as GraphView is, the view switch (with Problems'
and Runtime's counts), Problems' scope and Refresh, the filter (two-way,
UI-local) and Clear, over the `output` source (`studio_output.c`); the
rows are `output_rows`, a source of their own synced after it, so a new
line evaluates the rows' views alone and a view change the header's. The
kit UI of `studio_output.c`, `studio_find.c` and `studio_calls.c` is gone
(443 lines removed, 82 kit calls); the views' data and their navigation stay in C
behind the source's actions: `view`, `scope`, `refresh`, `clear`,
`click` and `open` (a row's key), `direction`, and the keys Output binds.

- **Rows:** a List window (`ui_list_window`, 60 dp overscan) of the view's
  rows, each moved to its place by `translateY` and keyed by what it
  shows: a log line's serial (the editor's log is now a ring of 1,024 lines
  with serials), a record's, a problem's section and index, a reference,
  a call. A row that stays in the window keeps its element and markup, so
  a new line is highlighted and parsed once, as it arrives, as S0 asked;
  moving rows lays nothing out. Three arrays by shape: `lines`, the Log's
  (one markup element a row), `entries` (kind icon, place, markup, count)
  and `items` (TreeRow). Markup escapes `<`, `&`, quotes and braces
  (`highlight_escape`; braces would open data expressions).
- **Follow-output:** the Log and Info follow new rows while at their end,
  stop when scrolled up and resume back at the end; a List's padding now
  counts in its end (`src/ui/list.c`).
- **Calls:** a TreeRow tree, as VS Code's call hierarchy: a click selects,
  the twisty folds (the item's `open`, read back at sync), a double click
  or Enter opens, and Up, Down, Home, End, Left and Right walk and fold it
  with the pointer over Output (Studio's shortcuts, which an editor area's
  focus no longer blocks). Find's rows open on a click, as before.
- **Also:** what the running game prints reaches the Log under its
  behavior's name (Unreal's Output Log in PIE); it was dropped. The gaps
  RmlUi drops between a log line's lead, origin and text are `&nbsp;`.
  A `line-height` or `font-size` set by `var()` now follows its variable
  (`rmlui-variable-line-height.patch`): authored labels kept the other
  density's line-height after a density switch, which the kit hid by
  restyling its controls every frame.

`./build-release/test_suite --ui-bench studio` case 11, Output's Log in
the drawer at 1440×900 following one or eight new lines a frame, the kit
build (`6ecac3a5` with its own `projects/studio`) and this one alternated
process by process, three runs each, 1-minute load 11.6-13.2 (ms, the
range of the runs' medians; provisional):

| Case | Kit | Authored | Ratio |
|---|---:|---:|---:|
| 1 line a frame, log growing | 4.10-4.25 | 3.45-3.69 | 0.85× |
| 8 lines a frame, log growing | 6.03-6.14 | 5.18-5.44 | 0.87× |
| 1 line a frame, log full (oldest drop) | 4.20-4.33 | 3.40-3.55 | 0.82× |
| 8 lines a frame, log full | 4.22-4.38 | 5.46-5.83 | 1.33× |

Per frame: one layout (the drawer's document) in both; markup parsed for
the new lines only, where the kit re-parsed every shown row (about 19)
once its ring was full. Eight new lines into a full log cost more because
each new row's text is new geometry to capture (capture 3.1-3.4 ms
against 2.0-2.2); the kit's full-log cost stays flat because it rewrote
every row anyway. Since the rows moved to `output_rows` (after Members
merged), a frame evaluates 38 views (64 with one variable; the kit has
none) and the drawer's document holds 556 elements against 518; the same
alternation at a 1-minute load of 48-62 gave 0.84-1.0× for one line a
frame and eight into a growing log, and 1.36× for eight into a full one.

Web shots (1600×1000, before `6ecac3a5` and after), Problems with places,
Find, Calls, Info and Runtime, dark and light, compact and comfortable:
identical pixel for pixel but for counts and clocks that moved between
runs, the Log's restored gaps, and the header's labels after a density
switch, which now sit as they do before one.

#### P3: Assets (done)

projects/studio's `Assets` (the area body), `AssetsHeader` (its header
controls, mounted per area with `studio_ui_area_controls`) and `NewAsset`
(New's name popover, in a document of its own, mounted only while it asks,
so it shows whether or not an Assets header does), over three sources in
`studio_assets.c`: `assets` (the header's values and the rows),
`asset_view` (the selection and the resting preview) and `asset_menus`
(New's kinds, the row menu and the name popover). The layout follows
Unity's Project window (the size slider whose smallest step is the list
grouped by kind) and Unreal's Content Browser (kind badges, the hover
thumbnail). C keeps the index model (sorting, grouping, filtering, usage
counts), the selection and its keys, thumbnails, the row menu's actions
and the asset writes; markup sends `assets.click`, `open`, `menu`, `kind`,
`tile` and the row menu's items as intents and binds the name filter and
the new name both ways, as view state; the row menu opens at the press
(`popover_show` at a point, from the press's document).

- **Slots.** One List windows either view with `ui_list_window`: the
  list's TreeRows or the grid's lines of Tiles, its columns fitted to the
  List's laid-out width. Row `i` (or line `i`) takes slot `i % slots` and
  C places it in the spacer, as Virtuoso keys its items, so a scroll
  rewrites only the slots entering and the others' tops, and slots only
  grow, so a narrower filter hides them instead of dropping elements.
  Tiles sit in lines, not all in the spacer: RmlUi restyles every sibling
  of an element whose attribute changes (for sibling combinators), so a
  selected tile restyles its line, not the grid. Index-matched rows in
  flow, the first version, cost about 1.6× more per scroll step.
- **Variables by rate of change** (P5's lead): a click changes only
  `asset_view` (each tile's selected and preview bindings), an open row
  menu only `asset_menus`. Rows and tiles carry their asset's `key` and
  alias `asset_view.selected` as `data-alias-selection`, the selection
  contract TreeRow took in P2 and Tile shares; their marks land on the
  use's only child, so a selection restyles no sibling. A select reads 819
  values, from 585 when the uses compared ids themselves, in the same two
  views a tile, and takes as long (6.6 ms against 6.6, three alternated
  release pairs at a load of 50). The data goes through the shared
  `ui_data_put_*` writers.
- **Thumbnails** are `kit:thumb:<slot>` sources the sync asks for, as the
  kit's emission did; Tile paints its picture as a decorator and hides its
  skeleton by visibility, so a thumbnail arriving lays nothing out (the
  gallery's Tiles too). A tile's tooltip names it, says what it is and how
  often it is used, and shows the 256 dp preview once the pointer rests
  0.3 s on it: C reads the asset under the pointer from its row's
  `data-asset`, which F2 and Delete use too.
- **Foundation fixes:** `:focus-within` for every context
  (`src/ui/focus_within.c`; the theme's field ring rule keeps to text,
  search and autocomplete fields), and a MenuItem showing its shortcut
  (`data-if` read "F2" as false) and growing its menu to its widest row.
- **Deleted:** the kit's `kit_tile`, `kit_tip_image` and their widget
  fields, and Assets' kit UI (header, list, grid, row and tile events,
  tooltips, row menu, New menu, name popover and button). The file gained
  its sources' data plumbing, so `studio_assets.c` grew from 959 to 1,516
  lines; the kit lost 87 lines.

`UI_BENCH_ASSETS=1 ./build-release/test_suite --ui-bench studio` (case 14:
Assets maximized at 1440×900, pixel scale 2, over the Sandbox's 101 assets
and 480 more inserted into the index), the kit build (`80bd2538` with the
same case) and this one alternated process by process, three pairs,
1-minute load 14-18, so the times are provisional; the median of the
pairs' medians of 31 runs:

| Case | Kit ms | Authored ms | Ratio | Views | Layouts | Elements in the area's document |
|---|---:|---:|---:|---:|---:|---|
| Open the grid (3 UI frames) | 36.9 | 41.4 | 1.12× | 5,657 | 2 (kit 3) | 1,581 (kit 1,105) |
| Scroll the grid a line | 6.9 | 5.3 | 0.77× | 2,466 | 1 | |
| Type in the filter | 12.4 | 9.2 | 0.74× | 2,466 | 1 | |
| Select another tile | 4.04 | 4.72 | 1.17× | 585 | 1 | |
| UI frame, nothing changed | 0.92 | 1.28 | 1.39× | 0 | 0 | |
| Scroll the list a row | 2.27 | 2.59 | 1.14× | 868 | 1 | 907 (kit 517) |

Selecting costs more in update (1.34 ms against 0.98) for the 585 views;
the idle frame's 0.36 ms more is the three sources' sync and emit
(0.49 ms against 0.34) and update and capture over 476 more elements, the
hidden slots and each Tile's and TreeRow's own markup. In the web build,
the 600-asset workspace scrolled and filtered at the same frame times as
the kit's, within noise.

Web shots (1600×1000, dark and light, compact and comfortable) of the grid,
the list, the hover preview, the row menu, New and its name popover and a
drag onto a Properties field match the kit's pixel for pixel but for the
row menu, which now fits Break When Spawned (the kit clipped it) and shows
its shortcuts, and scene thumbnails that streamed at another moment.
Checks: `--studio-documents` (the cached rows, no row marked selected
without a selection, the asset drop onto Properties, New for every kind,
the filter's kind first, a kind from the menu asking under the button with
its name selected, a wheel-scrolled, filtered and cleared grid showing its
tiles), `--studio-thumbnails` (every kind through the kind filter, the
grid's keys, a 1,220-asset grid windowed), `--studio-lists` (an authored
Assets row dragged into Collected) and `--ui-lists` (the Tile's skeleton).

Open: two areas showing Assets share one window and one hover (Rename's
refactor popover is authored since C4); the list's rows are
siblings of one spacer, so a scroll restyles all of them, and each
evaluates about 17 views (the TreeRow's and the drag and hover attributes
on its use); selecting and the idle frame cost 1.2× and 1.4× the kit's.

#### Shared follow-ups after Wave 2 (measure on an idle machine)

Wave 2 ran with load averages of 12–60, so its ratios are provisional. Three
costs recur across panels and belong in shared code, not in each panel:

- **Keyed `data-for`** (Vue's `:key`, an RmlUi patch): a window that shifts
  rewrites every row because rows match by index (Members' scroll 1.22×,
  Assets' list scroll 1.14×). Output keys rows by placing them itself.
- **Change-driven sync**: a shown panel's source syncs every refresh and
  walks its data even when nothing changed (Assets' idle frame 1.4×). Sources
  should compare a revision (asset index, scene history step, log serial)
  and return before walking. Behavior panels did this for `Refresh`
  (Behavior-driven panels). Measured for sources on the idle UI frame of the
  Outliner bench (0.9 ms, 0.44 of it sync; release, load 12): `commands` 124
  µs, `properties` 124, `editor` 108, `scene_view` 50 in every layout;
  `assets` 76 (the grid's tiles 61, the List's id lookup 8) while Assets
  shows; `inspector` 565 while the Inspector shows (2.4 ms idle frame). None
  has a cheap revision yet: Assets' tiles also read the thumbnails, which
  arrive when they are rendered, the List's scroll and width, which only the
  DOM knows, and the pointer's hover; `commands`, `editor` and `properties`
  read a state `ui_state` only approximates, since the kit re-emitted every
  frame with UI work; and the Inspector's walk has no document revision to
  compare (below). A source that opts in needs a stamp over everything it
  reads, thumbnails and DOM geometry included.
- **Sibling restyles**: RmlUi restyles every sibling of a changed element, so
  a selection in a flat list restyles all rows (Assets groups tiles in lines
  to contain it). Contain it in the List widget or patch RmlUi. TreeRow and
  Tile mark the use's only child, so their selection by key restyles no
  sibling; hover and drop marks on the use still do.

Fixed in Wave 3 (fx-classes): **panel RCSS could not read the body's
classes.** Scoping names the host `body`, so `body.comfortable .row` became
`[data-ui="Output"].comfortable .row`, which only a host carrying the class
matches, and `body:not(.dark)` matched every host. Output, Scene and Canvas
had left their density and dark-role rules in `theme/workspace.rcss`. The
host does not take the page's classes: the kit's unscoped `.dark` token blocks
would then match each host and replace the palette presets and the density
metrics inside it (a first try failed the Settings font check that way). Scoping instead
writes the classes after `body` for the page body as well (`ui_document_scope`:
`body.comfortable .row` becomes `[data-ui="Output"].comfortable .row,
body.comfortable [data-ui="Output"] .row`, and `body:not(.dark)` only the
page's form). RmlUi restyles on a class change, so a density or theme change
applies at once with no per-frame work, in the kit's document and in an
area's. Output's rows moved from `workspace.rcss` into `Output.rcss`, the
Actions row into `Canvas.rcss` and the viewport's dark roles into
`theme/viewport.rcss`; `workspace.rcss` keeps no rule that reads the body's
classes for an authored panel.
`--ui-followups` checks a host in the kit's document and in an area's
(`test/editor/kit_host.c`), `ui_document` the selectors. Reduced motion stays
`@media (theme: reduced-motion)`. Left in the theme because their panels are
still kit code: `body.comfortable .designer-tile` (Designer); D1 moved it into `Library.rcss`.

- **List reveal with top padding**: `ui_list_window`'s reveal measures a
  row from the scroller's top padding, so a padded list stops about the
  padding short (the Inspector avoids padding; the Outliner shows it).
- **Change-driven sync, DOM side**: the Inspector walks every open element
  at each refresh while it shows, because RmlUi documents have no revision
  to compare.

- **`data-attr` inside `data-when`** (D1): one `data-attr` in a row's
  `data-when` branch adds about 260 view reads to a Details selection,
  however many rows follow; it points at the data model, not a widget.
- **Widget quirks** (D1, G1): EnumField's dropdown is only as wide as its
  value; `data-for` leaves its hidden template as a sibling, so
  `:last-child` never matches the last row; margins collapse between
  siblings but not through a `use` wrapper's first child; a right press
  doesn't move focus, so a focused field keeps keys a context menu should
  take (G1 blurs on menu open; popover.c should own it).

- **List window query per frame** (fx-refresh): `ui_mount_measure` runs
  `window_lists`, a `[data-list-rows]` query per mount per frame (about
  123 µs of a 0.9 ms idle frame over 14 mounts); find a mount's list hosts
  once when its markup loads.

Re-measure every Wave 2 bench case (`--ui-bench studio`, `members`,
`UI_BENCH_ASSETS=1`) with `uptime` below 4 before and after each fix.

Fixed after Wave 2: an area that changed from one authored panel to another
kept the first panel's views in the data model. Binding the shared host moved
it with the old markup, which bound its views again, and replacing the markup
in the same frame dropped elements whose new views RmlUi had not yet taken
in, so they outlived them, and every later removal logged a warning per view
(the UI bench wrote tens of gigabytes). `ui_mount_sync` now empties a host
before binding it, and the RmlUi patch `move-keeps-data` keeps a moved
element's views bound under the same model, as a DOM move does, so a kit
reorder (GraphView's host in a maximized header) no longer unbinds evaluated
markup. `--studio-panels` turns one area between every pair of authored
panels, three rounds, and checks for no RmlUi warning and as many views as
after the first round; the Studio bench stops when a frame logs more than
100 warnings.

### Wave 3: scene, chrome, palette (4 parallel)

| | Scope | Deletes |
|---|---|---|
| C1 Scene (done) | Scene and Canvas headers, actions log, camera labels, notice line | About 460 lines of kit UI: C1, below |
| C2 Editor chrome (done) | Behavior header (breadcrumbs, debug target), graph find bar; Text Editor header and find; Performance with `<line-chart>` | The kit's header, find and chart code: C2 results |
| C3 Shell (done) | `Workspace.rml` (areas, `<splitter>`, drawer and side transitions, maximize); editor-type popover; `<menubar>`; play target, layout, UI Scale, debugger steps | ~600 lines and `kit_menubar_trigger`: C3 results |
| C4 Palette (done) | `Palette.rml` over ChoiceList (prefixes, pages, recents, thumbnails); asset pickers, Add Component, refactor popover; ranking moves to `support/` | ~500 lines of `kit_command` UI: C4 results |

#### C4: the palette and pickers (done)

One ranked list, the `palette` source (`studio_palette_view.c`), shows as
`Palette.rml`, a modal over the editor (cmdk's Command.Dialog, in a
`kit_modal_host`, so Tab keeps the focus in its field as the kit's did),
or as `Picker.rml`, a popover under the field that opened it, in a
document of its own, as Unreal's asset picker hangs under its property
(Unity's Object Picker is a window; the kit's picker hung, so the
dropdown stays). Both documents mount only while open. Its clients fill
it and hear what is chosen, by a switch, not callbacks: the search
(`studio_palette.c`: commands, settings, objects, assets, projects,
scenarios, the active document's symbols; `>`, `@` and `?`; recent items
first, frecency boosts), the graph's add-node search
(`studio_graph_edit.c`, its own ranking, shown in order), Add Component
(`studio_properties.c`) and the asset pickers (`studio_pick.c`).
`Refactor.rml` is the rename-across-uses and safe-delete popover over the
`refactor` source (`studio_refactor.c`), at the pointer, as JetBrains'
Rename and Safe Delete ask: Enter on a new name lists each asset it
rewrites with the name it takes or its change count and the mentions,
Enter again or Rename applies it, and its toast's Undo puts every file
back.

- **One ranking** (`support/text_rank.h`): `text_search_score` plus a
  boost, groups ordered by their best match, at most eight rows a group
  while the palette filters, or entries kept in order. The palette, the
  pickers and the kit's dropdowns and autocompletes share it, and labels
  mark in bold what `text_search_match` matched, so the bold follows the
  ranking (a typo match marks its letters; the kit's marker left those
  plain). `<choice-list>`'s own filter keeps `text_search_score`; it
  filters without reordering, since RmlUi has no `order`.
- **Work only on change:** the client collects when the inputs (asset
  index revision, objects) or the kind of query (prefix, typed or not)
  change, ranking runs on each query, and the sync writes the window into
  `palette` (rows: heading, label markup, about, meta, shortcut, icon,
  image, place, current). The highlight lives apart in `palette_ui`: a key
  evaluates one view. The kit re-collected, ranked and emitted every row
  each frame it was open.
- **The window:** the List holds group headings and rows of two heights
  (`--row-heading`, `--row`, `--row-about`), so `ui_list` takes each
  row's top (Virtuoso's item sizes), and reveal now counts the scroller's
  top padding. 2,242 results keep 26 rows.
- **`<choice-list virtualized>`** (Base UI's Combobox with `filter={null}`
  and `virtualized`): the app filters, orders and windows; options carry
  `aria-posinset`, the list `aria-setsize` and `highlight`; keys (Up,
  Down, PageUp, PageDown, Home, End, cmdk's Ctrl+N/P/J/K) and the pointer
  send `highlight` (`ev.value`, and `ev.pointer`, which scrolls nothing),
  and keys pressed before the app answers move on from the place last
  sent. Enter or a click sends `change` with the place.
- **Pickers:** AssetField is the field alone now; a panel opens Studio's
  picker on it with `emit('palette.pick', kind, value, none, 'typed')`,
  and the field hears `change` with the name chosen, so Properties' and
  the gallery's `data-event-change` stayed as they were. The picker
  highlights the current asset as it opens, ranks as typed, offers
  `Use "name"` for a name no asset has where the field allows it (the
  kit's did, for every Properties asset field; P1 had lost it), and
  closes on the field again, Escape or a press outside. Its placeholder
  names the kind ("Search UI"), as the kit's did. `choices.assets` keeps
  each kind's label, icon and none, and no longer lists every asset at
  each refresh. The Designer's kit asset field anchors the same picker on
  its element (`kit_item_element`); graph pins keep their kit dropdowns
  (W4 G1, since moved to this picker).
- **Deleted:** the kit's palette (`kit_command*`, its command state, keys,
  rows, highlighting, backdrop and theme rules): `kit_command.c` 795 to
  357 lines, the kit 549 lines out and 79 in. P1's per-row asset option
  tables and `choices`' asset listing went with them.

`UI_BENCH_PALETTE=1 ./build-release/test_suite --ui-bench studio` (case
18: the Sandbox's commands, settings, objects and assets and 2,000 more
assets, 2,242 results with nothing typed, at 1440×900, pixel scale 2),
the kit build (`d84bc707` with the same case, driven by keys) and this one
alternated process by process, three pairs, 1-minute load 8.4-10.8, so
the times are provisional; ms, the mean of the pairs' medians of 31:

| Case | Kit | Authored | Ratio | Views | Layouts | Elements in its document |
|---|---:|---:|---:|---:|---:|---|
| Open, 2 UI frames | 86.7 | 8.44 | 0.10× | 1,039 | 2 | 273 (kit 15,717) |
| A keystroke, "qa" to "qab" (eight a group) | 4.95 | 4.20 | 0.85× | 134 | 1 | 121 (kit 82) |
| A keystroke, "" to "b" (every result, then a few) | 57.9 | 4.34 | 0.075× | 354 | 1 | 273 |
| Move the highlight | 39.9 | 2.70 | 0.068× | 1 | 1 | |
| A UI frame with nothing changed | 11.6 | 1.23 | 0.11× | 0 | 0 | |

A keystroke spends 0.7-1.0 ms in sync (ranking 2,242 entries and writing
the window) and 1.2-1.4 ms in update; the kit spent its time emitting and
laying out every row. Web shots (1600×1000, dark and light, compact and
comfortable) before (`d84bc707`) and after: the palette empty, `toggle`
with Down, `?`, a query nothing matches, the comfortable density's, the
Document picker open and typed in, and the rename popover before and
after Enter are identical pixel for pixel but for a caret; intentional
differences: a query's results keep their group's heading in view (the
kit scrolled its first row to the top, hiding it), the bold of a typo
match, and the picker's placeholder.

Checks: `--studio-palette` (also in `--studio-documents`): ⌘K's focus and
window, Up, Down, Ctrl+N, Ctrl+P, PageDown, End and Home each in view, Tab
and Escape, ranking under a heading with bold and spaces, a described
row's height, the pointer and a still pointer, the empty state and a press
outside, the picker's current asset, ranking, Enter as one Undo step and
`Use "name"`, the field and Escape closing it, Add Component's groups, and
the refactor popover's rename with its Undo and a used asset's delete;
`--studio-panels` opens and closes the palette, a picker and the refactor
popover every round with no RmlUi warning and the same views;
`--ui-lists` rows of several heights; the palette checks in the shell
surface retargeted.

Open: graph pins' asset pickers, graph literal dropdowns, the member type
dropdown and source autocompletes stay kit dropdowns (W4 G1, since done
but for a node body's source field); cmdk's
pages and group jumps (Alt+Up/Down); a choice-list's own filter does not
reorder by score, as `kit_select` did.

#### C3: the shell (done)

The shell's chrome is authored; only each area's content, the editors not
yet migrated, stays kit-emitted, as a native slot of the Workspace.

- **Workspace** (`Workspace.rml`, `.rcss`, the `workspace` source in
  `studio_shell.c`): the areas' places (`#area-<key>`), the splitters between
  them, the drawer under the main areas and the side column, and a maximized
  area filling it all, as Blender's areas. C keeps the arrangement (ratios,
  open states, maximize, per layout mode) and computes each place's extent
  from the columns' last laid-out sizes with the kit's formulas, whole dp, so
  the places sit where the kit put them. Each area is still a document of its
  own, emitted into its place with `kit_slot_begin`; the slot follows the
  area, not the layout, so maximizing, restoring or switching modes keeps an
  area's document, its kit state and its Text Editor (the kit's paths changed
  with the parent and rebuilt them). Kit paths are now
  `app/workspace/<key>/area/...`.
- **Splitters** are `<splitter>`s sending `emit('workspace.resize', which,
  ev.value)` on `input`; the action resyncs the source at once
  (`studio_ui_resync`), so the places move in the frame the input arrives,
  as the kit's did. The arrangement is a preference: no Undo step. Their
  hit areas reach 2 dp onto the areas beside each 6 dp gutter (10 dp; 8 dp
  where the drawer and the side column clip it), as Unreal's `SSplitter`
  hit-tests a wider handle than it draws: the Workspace's document lies over
  the areas (`z-index: 1`) with `pointer-events: none` but at its splitters.
  Popovers (9) and the kit's layer (10) stay above it.
- **Transitions:** the drawer eases open in 200 ms and shut in 100 ms and
  the side column both ways in 200 ms, as RCSS transitions on `height` and
  `width` (`quadratic-out`, close to the kit's critically damped spring)
  toggled by `data-open`; a splitter changes the size variable, which moves
  them directly, and Reduce Motion removes the transitions. RmlUi resolves a
  transition's target without the element's inline properties, so
  `--drawer` and `--side` live on `.workspace`. C keeps an easing-shut area
  until its place is empty, and wakes UI frames while any area's box moved
  since the last emission (`studio_areas_moved`), so the kit content in it,
  such as the Scene's surface, follows each frame, as the springs' frames
  did. Maximize stays instant, as before.
- **Top bar:** a `<menubar>` of `.trigger` buttons and `Menu`s: MenuCommands
  over `commands`, MenuGroups, Submenus, and File's and Object's lists from
  the `menubar` source (New ▸ Project, Open Project, the browser's Import
  and Export Project, the scene's prototypes for Add), with actions
  `menubar.newProject`, `.openProject`, `.add`, `.importProject` and
  `.exportProject`. The play target (its morphing label), the playback
  speed (on while not 1×) and the layout menu read the `shell` source and
  send `shell.target` and `shell.speed` or run the layout commands; a
  paused run's Step Over, Into and Out are buttons over `commands`. The
  status bar's UI Scale menu reads `shell.scale` and sends `shell.scale`.
  Blender's and Unreal's menubars are the reference: a press opens, hovering
  another trigger switches, Left and Right step, typeahead within (no Alt or
  F10 binding, as before; F10 is Step Over).
- **Shared fixes:** popover.c marks an open popover's trigger with
  `data-popup-open` (Base UI's; the menubar's trigger stays pressed while
  its menu is open, as the kit's did) and gives every popover
  `--available-height`, the window's height less its margins, so an
  authored menu taller than the window scrolls (`popup.rcss`), as the kit's
  did; a press on an open authored popover keeps the area the command acts
  on, as a press on the kit's layer did (the Edit menu over the Scene);
  the kit frees a faded toast before its toaster emits, so the last one's
  node leaves in the emission its fade asked for (a deadline check had
  passed only while `kit_spring` kept frames coming).
- **Deleted:** the kit's `kit_menubar_trigger` and its trigger press,
  `kit_splitter` and `splitter_take`/`splitter_active`, `kit_spring` and its
  springs; the shell's kit menus, menubar, play target, layout control,
  speed control, debugger steps and menu, project menus, UI Scale control
  and the kit-built workspace. Across `src`, 788 lines went and 638 came
  (the sources' data and the workspace's geometry); `studio_shell.c` grew
  from 1,624 to 1,713 lines.

`UI_BENCH_SPLITTER=1 ./build-release/test_suite --ui-bench studio` (case
15: Studio over the Sandbox at 1440×900, pixel scale 2; the main splitter
dragged 3 dp, then the chrome at rest and after a selection), the
integration branch before C3 (`cf21ee38`, over its own resources with
`CHIBI_RESOURCE_DIR`) and this one alternated process by process three
times, 1-minute load 22-26, so the times are provisional; the range of the
runs' medians in ms:

| Case | Kit | Authored | Ratio | Layouts | Views | Elements |
|---|---:|---:|---:|---|---:|---:|
| A splitter dragged 3 dp, the two UI frames after | 6.26-6.52 | 3.96-4.30 | 0.65× | 3 / 3 | 0 / 46 | 2,355 / 3,480 |
| The chrome: a UI frame with nothing changed | 0.95-1.04 | 1.20-1.22 | 1.20× | 0 / 0 | 0 / 0 | |
| The chrome: select another object | 7.12-7.59 | 8.18-8.44 | 1.14× | 4 / 5 | 550 / 668 | |

A drag step lays out three documents in both: the kit's shell document
and the two areas, or the Workspace's and the two areas; the shell
document held the kit-built workspace, so the step's update fell from 3.9
to 1.4 ms. The kit resized the areas in the frame after the drag, the
Workspace in the same frame. The idle frame's 0.17 ms more is sync (+0.06:
three more sources; `menubar` walks the scene's prototypes for Add's icons
at each refresh) and emit (+0.12: the Workspace's slots and place
queries). A selection costs about 1.1 ms more: the closed menus hold about
1,100 elements and their views, and the Play target's Preview row and the
commands' states they show change with the selection, laying out the top
bar's document once more. **Follow-up, shared with F3 (done):** a popover's
content is built only while it is open, as the kit's menus were
(F3, above): at rest the top bar's document holds 99 elements, and a
selection evaluates 267 views, not 862.

Shots, before C3 (`3777b63b`) and after. Native, in a hidden window at
2560×1600 (`CHIBI_SHELL_CAPTURE_DIR`): dark and light, compact and
comfortable at UI Scale 100%, dark compact and light comfortable at 150%;
the window at rest, File, View, Object ▸ Add, the play target's, layout's,
speed's and UI Scale's menus, the drawer open, an area maximized, the side
pane closed and Horizontal Split: 55 of 72 pairs identical pixel for pixel;
the rest differ only in the UI Scale menu's labels by a subpixel (the menu
sits against the window's right edge, and its width differs from the kit's
measured one by a fraction of a pixel), the 150% View menu's shortcut
column and scrollbar by 1 px, and the Scene's and the graph's zoom
readouts, caught mid-roll. Web, 1600×1000 at DPR 1, dark and light,
compact and comfortable, the same states plus a splitter hovered and
dragged: 52 pairs whose chrome matches; they differ in the Scene's live
world generation, and in comfortable a pointer on a gutter's edge row now
tints the splitter (its hit area reaches past the gutter). Object ▸ Add's
long prototype names fit since Submenu's room (before it they clipped at
the parent menu's width).

Checks: `--studio-shell` (new; also in `--studio-documents`): the menubar's
press, hover switching, Right and Escape, a MenuCommand running, Object ▸
Add listing and adding a prototype; the play target's menu, a speed preset,
the layout menu; UI Scale's menu checking the scale and setting another;
the areas over their places, a splitter's 10 dp hit area and a drag that
resizes in the frame its input arrives; the drawer easing open and shut and
leaving its place; the guest Studio's areas; layout modes, side pane,
maximize; with `CHIBI_SHELL_CAPTURE_DIR` set, native shots of every state
in six variants. `--studio-panels`: the panel turns check also changes the
drawer's and the side column's editors and eases each open and shut, with
no RmlUi warning and no views piling up. `--kit-documents` (`kit_slot.c`):
the authored top bar's menubar, play target and paused steps;
`--rmlui-kit`: the kit's menu checks over a plain trigger.

Open: the area headers' editor controls are still native slots (C1, C2);
a menu taller than the window keeps clipping its longest labels by its
scrollbar's width (the View menu at 150% UI Scale), as the kit's did.

#### C1: the Scene area (done)

The Scene area is projects/studio's `Scene` (the overlays over the scene)
or, while a ui asset previews, `Canvas` (the stage and the Actions log),
with `SceneHeader` in its AreaHeader's controls for both. The surfaces stay
native: the scene's `kit_surface` (or a guest Studio's) in `#scene-surface`
and the Canvas's frame in `#canvas-frame-slot`, where pointer input, the
gizmos, the camera, the view zoom's crop and drops stay C's. Unreal's
viewport toolbar (Show flags in a View menu, snapping with its increment
beside it) and Unity's Scene view toolbar (tools as a pill, the grid
snapping increment) are the references, as before. Three sources:

- `scene` (`studio_scene.c`, synced while SceneHeader shows): mode (the
  scene's controls, a guest Studio's name or the Canvas's), View Zoom's
  label, levels and whether it shows, and the grid increment's label and
  presets. `scene.zoom` and `scene.increment` choose one by index. The tools,
  the magnet, Show's toggles, Reset Camera, View Through Camera and Frame
  bind `commands` (MenuCommand in the View menu) and run `studio.<id>`, so
  their state, tips and shortcuts come from the command table.
- `scene_view` (`studio_scene.c`, synced while Scene shows): each camera
  frame's label, the label naming the scene and selection, the camera's
  zoom (`<number-flow>`, built by `data-when` once the scene has drawn, so
  it never rolls from the reset camera's), Live, the generated world's
  progress and the notice line. A label moves by `translate()` in whole
  pixels, as layout placed it, so a pan repaints it without laying out.
- `canvas` (`studio_canvas.c`, synced while Canvas shows): the previewed
  asset, what uses it and the ui assets to preview instead (the crumb's
  Menu and Submenu), the screen size, the backdrop, the stage's label, the
  game UI's error and the Actions log's List window, which follows new
  events while at its end. `canvas.back`, `.used`, `.preview`, `.size`,
  `.backdrop`, `.actions` and `.clear`.

**Panel swaps and idle refreshes.** The Scene and Canvas take turns in one
area's host, which first showed the view leak the integration branch fixed
(cf21ee38: the host empties before it takes the next panel's data model);
`--studio-panels` turns the Scene editor with the other authored panels and
swaps Scene and Canvas in it each round, with no RmlUi warning. The kit's
zoom readout re-rolled for about half a second after an area resized, which
kept the UI refreshing every frame; the authored Scene idles once settled,
at the 4 Hz idle refresh. Output's Log checks had leaned on those extra
refreshes after printing, so they now wake two refreshes after a print, as
an idle Studio takes new lines at its next refresh.

Styles moved from `theme/scene.rcss`, `theme/scene_view.rcss` and
`workspace.rcss` into `SceneHeader.rcss`, `Scene.rcss` and `Canvas.rcss`;
the scene's dark roles in light themes and the comfortable Actions row stayed
in `workspace.rcss` while scoped RCSS couldn't see the body's classes; they
moved to `theme/viewport.rcss` (imported by both) and `Canvas.rcss` with
that fix (Shared follow-ups, above). The
header's buttons keep the area header's control height, as the kit's did
(AreaHeader's `.btn` rule won over `.tool-group .btn` there).

**Parity.** Web shots before (`f7edf3ec`, 1600×1000) and after, dark and
light, compact and comfortable: the scene with its overlays, a tool's and
the magnet's tooltips, the View menu, the increment menu and a chosen
increment, 200% with its View Zoom button and menu, the Canvas, its Actions
log with a logged event, the crumb's menu and Preview submenu, Phone on the
light backdrop, back to the scene, and Play's Live badge: 68 pairs, identical
pixel for pixel but for an action's clock, the game's own frame in Play and
the world's progress badge after Stop, which ran at another moment.
Native shots (hidden window, 2560×1600; `CHIBI_SCENE_CAPTURE_DIR` with
`--studio-scene`) of the scene, 200%, the Canvas with its Actions log open and Play in
the four variants: identical but for the readout's roll and the Pause
button's press fade, caught mid-animation. Keys are unchanged (W, Q,
Shift+G checked on the web build) and the guest Studio runs in the Scene
area with its own authored Scene. Intentional difference: the Canvas's
screen sizes are buttons, as Output's view switch is, so Left and Right no
longer step them (Tab and Enter do).

`UI_BENCH_SCENE=1 ./build-release/test_suite --ui-bench studio` (case 16:
the Sandbox at 1440×900, pixel scale 2, its Camera's frame labeled; the
camera moves 8 world units diagonally and back, or zooms a notch in and
out, and is drawn before the timed UI frame), the kit build (`f7edf3ec`)
and this one alternated process by process six times, which arm ran first
alternating, 1-minute load 35-38, so the times are provisional; medians of
the runs' medians:

| Case | Kit ms | Authored ms | Ratio | Views evaluated | Layouts | Elements in the Scene's document |
|---|---:|---:|---:|---:|---:|---|
| A pan step | 1.30 | 1.31 | 1.0× | 16 (kit 0) | 0 | 579 (kit 359) |
| A zoom notch | 1.44 | 1.46 | 1.0× | 16 (kit 0) | 2, the readout's roll | |

Neither build lays the Scene out for a pan: the kit's labels moved by
`left` and `top` on absolute boxes, which RmlUi places without a layout,
and the authored ones move by transform. A zoom lays out twice in both,
for `<number-flow>`'s digits. The authored document held 220 more
elements, the header's closed menus and `data-when` templates, which
neither lay out nor draw (the menus no longer: F3 follow-up). Checks: `--studio-scene` (also in
`--studio-documents`): the tools run their commands, pressed while active,
their tips naming them and their keys; the magnet and the increment menu;
the View menu's Show toggles and View Zoom, whose button shows only while
it magnifies; Frame disabled without a selection; a camera label following
a pan by transform with no layout; the scene-and-selection label and Live;
the Canvas's size, backdrop, Actions log and Clear, and its Scene crumb; a
guest Studio's header.

#### C2: editor chrome (done)

The Behavior header, the graph's find bar, the Text Editor's header and
find bar and Performance are authored; the graph canvas and Zep's surface
stay native. Unreal's graph editor toolbar (Back and Forward, the
breadcrumb of the calls entered, the debug object menu) and VS Code's find
widget are the references, as the kit's were.

- **`<line-chart>`** (`src/ui/controls/line_chart.c`, any context): the
  kit's chart moved out of the kit. C publishes series by name
  (`line_chart_publish`: times, values, color and the time at the right
  edge), as textures are named `kit:...`, and a chart draws the series its
  `series` attribute names, the first in its `color`; `window`, `unit`,
  `precision`, `reference`, `min`, `max`, `fill` and `live` as the kit's
  desc had them. A publish that changes a series redraws the charts that
  name it: the geometry is rebuilt at render and the labels move by
  transform, so samples lay nothing out. The element hears the pointer
  itself for the scrub readout. `kit_chart.c` (669 lines) is gone; the
  kit's chart checks in `--rmlui-kit` drive the element.
- **Performance**: `Performance.rml` and `PerformanceHeader.rml` over the
  `performance` source (readout, live, running, gpu, lighting,
  lightingText, budget). Each 20 Hz (4 Hz idle) publication hands the
  samples to the charts as `perf:frame`, `perf:behavior`, `perf:physics`,
  `perf:systems`, `perf:rendering` and `perf:gpu`; the readout is a
  `<live-text>`. Its styles left the theme for the panel's `.rcss`.
- **Behavior header**: `BehaviorHeader.rml` over four sources by rate of
  change (`studio_graph_header.c`): `graph_header` (crumbs, Back and
  Forward, rename, the debug target's label), `graph_menus` (the picker's
  graphs and items, the assets of the behavior's kind), `graph_view` (the
  zoom and the Graph menu's items, which the selection enables) and
  `graph_targets` (the debug target's choices). The View menu is GraphView
  used as a widget, its own entity and prototype gone. Visits are tracked
  as the header syncs (`sg_nav_track`), as its emission did.
- **Debug target picker**: a ChoiceList, searchable, of No target, then
  Follow newest and Break when spawned per prototype that attaches the
  behavior, then the scene's and the spawned instances, as the kit's
  dropdown listed them; the keywords stay. The kit headed Scene and Spawned
  each time the attachment order switched between them; now each is one
  group. Playback speed is the top bar's (C3), not this picker's.
- **Find bars**: `GraphFind.rml` (the query, its count, Previous, Next,
  Close and a List of TreeRows over the `graph_find` source, eight rows
  tall at most, the current one revealed) and `CodeFind.rml` (`code_find`),
  each mounted in a `.find-widget` while open. Keys are unchanged through
  `data-keys`: Up, Down, Enter and Escape in the graph's, Enter, Shift+Enter
  (the activation's modifiers), Up, Down and Escape in the text's; Find
  focuses the field as before. Closing the graph's now gives the keyboard
  back to the graph's area, as the text's returns to the text.
- **Text Editor header**: `CodeHeader.rml` over `code_header`: a UI
  document's Back and Forward, part, file with its unsaved dot, Find, Vim's
  mode, the problems' count and Invalid chip, and the apply mode; an
  asset's KDL source's name, Unsaved, Save and apply mode.
- **Fixes**: the kit's rename field in the crumbs had no width, so the
  name typed was invisible; the authored one is 160 dp at most. The
  rename field's blur after Escape doesn't commit (the rename ends first).
  A find bar opened without input (a command run from code) shows at the
  next UI frame.

Web shots (1600×1000) before (`3777b63b`) and after, dark and light,
compact and comfortable: the header, the graph picker for the event graph
and for a function, the asset crumb's menu, the Graph menu, the find bar
with matches, the debug target picker in Play, and the Text Editor's
header and find bar match pixel for pixel but for a caret's blink and, in
Play, the Scene's live render. The asset crumb's Behaviors submenu clips
long names at the menu's width until C3's Submenu, which fits a submenu
to its widest row, merges; with that fix it matched too. Performance, idle and
in Play, matches in its headings, legend, ticks, badges and readout; its
samples differ run to run. The rename field is the intentional
difference above. Native shots (`UI_BENCH_CAPTURE=<dir>` on the Studio
bench: the header, graph picker, Graph menu, find bar and Performance in
Play, dark and light, compact and comfortable, reduced motion, 2880×1800)
of the kit build and this one match in the Behavior area within 1/255;
Performance's charts differ only in their samples.

`UI_BENCH_PERFORMANCE=1 ./build-release/test_suite --ui-bench studio`
(case 17: Performance in the bottom area while the Sandbox plays, 1440×900,
pixel scale 2), the kit build (`3777b63b`) and this one alternated process
by process three times, 1-minute load 40-44 (provisional); ms, the range
of the runs' medians:

| Case | Kit | Authored | Ratio | Layouts in 31 runs | Views | Elements in its document |
|---|---:|---:|---:|---:|---:|---:|
| A new sample and readout | 1.46-1.49 | 1.55-1.57 | 1.05× | 0 / 0 | 0 | 350 / 401 |
| No new sample | 1.14-1.19 | 1.23-1.34 | 1.06× | 0 / 0 | 0 | 350 / 401 |

Samples reach the charts without the data model (no views evaluated) and
lay nothing out in either build; emit fell (0.34 to 0.32 ms) and update
rose (0.42 to 0.48 ms) for the charts' update hooks and the panel's 51
more elements (the hidden legend, idle line and GPU chart variants).

Checks: `--studio-chrome` (also in `--studio-documents`): the chrome's
documents check clean; the crumbs and graph picker; Back and Forward;
rename in place with Enter (one Undo step) and Escape; the find bar's
matches, Down, Enter (to the node, closing, the keys back to the graph)
and Escape; the debug target picker's Follow newest, Break when spawned
and No target in Play; the Text Editor's part, apply mode and find bar
with Enter, Shift+Enter and Escape back to the text; an asset source's
header; Performance's charts and legend in Play. `--studio-panels`' area
turns now include the Text Editor and Performance. `--ui-controls`: a
`<line-chart>` showing published series, redrawing new samples without a
layout, its scrub readout and a forgotten series.

Open: two Behavior areas share one header's data, as one Text Editor's;
`graph_targets` follows the instances while the header shows, the picker
closed or not. The bench ran before the integration branch's view leak
fix (`cf21ee38`) merged.

### Wave 4: graph chrome, Designer, Inspector (3 parallel)

| | Scope |
|---|---|
| G1 Graph chrome | Node, wire, pin and comment menus; add-node search; Node and Member tabs as PropertyRow kinds (binding chip, source autocomplete, expression field, type picker, reorder grips) |
| D1 Designer (done) | Hierarchy (TreeRow, drop zones), Details (lengths with units, colors, keywords, class chips, add popovers), Library (Tile): D1, below |
| I1 Inspector (done) | Its tree on `List`, removing the 1,000-row cap; `InspectorRow` is gone: I1 results |

#### I1: the UI Inspector (done)

The Inspector's Elements tree is a List of TreeRows, so it shows every
element of every document, as Chrome's Elements panel does, where it stopped
at 1,000 rows (and told the user to collapse some). `Inspector.rml` keeps its
toolbar and details panes; `InspectorRow` is gone. The `inspector` source
(`studio_sources.c`, the Inspector section; the model is `studio_inspect.c`)
keeps three variables, each dirtying only the views that bind it:

- `inspector`: the toolbar, target and the selection's box, styles, computed
  values and attributes, as before, without the rows. The panel still writes
  `rule`, `action`, `target` and `force` there.
- `inspector_tree.rows`: the window of rows the List shows (`key`, `depth`,
  `label` (the tag), `id`, `classes`, `widget`, `text`, `parent`, `open`,
  `node`, `hidden`), the strings pointing at the model's, as the details
  do, so a scroll step allocates nothing.
- `inspector_ui`: the `selected` and `picked` keys, apart, as `outliner_ui`
  does: a selection or a pick re-evaluates only TreeRow's two key
  comparisons (`data-alias-selection`) and the `data-picked` mark.

How it avoids a row cap: `studio_inspect.c` walks the open tree once per sync
(`walk`, which describes nothing: a list of element, depth, parent row and
child index), so the tree's size and the selected and picked rows are known;
`ui_list_window` then asks for the window, and only its rows are described
(tag, id, classes, `data-ui`, text) and placed (`place_of`, the
`element_ref` each is found again by). The `studio_inspect_read` API the
unit test uses lists every row.

- **Keys name elements, not rows.** A row's key is its element's address as
  text, only compared, never followed, so a click names the element it was
  drawn for however the rows above it changed since: a first version keyed
  rows by their index and mis-targeted a click whenever a document came or
  went between the frame the user saw and the frame that handled it (a
  popover or toast is a document). `inspector.click` takes the key and the
  row's open state.
- **Folding.** The twisty is TreeRow's, which flips `item.open` in the data
  and lets the click reach the row; the row's `emit('inspector.click', row.key,
  row.open ? 1 : 0)` sends the flipped state, and C, finding it differs from
  the one it showed, expands or collapses the element and does not select it.
  A click anywhere else on the row selects.
- **Hover.** Unchanged for the user, and no events: each frame the Inspector
  reads the `data-inspect-row` of the row under the pointer from the DOM
  (`rml_context_hover_element`, up its parents) and highlights that row's
  element in the app. The old panel wrote `inspector.hover` on every
  mouseover, which dirtied the whole variable.
- **Pick.** Picking marks the row of the element under the pointer
  (`inspector_ui.picked`, `data-picked`); a click selects the element, opens
  its ancestors and scrolls its row into view (Chrome's reveal). RmlUi clamps
  a scroll to the content laid out, and a deep element in a short tree needs
  the spacer laid out first, so `window` asks `ui_list_window` again at each
  sync (waking the UI) until the row is in view, up to 8 syncs. The List's own
  reveal counts a row from the scroller's top padding, so the tree has none.
- **Mirror.** The tree lists the Inspector's own area as it does any
  document, but the panel's List is a leaf, `N rows`, as the old tree's
  container was (its rows are the window of this very tree).
- **Intentional differences:** TreeRow's indent (14 dp a level, the
  Outliner's) for the old 12 dp; the tree has no top padding; the note
  `Collapse rows to list more` is gone with the cap. Keyboard: unchanged.
  The Inspector has no arrow-key navigation, as before; `Mod+Shift+C` and
  Escape stay in `studio_inspect_key`.
- **Bench:** `--ui-bench studio` case 20 (`UI_BENCH_INSPECTOR=1` runs it
  alone): the Inspector in the Properties area over a document of 10,000
  elements whose items are open in the tree (372×214 dp, 1440×900, pixel
  scale 2), a release build before (`3160b5f9`) and after, alternated process
  by process three times at a 1-minute load of 76-80 (provisional), whole
  Studio UI frame, medians of the three runs:

  | Case | Before (1,000 rows) | After (window of 19) | After ÷ before | Views | Layouts | Elements in its document |
  |---|---:|---:|---:|---:|---:|---:|
  | A UI frame with nothing changed | 10.3-10.7 ms | 4.2-4.8 | 0.41-0.45× | 0 / 0 | 0 / 0 | 9,645 / 875 |
  | Select another element in view | 27.6-29.5 | 6.0-7.1 | 0.21-0.25× | 17,176 / 308 | 4 / 4 | 9,645 / 875 |
  | Scroll step of 3 rows | 11.5-12.6 | 7.3-8.0 | 0.64× | 0 / 494 | 0 / 2 | 9,645 / 875 |

  The window holds 19 rows whatever the tree's size and keeps its elements
  as it scrolls (0 instanced a step), where the old panel instanced 1,000 and
  evaluated 17 views a row on every selection. A scroll step reads 494 views
  (a window row's, as the Outliner's) where the old scroller read none, but
  it could scroll only the first 1,000 rows. Sync rose 0.85 to 1.1 ms: it
  walks all 10,000 open rows at about 90 ns each (the old one described 1,000).
- **Checks:** `--studio-inspector` (new, also in `--studio-documents`): each
  document a root row, a click selecting and the twisty folding without
  selecting; ten thousand elements as a window of 18 rows that keeps its
  length and elements as it scrolls to the middle and the end, reveals the
  selected last item from the top and folds; the hover highlight following
  the row under the pointer and stopping when it leaves; a deep element
  selected in a short tree scrolling into view, while picking marking the
  hovered element's row, and a click selecting it. `--ui-inspector` reads
  the window's selected row.
- **Open:** two areas showing the Inspector share one window and one List
  (the first's); the sync walks the open tree at every refresh while the
  panel shows, as the shared follow-up on change-driven sync describes (the
  DOM has no revision to compare, so the walk's cost is the open rows).

#### G1: graph chrome (done)

The graph canvas's context menus and Properties' Node and Member tabs are
authored; node bodies stay kit-emitted (Postponed). Unreal's Blueprint
editor is the reference, as the kit's was: its graph context menus (a
node's actions with their keys and the Select submenu, a pin's Break Link
To and Reset to Default, a wire's Straighten Connection), its Details
panel for a node's inputs and for a variable or function (Name, the pin
type picker, Container, Category, Default Value, a function's inputs and
outputs with grips), and its comment colors. The source field is Base
UI's Combobox over cmdk's list; Blender's node sidebar and Godot's
Inspector agree on editing a node's inputs beside the graph.

- **Canvas menus** (`GraphMenu.rml` over `graph_menu`,
  `studio_graph_menu.c`): a node's, a pin's, a wire's and a comment
  frame's, each a Menu with its heading, MenuItems with icons and keys,
  Submenus (Select, Alignment, Break Link To, a frame's Color with a dot
  per color and the current marked) and separators. A menu is written
  once in C, as the kit's immediate menus were: described, `sg_menu_item`
  adds an entry and returns false; run with the key a click sends, only
  that item returns true and its code acts, one Undo step. The document
  mounts while a menu is open, at the pointer (`popover_show`); opening
  one takes focus from a field, so its keys reach the menu.
- **Node tab** (`Properties.rml`'s graph section over `graph_details`,
  `graph_values` and `graph_details_ui`; `studio_graph_details.c` writes
  the rows, `studio_graph_properties.c` fills and edits them): an input
  per row as its reflected type asks (`GraphField.rml`'s controls: a
  source field, the binding chip, numbers and vectors that scrub, a
  checkbox, an EnumField with every value, an AssetField, a color with
  its alpha, a text or a multi-line text), struct and array values in a
  popover with Add item and remove, an inline source's inputs in a
  popover, the expression field with its error shown under it, the
  outputs, and the Node panel (Disabled, Breakpoint, Comment, Show
  Comment, Show on node). A comment frame's tab edits its title and
  color. A row's id names what it edits (`n12.i3`, `n12.i3/a1/f0`, `c4.t`);
  the fast values (a scrubbed number) are their own variable.
- **The source field**: one field per input, as CLAUDE.md asks: typing a
  literal offers it first and Enter sets a constant; typing a path lists
  the fields and outputs that fit. Its `<input role=combobox>` sits in a
  virtualized `<choice-list>` whose rows are the palette's
  (`STUDIO_PALETTE_SOURCES`, `studio_palette_open_field`), shown under the
  field: focus opens it, typing ranks, Enter or Tab sets the highlighted
  row, Escape shuts it keeping the source.
- **Member tab** (`studio_member_properties.c`): a variable's Name, Type,
  Container, Category and Default; a function's Name and Pure and its
  inputs and outputs, each a row with a grip (F4's `data-drag` and
  `data-drop`; dropped on another row it takes its place), name, remove,
  type, container and an input's default; a local's. Type is a button
  that opens Studio's picker anchored to it (`STUDIO_PALETTE_TYPES`).
- **Canvas pickers**: a node body's enum, choice and asset fields open
  Studio's picker under the field's box in the canvas
  (`studio_palette_open_box`, `studio_pick_open_box`;
  `STUDIO_PALETTE_CHOICES` lists every value). The kit's searchable
  dropdowns there are gone.
- **Edits** stay in C behind `graph_details.input`, `.change`, `.check`,
  `.choices`, `.type`, `.rename`, `.container`, `.remove`, `.add`,
  `.addPin`, `.reorder`, `.fold` and the source field's `.source`,
  `.query`, `.escape` and `.tab`; each commit is one Undo step and a
  scrub coalesces into one. A checkbox's binding `change` and an
  unchanged value make no step. A refused edit (a taken name) redraws its
  field with the model's text. Each tab's file keeps its capabilities
  (renames, retypes, defaults, signature edits, node flags, frame title
  and color, over nodes, members, pins and values) apart from the
  intents that read the rows' events into them, so each can become an
  editor capability node; the Members panel and the Member tab share
  `sm_rename`.
- **Closed pickers build nothing**: EnumField's popover and the graph
  section's popovers carry `data-template` (fx-popovers).
- **Deleted**: the native Node and Member tabs and the `#native` slot's
  graph branch, the kit's member type dropdown and graph literal
  dropdowns, `kit_choice_items`, `kit_dropdown_open_at`,
  `kit_dropdown_open`, `kit_dropdown_query`, `kit_command_item`'s shortcut
  and image, `sm_swatch` and the eight-output cap on Name Value
  (`STUDIO_VALUE_NAMES`); `kit_dropdown` is the kit's own
  (`dropdown_items`), serving `kit_select`.
- **Fixes**: the base's Member tab never showed its type dropdown
  (`studio_member_palette` had no caller); the picker opens now.
  Checkbox bindings no longer add an Undo step when a row rebinds.

Keys are unchanged: menus keep Menu's (arrows, Enter, Escape, typeahead,
Right and Left into and out of a submenu); the source field keeps the
kit autocomplete's Enter, Tab and Escape and adds the list's Up, Down,
PageUp, PageDown, Home, End, Ctrl+N and Ctrl+P (`studio-keys.md`).

Web shots (1600×1000) before (`3ceb236f`, `99aaea5b` plus the frame
clock) and after, dark and light, compact and comfortable
(`.shots/base4`, `.shots/new4`, `.shots/base5`, `.shots/new5` in the
worktree): the Node tab on Find Nearest Entity, Set Aim and an
Expression; the pin and wire menus; and the Member tab on a variable and
on a function with an input match pixel for pixel. Intentional
differences:

- The node menu after a key highlights as Menu does (P5's: the kit
  highlighted its first item).
- The source list is as wide as its field and under it; the kit's was
  wider and shifted left. Its empty state's box is 1 px taller.
- After Escape, Tab and Down leave the source list shut (the kit's
  reopened it); typing reopens it.
- The Container dropdown is EnumField's (P1's): its trigger shows the
  open state and its list is 3 px narrower.
- An enum inside a struct or array value is an EnumField, and a compound
  value's popover lists its fields flat; inside an inline source's
  popover a struct or array value has no nested popover.
- Name Value names every output, not the first eight.

`UI_BENCH_NODE=1 ./build-release/test_suite --ui-bench studio` (case 21:
Properties' Node tab beside the Behavior area over Movement, a Direction
To and an Atan2 spawned, 1440×900, pixel scale 1), the base build
(`3ceb236f` with the same case) and this one alternated process by
process, three pairs, 1-minute load 16-19 (busy; counts and ratios matter
more than ms); ms, the range of the runs' medians of 31:

| Case | Native | Authored | Ratio | Views (lookups) | Layouts | Elements in Properties' document |
|---|---:|---:|---:|---:|---:|---:|
| Select the other node | 2.73-3.00 | 3.41-3.51 | 1.21× | 633 (3,129) | 2 / 2 | 587 / 345 |
| A vector literal's axis grows | 1.54-1.65 | 0.84-0.89 | 0.54× | 8 (32) | 0 / 0 | |
| A UI frame with nothing changed | 0.96-1.51 | 0.56-0.63 | 0.47× | 0 | 0 / 0 | |

A selection change rebinds every row (update 1.9 ms against the kit's
0.7, emit 0.18 against 0.82): `data-for` reuses the rows by index, but
rows whose control kind changes rebuild their `data-when` content. A
literal edit writes one row's values (8 views) and lays nothing out; an
idle frame evaluates nothing, where the kit re-emitted the tab.

Checks: `--studio-graph-chrome` (also in `--studio-documents`): the node
menu's heading, keys and Select submenu, Escape, and Disable as one Undo
step; a pin's menu with Break Link disabled and Reset to Default; a
wire's Delete Wire; a comment frame's Color dots, current and choice; the
Node tab's vector axis as one step, Disabled, the comment and a rebind to
the next node selected; a comment frame's title, and a color dragged and
committed as one step; the source field's list, a typed literal first,
Enter as one step, typed text kept as the list follows and Escape; no
views evaluated while idle; the Member tab's rename, default and type
from the picker each one step, and a function's Add Input, grip reorder
as one step, pin rename and remove. `--studio-graph-inputs` drives a
node body's Interpolation through Studio's picker. `--rmlui-kit` lost
the button- and picker-opened dropdown checks with their API.

Open: the select case's rebuild (a keyed `data-for` would keep rows of
the same kind); a node body's source field stays the kit's autocomplete
with the node bodies.

#### D1: the Designer (done)

The Outliner, Properties and Assets areas show the UI Designer's Hierarchy,
Details and Library while a UI document is active (`studio_designer_active`),
as three more panels of the same area hooks. The Canvas, its guides and its
resize handles stay native (per-frame geometry, Postponed). Unity's UI
Builder is the model for the three (a Hierarchy of elements, an Inspector of
unit-bearing style fields with the element's classes as chips, a Library of
tiles to drag in); Unreal's UMG Designer is the model for dragging a Library
entry onto the hierarchy and for a widget element opening its widget.

- **Hierarchy** is `Hierarchy.rml` (a `List` of `TreeRow`s) and
  `HierarchyHeader.rml` (Filter, Add) over `hierarchy` (the window's rows and
  folds), `hierarchy_ui` (selection and the row menu, apart so a click
  re-reads only its views, as P2's `outliner_ui`), `hierarchy_carry` (the slot
  a row carried with the keyboard marks) and `hierarchy_header`, in
  `studio_designer_hierarchy.c`. Rows are windowed per area
  (`ui_list_window`), keyed, and select, fold (twisty, Left and Right),
  filter and walk as before. A row's `data-drag="ui"` and
  `data-drop-zones="tree"` give the drop's `zone` (a container's top and
  bottom quarters before and after, its middle inside; an element that holds
  nothing takes halves), and TreeRow draws the line or fill; a Library tile
  dropped on a row adds there. The row menu (`Menu` and `Submenu`, mounted
  only while open) and Add list the same `library` items.
- **Details** is `Details.rml` (a `PropertyRow` per value, one control by
  `data-when` on `row.control`) and `DetailsHeader.rml` (search) over
  `details` and `details_add` in `studio_designer_details.c`. Each value shows
  in the control its type calls for: text fields (id, text, attributes,
  bindings), `<number-field>`s for lengths and numbers (the unit beside the
  value, scrubbing, a slider for opacity), `<color-field>`s, `EnumField`s for
  a keyword (two or three keywords show as segments), `AssetField` for a ui
  asset and class chips with a remove button. Every Add button opens one
  popover (`details_add`) whose field suggests what the document names; a
  value the document would not take warns with the reason and changes
  nothing. A scrub's `input`s join one Undo step through the designer's
  gesture key, `change` commits it and Escape puts the value back
  (`ev.canceled`). Panel heads fold (`details.fold`); the open state is the
  designer's, not the model's.
- **Library** is `Library.rml` and `LibraryHeader.rml` over `library`
  (`studio_designer_library.c`): the elements and the project's widgets as
  tiles, filtered by the header, dragged into the Hierarchy or the Canvas or
  clicked to add to the selection.
- Edits stay in C. Each intent (`emit('hierarchy.drop', ...)`,
  `details.change`, `library.add`) becomes one `ui_design_*` text edit, so one
  Undo step; the markup never edits the document. Fast-changing state
  (selection, renaming, the hovered row, the carry) has a variable of its own,
  so a selection reads about 220 views outside Details, not the window's.
- **Capabilities are plain C, apart from the panels' glue**, so they can
  become behavior nodes when panels' logic moves to KDL graphs:
  `studio_designer_values.c` (a property's control, a control's value as
  document text, a new property's start value), `studio_designer_edit.c`
  (attributes, the inline style, which rules match an element, what an Add
  popover suggests, and `designer_set_text`, `designer_set_value` with its
  one-Undo-step gesture and Escape, `designer_remove_target` and
  `designer_add_value`), declared in `studio_designer_edit.h`; the
  Hierarchy's moves, duplicate, wrap and delete stay in
  `studio_designer.c` over `ui_design`. A change returns unchanged, changed
  or refused with the document's reason; `studio_designer_details.c` only
  turns a row id and a control's value into a call and shows the result.
- **Deleted:** the kit's tree rows, drop marks, details fields, popups and
  tiles in `studio_designer*.c` and the Designer's rules in
  `theme/workspace.rcss`: about 1,800 lines out of the C, 73 of the theme.
  The C grew by 1,260 net (3,260 to 4,520 lines in `studio_designer*.c` and
  `.h`) because the sources now publish JSON shapes and handle intents where
  the kit drew, and the capabilities have a header; the fold, carry and
  keyboard code moved from `studio_designer.c` to
  `studio_designer_hierarchy.c`, and Details's row shapes are the largest
  part (1,000 lines). The panels are 110 lines of RML and 109 of RCSS.
- **Keys** are unchanged (`docs/studio-keys.md`); the Hierarchy's keys still
  run from `studio_designer_key`.

Parity: the kit build (`3160b5f9`) and this one, native (shots by
`UI_BENCH_DESIGNER_CAPTURE=<dir> ./build/test_suite --ui-bench studio`: rows,
row menu, Add menu, a row dragged over the middle and top of another, Details
of the root with the Add Class popover and a color picker, of a row with a
keyword open, of a button with a binding, of the document, and the Library
with a tile hovered; dark and light, compact and comfortable, 2880 by 1800)
and web (1600 by 1000, the same states, by a script over `make web`) match
pixel for pixel in the Hierarchy and the Library. Details matches but for the
keyword dropdown's width, which is EnumField's (the list is as wide as the
field's value, P1's known deviation). Re-shot on the web after merging
`00e3ceb8`, against that branch's kit Designer, all 52 states (dark and
light, compact and comfortable) match pixel for pixel but for that dropdown;
native shots after the merge (1x) were looked over, not compared. One kit
detail was kept as the kit showed it, not as an earlier design said:
Bindings values use the body font.

Checks: `--studio-designer` (21, also in `--studio-documents`, in
`test/editor/studio_documents_designer.c` over the arena's HUD with in-memory
text): the Hierarchy's rows and counts; a click selects and a twisty folds
without selecting; the filter and its empty state; the drop marks (inside,
before, after); drops inside a container and before a leaf; a Library tile
dropped on a row; the keyboard carry (arrows, Escape, Space); the row menu
(Duplicate, Delete, Open Widget only for a widget) and the header's Add;
Details' text commits (Enter and blur), the class chips and the Add popover
(typed and picked), a length's scrub as one Undo step with Escape putting it
back, opacity as a slider, a color in its picker, a keyword from its list and
as segments, an inline property's remove, the document's rules with nothing
selected, an invalid Add and the heads folding; the Library's tiles, its
filter and a click. `--studio-panels`' area turns now run a second time with a
UI document open, so the Hierarchy, Details, Library and Canvas turn with the
other editors and bind each one's views anew with none lost.

`UI_BENCH_DESIGNER=1 ./build-release/test_suite --ui-bench studio` (case 22:
the arena's HUD with 40 added spans, 49 elements, the Hierarchy, Details and
Library beside the Canvas, 1440 by 900, pixel scale 1), the integration
branch's kit Designer (`00e3ceb8`) and this build alternated process by
process three times, 1-minute load 32-36 (provisional; the counts do not
depend on load). ms, the range of the runs' medians:

| Case | Kit | Authored | Ratio | Views | Layouts kit / authored | Elements in its document |
|---|---:|---:|---:|---:|---:|---:|
| A UI frame with nothing changed | 1.16-1.26 | 0.80-0.97 | 0.63-0.84x | 0 | 0 / 0 | 877 / 1,169 |
| Select another element | 3.65-3.73 | 4.56-5.15 | 1.2-1.4x | 1,406 | 4 / 4 | 769 / 1,009 |
| Select another button (same rule, same rows) | 3.27-3.54 | 4.41-4.95 | 1.35-1.45x | 904 | 4 / 4 | 736 / 901 |
| Write the root's inline width (re-parses, rewrites Details) | 1.74-1.86 | 1.41-1.59 | 0.76-0.86x | 333 | 0 / 0 | 864 / 1,118 |

The first build of this wave, over `3160b5f9`, measured a selection at 1.6x
the kit's (7.1 against 4.4 ms): the Hierarchy's row menu, its Add menus and
Details' Add popover stayed built while closed. The integration branch's
`data-template` Menu and Submenu mount them only while open, which took it to
the table's ratio. What remains is Details. Measured before that merge, with
Details's sync skipped a selection cost 2.6 ms and read 218 views, so Details
was about 1,400 of its 1,600 reads and two thirds of its time. Every view
under `details`, one variable, is evaluated on a selection. The cost is the
row structure, not a widget: one `data-attr` on any element inside a row's
`data-when` branch adds about 260 reads to a selection, whatever it binds and
however many follow it, where the same branch with no view adds none
(swapping the color field for a `<span>` with one attribute reproduces it),
while an edit that changes one value stays at 333 views and no layout. Not
explained; the data model's owner may know. A selection costs 1 to 1.5 ms more
than the kit's frame.

Open: Details's JSON builders are the largest part of the C that remains and
could shrink with a table-driven row shape. The Canvas keeps its guides in the
kit's immediate mode. Observed in shared code, not changed here: EnumField's
list is only as wide as its value; `data-for` leaves its hidden template as a
sibling, so `:last-child` never matches the last generated row; margins
collapse between siblings but not through a `use` wrapper's first child.

### Wave 5: removal

Delete the kit controls, list, command and menu code the graph no longer
needs; shrink `kit.h`; update DESIGN decision 5, `studio-ui-authoring.md` and
`editor-ui-references.md`.

## Postponed

| Item | Why |
|---|---|
| Graph node bodies, pins, inline literals and their source field (`kit_autocomplete`), comment frames, reroutes, live chips | Node drags every frame and 10 Hz chips over 5-10k bound views; the canvas is a separate context whose dp ratio is the zoom. They stay kit-emitted, styled by `graph.rcss`; a pin's choice and asset fields open Studio's picker (G1) |
| Zep's text surface, completion list, hover card | A native editor by nature, like Monaco in a browser |
| Chart, wire and grid geometry; color plane; drag ghost; Inspector and Designer highlights | Per-frame geometry with no document structure |
| Toasts | Springs and swipe need native motion; revisit after W3 |
| Tooltips | Already attribute-driven; the controller stays native, as a browser's `title` does |
| Reordering and reparenting objects by dragging Outliner rows | Scenes have no hierarchy, and object order has no Undo step, collaboration action or checkpoint record; it is a scene-authoring feature to add first. TreeRow's drop zones and the insertion line (F4) are ready for it |

## Risks

- `data-for` matches by index, so focus, transitions and presence follow the
  index when a list reorders.
- Popovers rely on `clip: none` and stacking; S0 found both work while no
  area forms a stacking context (`z-index`, `transform`, `filter`). Since F5
  each area is a document, which is one; F3 raises the document holding an
  open popover above the other areas, so a `transform` or `filter` inside
  an area still clips or buries a popover under it.
- The web build costs 1.3-1.7× more per view (S0); the guest Studio doubles UI cost.
- Hot-reloading a panel mid-gesture drops the gesture.
- Each entity's `self` data is its own variable since the Behavior-driven
  panels spike; within one panel, every Set UI Data that changes a value
  re-evaluates all the panel's `self` views.
- Tests churn as kit paths become selectors.

## Open questions

1. Can graph node bodies stay kit-emitted for good, keeping the kit as the
   graph's renderer?
2. Can Zep's surface and popups stay native, with only the Text Editor's
   chrome authored?
3. Answered in F1: panel actions are their own namespace
   (`settings.record`), routed to the panel source; actions the palette lists
   stay commands (`studio.<id>`).
4. Parity: exact pixels, or screenshot diffs within tolerance plus review?
5. Answered in F1: the structural conditional (`data-when`) and `input`/`change`
   on text inputs are patches; `contextmenu` needed none (the backend
   dispatches it). O(1) JSON arrays live in our bridge, and one document per
   area needs no patch.
6. Where do shared widgets live so games can import them (today
   `projects/studio`)?
7. May one editor type show in two areas at once, or do the fixed layout
   modes rule that out and simplify per-view state?
