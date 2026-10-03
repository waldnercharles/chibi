# Studio in RML and RCSS

Studio's whole editor moves to authored RML and RCSS, as the user decided on
2026-10-03. Anything that can't be authored, or that would cost substantial
performance, is postponed and listed in Postponed below. Status: S0, F0 and
F1 done; the rest planned. The spike (S0) ran on a loaded machine, so its
times are provisional until re-measured on an idle one (S0 results).

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
| Top bar | `TopBar.rml`; menubar drawn natively from its `<menu>` markup; play target, layout and debugger steps in native slots | `commands`, `editor.tabs`, menu lists | W3 C3: `<menubar>`, MenuCommand, popovers |
| Status bar | `StatusBar.rml`; UI Scale native | `editor`, `commands` | W3 C3 |
| Area header | `AreaHeader.rml`; editor type menu and editor controls in native slots | `editor.areas`, editor types | W3 C3; each panel's controls move with the panel |
| Settings, Shortcuts, Collaboration, NewProject, Dialog, Inspector panel, Data, Theme, GraphView popover | Authored | | Done |
| Properties: Object, Prototype, Scene, Behaviors | `studio_properties.c` (~2k lines) | Reflection, ECS reads, overrides and mixed values, `write_field` gestures, ranges and units from `Components.kdl`, live values at 20 Hz in Play, locks, search, Keep, Add Component | W2 P1 |
| Properties: Node, Member | `studio_graph_properties.c`, `studio_member_properties.c` | Literal editors, binding autocomplete, expression code, type picker, reorder grips | W4 G1 |
| Outliner | `studio_outliner.c` (F0) | Selection (⌘/⇧), 10k-row virtualization, drops, rename, eye and lock, menu | W2 P2 |
| Scene and Canvas headers | `studio_scene.c`, `studio_canvas.c` | Commands, tool, snap, grid, zoom, Show toggles, Canvas size, actions log | W3 C1 |
| Scene viewport | `kit_surface` showing `kit:viewport` | Texture, pointer input, camera labels | Overlays authored; input stays in C |
| Assets | `studio_assets.c` | Index, filters, `kit:thumb:` textures, thumbnail budget, columns, keys, drag, New menu | W2 P3 |
| Output, Find, Calls | `studio_output.c`, `studio_find.c`, `studio_calls.c` | Log ring, highlighted markup, problem places, follow-output, call tree | W2 P4 |
| Members | `studio_members.c` | Members, uses, rename, drag into the graph, menus | W2 P5 |
| Behavior area | `studio_graph*.c`, a second context drawn to `kit:graph` | Nodes, pins, literals, comments, reroutes, wires, live chips | Header, breadcrumbs, find, menus, add-node search: W4 G1. Node bodies: postponed |
| Text Editor | `studio_code.c`, `kit_code.c` (Zep) | Buffer, intelligence, Vim | Chrome authored (W3 C2); the Zep surface stays a native element |
| Performance | `studio_performance.c` | Sample ring | W3 C2 around a native `<line-chart>` |
| Shell | `studio_shell.c` | Area tree, ratios, drawer and side springs, maximize, editor types | W3 C3 with a native `<splitter>` |
| Palette, dropdowns, autocomplete, asset pickers | `kit_command.c`, `studio_palette.c`, `studio_pick.c`, `kit_popup.c` | Ranking, recents, prefixes, thumbnails | W3 C4; ranking stays in C |
| Popups, menus, tooltips | `kit_popup.c` | Placement, stacking, dismissal, typeahead | Content authored; the popover controller is native; tooltips stay attribute-driven |
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
`commands`, `collaboration` and `new_project` (NewProject). The shell mounts
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
choices: { "GameBody.kind": [{ value, label, icon, group }] }
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
| `<color-field>` | `kit_color` (done, F2) | `value mixed readonly disabled`; opens its own picker as `<input type=color>` opens the OS one: `input` while open, `change` on close. Its popover is inside the field (`position: fixed; clip: none`) until F3's `popover.c` |
| `<splitter>` | `kit_splitter` (done, F2) | `axis min max value invert`; `input`/`change`; cursor |
| `<live-text>` | `kit_live_label` (done, F2) | `value`; glyphs update without layout |
| `<line-chart>` | `kit_chart.c` | Series from a C registry, named like `kit:` textures |
| Existing | | `<img src="kit:...">`, `<number-flow>`, `<morph-text>`, `presence` |

