# Studio in RML and RCSS

Studio's whole editor moves to authored RML and RCSS, as the user decided on
2026-10-03. Anything that can't be authored, or that would cost substantial
performance, is postponed and listed in Postponed below. Status: S0, F0,
F1, F2, F3, F4, F5, P2, P4 and P5 done; the rest planned. The spike (S0) ran on a
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
- `Inspector.rml` renders up to 1,000 rows through `data-for`.
- `TopBar`, `StatusBar` and `AreaHeader` bind `editor` and `commands`.
- `Properties.rml` is a shell (tabs and a scroll area) around native content.

About 1,600 kit call sites remain across 40 files. The largest:
`studio_properties.c` (171), `studio_scene.c` (162), `studio_shell.c` (98),
`studio_code.c` (90), `studio_member_properties.c` (88),
`studio_designer_details.c` (85), `studio_graph_draw.c` (76),
`studio_graph.c` (71), `studio_graph_literals.c` (65), `studio_assets.c` (62).

## Inventory

| Surface | Lives now | Needs from C | Plan |
|---|---|---|---|
| Top bar | `TopBar.rml`; menubar drawn natively from its `<menu>` markup; play target, layout and debugger steps in native slots | `commands`, `editor.tabs`, menu lists | W3 C3 over F3's `<menubar>`, Menu and MenuCommand |
| Status bar | `StatusBar.rml`; UI Scale native | `editor`, `commands` | W3 C3 |
| Area header | `AreaHeader.rml`; editor type menu authored (F3, a Menu over `choices.editors`); editor controls in a native slot | `editor.areas`, `choices` | W3 C3; each panel's controls move with the panel |
| Settings, Shortcuts, Collaboration, NewProject, Dialog, Inspector panel, Data, Theme, the Behavior header's View menu (GraphView, trigger included since F3) | Authored | | Done |
| Properties: Object, Prototype, Scene, Behaviors | `studio_properties.c` (~2k lines) | Reflection, ECS reads, overrides and mixed values, `write_field` gestures, ranges and units from `Components.kdl`, live values at 20 Hz in Play, locks, search, Keep, Add Component | W2 P1 |
| Properties: Node, Member | `studio_graph_properties.c`, `studio_member_properties.c` | Literal editors, binding autocomplete, expression code, type picker, reorder grips | W4 G1 |
| Outliner | Authored: `Outliner.rml`, `OutlinerHeader.rml`; the `outliner` source (`studio_outliner.c`) | Selection (⌘/⇧), 10k-row virtualization, drops, rename, eye and lock, menu | Done (W2 P2) |
| Scene and Canvas headers | `studio_scene.c`, `studio_canvas.c` | Commands, tool, snap, grid, zoom, Show toggles, Canvas size, actions log | W3 C1 |
| Scene viewport | `kit_surface` showing `kit:viewport` | Texture, pointer input, camera labels | Overlays authored; input stays in C |
| Assets | `studio_assets.c` | Index, filters, `kit:thumb:` textures, thumbnail budget, columns, keys, drag, New menu | W2 P3 |
| Output, Find, Calls | Authored (P4): `Output.rml`, `OutputHeader.rml` over `studio_output.c`'s `output` source | | Done |
| Members | `Members.rml` and `MembersHeader.rml`; the `members` source in `studio_members.c` | Members, uses, rename, drag into the graph, menus | Done (P5) |
| Behavior area | `studio_graph*.c`, a second context drawn to `kit:graph` | Nodes, pins, literals, comments, reroutes, wires, live chips | Header, breadcrumbs, find, menus, add-node search: W4 G1. Node bodies: postponed |
| Text Editor | `studio_code.c`, `kit_code.c` (Zep) | Buffer, intelligence, Vim | Chrome authored (W3 C2); the Zep surface stays a native element |
| Performance | `studio_performance.c` | Sample ring | W3 C2 around a native `<line-chart>` |
| Shell | `studio_shell.c` | Area tree, ratios, drawer and side springs, maximize, editor types | W3 C3 with a native `<splitter>` |
| Palette, dropdowns, autocomplete, asset pickers | `kit_command.c`, `studio_palette.c`, `studio_pick.c`, `kit_popup.c` | Ranking, recents, prefixes, thumbnails | W3 C4; ranking stays in C |
| Popups, menus, tooltips | `src/ui/popover.c` (F3) stacks, places and dismisses authored popovers and the kit's popups and runs menus; `kit_popup.c` emits the kit's | Placement, stacking, dismissal, typeahead | Done for the controller (F3); kit menus move to Menu with their panels; tooltips stay attribute-driven |
| Toasts | `kit_toast.c` | Springs, swipe | Postponed |
| Designer: Hierarchy, Details, Library | `studio_designer*.c` | `ui_design` edits, drop zones, CSS value types | W4 D1; Canvas guides native |
| UI Inspector | Authored panel; picking native | | W4 I1: virtualize its tree |

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
holds one array per control kind with exactly one item in use, the Settings
pattern:

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
outliner: { title, count, collapsed, total, row,
  views: [{ first, query, rows: [{ id, label, icon, behaviors, selected, active, marked, hidden, locked, renaming }] }] }
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
| `<choice-list>` | `kit_dropdown`'s list (F3; the kit's still runs its own) | `[role=option]` descendants with `value`, `data-label` or `.label`, `data-keywords`; its first `<input>` filters them by `text_search_score` (`data-filtered`, `data-empty`); Up, Down, Home, End and the pointer move `data-highlighted`; Enter or a click sends `change` with the option's value |
| `<splitter>` | `kit_splitter` (done, F2) | `axis min max value invert`; `input`/`change`; cursor |
| `<live-text>` | `kit_live_label` (done, F2) | `value`; glyphs update without layout |
| `<line-chart>` | `kit_chart.c` | Series from a C registry, named like `kit:` textures |
| Existing | | `<img src="kit:...">`, `<number-flow>`, `<morph-text>`, `presence` |

The kit's number, color, splitter, drag and popup calls become thin wrappers
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
  `display: none`, so nothing in it lays out or draws; its data views stay
  bound.

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
| Properties, Object tab (the Player: 4 panels, 14 rows, 309 elements) | Measured: 340 per selection, 166 per refresh | Selection rebuilds rows; edits dirty once; Play refreshes at 20 Hz | Low for views (0.19 ms native, 0.32 ms web per refresh); each text change lays out its document |
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
| P1 Properties | Object, Prototype, Scene, Behaviors tabs; header with Live and Keep; search; field menu (Node and Member stay in a slot) | ~1,200 lines of kit UI; `write_field`, reset, Keep and multi-object logic stay |
| P2 Outliner (done) | Rows, root, eye and lock, rename, drops, menu, filter | ~350 lines |
| P3 Assets | Grid and list, header, New menu, name popover, drag sources, preview tooltip | ~400 lines |
| P4 Output (done) | Six views, follow-output, highlighted rows, Calls tree | ~450 lines |
| P5 Members (done) | Sections, rename, uses, menus, drag into the graph | ~550 lines of kit UI: P5 results |

#### P5: Members (done)

`Members.rml` is a List whose rows the `members` source
(`studio_members.c`) windows with `ui_list_window`, and `MembersHeader.rml`
is the header's filter (`members.filter`), mounted in the AreaHeader's
controls as GraphView is. What changes as the user selects and names
lives apart, in `members_ui` (the selected member's, the open graph's and
the named member's row keys, and the selected member's menu): rows bind
it by key (`row.key == members_ui.selected`), so a selection re-evaluates
those views and not every row's. The rows are TreeRow's anatomy written
out, since TreeRow reads its selection from its item. Rows keep the kit's
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
  Shift+D work after a click on a row; a field keeps its keys.
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
of rewriting them. Checks:
`--studio-members` (also in `--studio-documents`): sections, swatch and
use counts, selection, folding, a use count's press, F2 and double-click
rename with Enter, Escape and one Undo step, the add buttons, the context
menu's Duplicate, X with its Undo, a drag onto the graph and its hint,
reveal and the filter, which leaves preferences alone; with
`CHIBI_MEMBERS_CAPTURE_DIR` set it writes native shots of rows, rename,
menu and drag in the four variants. Open: two areas showing Members share
one window and one List.

#### P2: the Outliner (done)