The kit's number, color, splitter, drag and popup calls become thin wrappers
over these, so native and authored panels share one implementation, one popup
stack and one drag session while the migration runs.

### Lists, drag, focus, popovers

- **Virtualization:** an authored `List` widget (scroller, spacer, window) and
  `ui_list_window(...)`, a port of `kit_list.c` that works on any element.
  The panel source reads `scrollTop` at sync and fills about 40-60 rows, in
  the same frame. Row height comes from `--row`. Windows keep their length so
  `data-for` reuses elements.
- **Drag:** `src/ui/drag.c`, an element-level `kit_drag.c`. Sources use
  `data-drag`, targets `data-drop` (with `data-drop-zones="tree"` giving
  before/inside/after); a `drop` event carries type, payload and zone. C asks
  `ui_drag_dropped(...)` for drops on surfaces such as the viewport and graph.
- **Keys:** area keys stay in C (`studio_shortcuts`, editors' `*_key`); widget
  keys are the element's; modal content uses `nav`/`tabindex` and behaviors.
  Rename in place uses `<input autofocus data-value-lazy>`.
- **Popovers:** `src/ui/popover.c` gives the HTML popover attribute's
  behavior: content stays in its document, shows above all with
  `position: fixed; clip: none`, anchors to its target or a point, closes on
  an outside press or Escape and returns focus. `role="menu"` adds highlight,
  typeahead and submenu delay; `<menubar>` switches menus on hover. Widgets:
  Menu, MenuItem, MenuGroup, MenuSeparator, Submenu, MenuCommand and
  ChoiceList (palette, pickers, Add Component, editor types, debug target).
  Closed popovers mount nothing.

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
| Outliner (10k objects, 60-row window) | Measured: 362 per scroll step | Scroll, selection | Low (0.69 ms native, 0.91 ms web per step) |
| Assets (8×8 tiles) | ~400 | Scroll, thumbnails ready | Low |
| Output following a log | Measured: 40 markup parses per batch | Logging | Medium (1.3 ms native, 1.9 ms web; 6× plain text) |
| Members (<300 rows) | ~1.5k | Edits | Low |
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
| F3 Popovers | `popover.c`, `<menubar>`, one popup stack; widgets Menu*, MenuCommand, ChoiceList, AssetField, EnumField; a `choices` source | The GraphView popover and one authored menu at parity |
| F4 Lists and drag | `drag.c`, `ui_list_window`, kit wrappers; widgets List, TreeRow (drop zones), Tile | One drag crosses an authored list and a native one |

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
| P2 Outliner | Rows, root, eye and lock, rename, drops, menu, filter | ~350 lines |
| P3 Assets | Grid and list, header, New menu, name popover, drag sources, preview tooltip | ~400 lines |
| P4 Output | Six views, follow-output, highlighted rows, Calls tree | ~450 lines |
| P5 Members | Sections, rename, uses, menus, drag into the graph | ~600 lines |

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

## Risks

- `data-for` matches by index, so focus, transitions and presence follow the
  index when a list reorders.
- Popovers rely on `clip: none` and stacking; S0 found both work while no
  area forms a stacking context (`z-index`, `transform`, `filter`). Since F5
  each area is a document, which is one: a popover in it is above the other
  areas only while its document is in front, as it is once it takes focus.
  Fallback: a teleport into the layer that rebinds the data model.
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