The Outliner is `Outliner.rml` in each area that shows it (a List of
TreeRows, `studio_ui_area_view` aliasing the area's item of
`outliner.views` as `view`) and `OutlinerHeader.rml` in its AreaHeader's
controls (the filter and Add, a ChoiceList of the scene's prototypes). The
`outliner` source publishes the root, the count, the notice, the filter
(`query`, two-way, view state), the row menu's state and each view's
window (`root`, `rows`: id, label, icon, behaviors, drop, selected, active,
marked, hidden, locked, renaming); `outlinerHeader`, apart so a scroll or
a selection reads none of its views, holds what rarely changes (the Add
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
  Studio UI frame (sync + emit + update + capture), medians (data views
  counted after the header split, in a later run):

  | Case | Before (kit) | After (authored) | After ÷ before | Rows instanced | Views | Layouts |
  |---|---:|---:|---:|---|---:|---:|
  | Scroll step of 3 rows, 22 rows shown | 6.30 / 8.29 / 7.93 | 7.47 / 6.57 / 6.87 | 0.93× | 3 → 0 | 518 | 2 |
  | Select another object in view | 7.61 / 9.54 / 9.46 | 9.08 / 7.93 / 8.70 | 0.97× | | 927 | 6 |
  | A text change in the Outliner (case 8) | 2.27 / 2.13 / 1.77 | 1.54 / 2.16 / 2.13 | 0.94× | | | 1 |

  A scroll step or a selection reads the window's row views (about 23 a
  row) and the root's; a selection also reads the chrome's (`editor`,
  `commands`). The header's Add list (160 views) and the eye's tip moved
  to `outlinerHeader`, from 696 views a step. Row flags (selected, active,
  hidden) stay in the rows: RmlUi addresses take only literal indices, so
  a parallel array of flags can't be read by the `data-for` index, and
  TreeRow reads its state from its item. `data-for` matches by index, so
  a step rewrites each row's values as the window slides (as the gallery's
  List does), and instances none.

  The Outliner's document holds 929 elements against 558: closed popovers
  (the row menu, the Add list's options) and `data-when` templates add
  elements that never lay out. Sync rose 0.3 to 0.5 ms (the filter walks
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

### Wave 3: scene, chrome, palette (4 parallel)

| | Scope | Deletes |
|---|---|---|
| C1 Scene | Scene and Canvas headers, actions log, camera labels, notice line | ~500 lines |
| C2 Editor chrome | Behavior header (breadcrumbs, debug target), graph find bar; Text Editor header and find; Performance with `<line-chart>` | ~400 lines |
| C3 Shell | `Workspace.rml` (areas, `<splitter>`, drawer and side transitions, maximize); editor-type popover; `<menubar>`; play target, layout, UI Scale, debugger steps | ~600 lines and `kit_menubar_trigger` |
| C4 Palette | `Palette.rml` over ChoiceList (prefixes, pages, recents, thumbnails); asset pickers, Add Component, refactor popover; ranking moves to `support/` | ~500 lines of `kit_command` UI |

### Wave 4: graph chrome, Designer, Inspector (3 parallel)

| | Scope |
|---|---|
| G1 Graph chrome | Node, wire, pin and comment menus; add-node search; Node and Member tabs as PropertyRow kinds (binding chip, source autocomplete, expression field, type picker, reorder grips) |
| D1 Designer | Hierarchy (TreeRow, drop zones), Details (lengths with units, colors, keywords, class chips, add popovers), Library (Tile) |
| I1 Inspector | Its tree on `List`, removing the 1,000-row cap |

### Wave 5: removal

Delete the kit controls, list, command and menu code the graph no longer
needs; shrink `kit.h`; update DESIGN decision 5, `studio-ui-authoring.md` and
`editor-ui-references.md`.

## Postponed

| Item | Why |
|---|---|
| Graph node bodies, pins, inline literals, comment frames, reroutes, live chips | Node drags every frame and 10 Hz chips over 5-10k bound views; the canvas is a separate context whose dp ratio is the zoom. They stay kit-emitted, styled by `graph.rcss` |
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
- The shared `entities`/`self` variable dirties every panel, so editor data
  never goes through `self`.
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
