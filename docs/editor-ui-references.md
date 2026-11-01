# Editor UI references

Studio source and shell assets are owned by the extracted `chibi-studio`
repository. Paths under `src/editor/` and `projects/studio/` in this guide are
relative to that repository. The engine supplies the shared SDK interfaces.

The editor UI is RmlUi driven from C through the Studio kit (`src/editor/ui/kit.h`,
`src/editor/rmlui/kit*.c`). None of the libraries below can be linked into it: they
are React/DOM code. Each one solves a problem the editor has too, so the kit carries
a native version that follows the library's contract: its states, timings,
thresholds and keyboard model. Before building a UI mechanism inside a panel, find
the problem here and use, or extend, the kit primitive. Node editors (xyflow,
imgui-node-editor, Blender, Unreal, ComfyUI and others) have their own map in
[Node editor references](editor-graph-references.md).

Contracts were read from each project's README, docs and source on 2026-09-23. Where
the docs and source disagree, the source value is recorded. All are MIT licensed
except cva (Apache-2.0); no source is copied. This supersedes the "skip" verdicts in
the earlier library catalog from the Codex RmlUi branch.

## Porting React and Tailwind references

Use the library's primary docs and source as the contract for component anatomy,
size and style variants, state attributes, motion, and keyboard behavior. Recreate
that contract as a reusable native kit primitive with RML and RCSS; React components
cannot run in this C/RmlUi runtime. Tailwind's [CLI](https://tailwindcss.com/docs/installation/tailwind-cli)
can scan templates and emit static CSS, and its [Preflight can be omitted](https://tailwindcss.com/docs/preflight#disabling-preflight)
by importing only theme and utilities. This helps inspect utility output, but the
generated stylesheet is not RCSS: a Tailwind v4.1.13 fixture produced `@layer`,
`@property`, `@supports`, CSS nesting, logical padding, custom-property `calc()`, and
`:root, :host`; RmlUi logged parse/property errors, and the fixture stayed at its
default color even though adding the stylesheet returned success. The RCSS expander
handles the documented subset in `src/support/rcss.h`, not general CSS. Keep authored
RML/RCSS as the runtime assets; do not treat stylesheet-load success as proof that a
ported style applied.

For each port, record the source's sizes, variants, state and keyboard contract,
then implement those states in the reusable kit primitive and its authored markup.
Check the RCSS through the native RmlUi parser and verify the actual rendered states,
keyboard interactions, and cropped screenshots in Studio's web build (`chibi-studio`); compare against the
reference for spacing, alignment, and motion as well as appearance.

## Map

| Library | Problem | Kit equivalent | State |
| --- | --- | --- | --- |
| [Base UI](https://base-ui.com) | Behavior contracts for fields, menus, popovers, dialogs, tooltips | Controls in `kit_controls.c` (toggle groups: `kit_toggle_group`; anchored searchable choices: `kit_select`'s dropdown, with `kit_picker` as the canvas literals' trigger for Studio's picker) and the native `<number-field>` and `<color-field>` (`src/ui/controls`, which `kit_number` and `kit_color` emit); popups and menus on `src/ui/popover.c`, the HTML popover attribute with one stack for authored popovers and the kit's (`kit_popup.c` emits the kit's: color swatches `kit_menu_item_desc.swatch`, a panel header's menu `kit_panel_menu`), and tooltips (a picture above the text: `kit_tip_image`) in `kit_popup.c`. Authored UI, the editor's and games', uses the widgets in `projects/studio/content/ui`: Switch, Select, Slider, Segmented, SidebarNav, SettingRow, Dialog, KeyHint; Menu, MenuItem, MenuCommand, MenuGroup, MenuSeparator, Submenu, MenuCheck and MenuOption over popover.c; ChoiceList over the native `<choice-list>`, with EnumField and AssetField; and over the native controls NumberField, VectorField, ColorField, Checkbox, PropertyRow, Panel and Splitter | Done |
| [shadcn/ui](https://ui.shadcn.com) | Semantic tokens, variant discipline, control anatomy | Tokens in the Studio theme (`projects/studio/content/ui/theme/tokens.rcss`, imported by the `StudioShell` root document); Textarea as `kit_text_area` | Done |
| [cmdk](https://github.com/dip/cmdk), [VS Code Quick Open](https://code.visualstudio.com/docs/getstarted/userinterface#_quick-open) | Command palette: ranking, groups, keyboard; prefixes and recent items | `Palette.rml` and `Picker.rml` over `<choice-list virtualized>` and the `palette` source (`studio_palette_view.c`; clients `studio_palette.c`, `studio_pick.c`), ranked by `support/text_rank.h`, which the kit's dropdowns share | Done; pages and group jumps open |
| [DialKit](https://github.com/joshpuckett/dialkit) | Live tuning of values, scrub feel, spring and easing controls | The native `<number-field>` (`src/ui/controls/number_field.c`; scrub, fine/coarse/snap, Escape cancels a scrub), which `kit_number` emits and authored UI uses as NumberField | Partial; rubber-band and spring controls open |
| [Motion](https://github.com/motiondivision/motion) | Springs, enter/exit presence, layout (FLIP) animation | `kit_presence`, `kit_layout` (`kit_motion.c`); springs in `src/ui/motion.c`. Authored UI, the editor's and games': `presence` on a `data-if` element | Done |
| [NumberFlow](https://github.com/barvian/number-flow) | Number changes as spinning digit wheels | The native `<number-flow>` element (`src/ui/text_motion.c`), which `kit_number_flow` emits | Done |
| [Torph](https://github.com/lochie/torph) | Text continuity when a label changes | The native `<morph-text>` element (`src/ui/text_motion.c`), which `kit_morph` and `kit_button_desc.morph` emit | Done; place-value numbers open |
| [Sonner](https://github.com/emilkowalski/sonner) | Toast notifications | `kit_toast` (`kit_toast.c`) | Done; Alt+T focus open |
| [Virtuoso](https://github.com/petyosi/react-virtuoso) | Lists with thousands of rows, follow-output logs, grids | `ui_list_window` (`src/ui/list.c`) on any scroller, fixed row height, a window that keeps its length; authored UI's `List` widget, and `kit_list`, a wrapper over it; a grid (VirtuosoGrid) is a list of rows of tiles, as Assets' | Done; measured heights open |
| [dnd-kit](https://github.com/clauderic/dnd-kit) | Accessible drag and drop | One drag for every document (`src/ui/drag.c`): `data-drag` sources, `data-drop` targets of one or several types with tree or list zones, `data-drop-accept`/`-over`/`-zone` marks, a bubbling `drop` event, `ui_drag_over` (dnd-kit's `over`), `ui_drag_hint` and `ui_drag_dropped` for C surfaces. `kit_drag_source`, `kit_drop_target`, `kit_drop_hover` and `kit_drag_hint` wrap it; the Hierarchy's keyboard sensor over the sortable tree's slots (`ui_design_slots`) | Done; a keyboard sensor for other lists and auto-scroll open |
| [Shiki](https://github.com/shikijs/shiki) | Syntax highlighting as styled token runs | `highlight_runs` / `highlight_markup` (`editor/ui/highlight.c`), `kit_markup` | Done: log, code assets, expressions |
| [Liveline](https://github.com/benjitaylor/liveline) | Streaming line chart that feels alive | The native `<line-chart>` (`src/ui/controls/line_chart.c`) over series C publishes by name (`line_chart_publish`) | Done |
| [Recharts](https://github.com/recharts/recharts) | Axes, ticks, tooltips for static charts | `<line-chart>` ticks, badge and scrub value; Performance's legend (`Performance.rml`) | Partial; bars open |
| [zustand](https://github.com/pmndrs/zustand) | One store, selective updates, persistence | `studio_prefs`, whose preferences are rows of `studio_settings[]` | Done |
| [Blender Workspaces](https://docs.blender.org/manual/en/latest/interface/window_system/workspaces.html), [Godot main screens](https://docs.godotengine.org/en/stable/getting_started/introduction/first_look_at_the_editor.html) | Named arrangements of areas for a task, switched without touching documents | `studio_prefs.layouts` per `studio_workspace`; explicit Horizontal Split and Vertical Split commands in the top bar | Done; two fixed split modes |
| [next-themes](https://github.com/pacocoursey/next-themes) | Theme preference: system, no flash, no mixed transitions | Theme in `studio_prefs` | Done |
| [clsx](https://github.com/lukeed/clsx) | Conditional class lists | `KIT_CX` | Done |
| [cva](https://github.com/joe-bell/cva) | Variants mapped to classes in one table | `kit_variant`, `kit_size` | Done |
| [Chrome DevTools](https://developer.chrome.com/docs/devtools/css), [Unity UI Toolkit Debugger](https://docs.unity3d.com/Manual/UIE-ui-debugger.html) | Pick any element; see its box, the rules that style it and where they were written | `kit_inspect_begin`, `kit_highlight`, `kit_element_path` (`kit_inspect.c`); the authored Inspector panel (`studio_inspect.c`) | Done; overridden declarations and tree keys open |
| [Unity IMGUIContainer](https://docs.unity3d.com/ScriptReference/UIElements.IMGUIContainer.html) | Native, immediate-mode content inside an authored layout | `kit_slot_begin`, `kit_slot_end` (`kit_slot.c`): each area's content in its place in the Workspace; each area header's editor controls | Done |
| [Unity UI Builder](https://docs.unity3d.com/Manual/UIB-getting-started.html), [UMG Designer](https://dev.epicgames.com/documentation/en-us/unreal-engine/umg-ui-designer-quick-start-guide-in-unreal-engine) | Visual editing of a UI document: hierarchy, canvas, details and a library of elements | The designer (`studio_designer*.c`) over `ui_design.h`'s text edits; its Hierarchy, Details and Library are authored (D1), the tree a List of TreeRows whose `data-drop-zones` give before, inside and after, the Library's tiles `data-drag="ui"` sources; `kit_highlight` for the Canvas outlines, `kit_item_pressed` for its resize handles | Done |
| [Zep](https://github.com/Rezonality/zep) (linked, C++) | Code editing: Vim and standard keys, undo, diagnostics, completion, hover | `kit_code` (`kit_code.c`) over `editor/text/zep_bridge.h`; the Text Editor's language glue in `editor/documents/ui_code.h` and `asset_code.h`; shared code intelligence in `assets/asset_source.h` | Done; highlighting, diagnostics, completion items and hover text come from the caller |

## Contracts

### Base UI

State lives in `data-*` attributes (`data-open`, `data-pressed`, `data-highlighted`,
`data-disabled`, `data-starting-style`, `data-ending-style`). Menus open on press,
switch on hover within a menubar and open submenus after 100 ms. Tooltips show
after 500 ms and stay warm for 400 ms within a group. A tooltip holds a label, a
shortcut and an optional description that wraps; in a zoomed canvas it keeps
its size on screen. An element marked `data-tip-clipped` shows its own text as
a tooltip only while that text is cut off, unless an ancestor has its own tip. Number fields repeat held
steps after 400 ms, then every 60 ms. Popups close on Escape or an outside press,
the top one first: `src/ui/popover.c` keeps one stack per context for authored
popovers (`<div popover>`, opened by `popovertarget`), the kit's popups and the
color picker, and an open popover's document stacks above the other areas.
A popover that is not a menu or a list (an authored `[popover]`, a `popover`
class on `kit_popup_begin`) uses Base UI's nonmodal Popover focus model:
opening focuses its first control, Tab and Shift+Tab move through its controls,
and leaving its boundaries closes it and continues at the trigger's position in
document order. Escape returns focus to the trigger. Menus (`role="menu"`) keep
arrow-key navigation, typeahead within 0.5 s and close on Tab; a `menuitem` or
`option` click closes the menu chain, and checkbox and radio items stay open
(Base UI's `closeOnClick`). A disabled item never highlights, under the
pointer, by arrow keys or by typeahead, and its click reaches nothing. A popover marked `data-template` (Menu, Submenu) is mounted only while open, as Base UI unmounts a Popup that is not `keepMounted`: it builds its content on opening and removes it after its exit transition. A modal host (`kit_modal_host`) traps the
focus as Base UI's Dialog does: Tab and Shift+Tab cycle through its tabbable
elements and never reach the editor behind it.

Editable references use `kit_autocomplete`: one inline text input with an
anchored suggestion list. The caller supplies type-compatible suggestions and
validates typed literals before committing. Up/Down select suggestions, Enter or Tab
accepts, and Escape restores the committed value. Text editing keeps focus in
the input; the popup does not add another search field or an adjacent trigger.
Rows share the text editor's compact completion styling: a symbol icon, a
monospace name with matched characters highlighted, and a type aligned right.
Tab accepts an active suggestion; otherwise Tab and Shift+Tab move focus normally.
This follows the [editable combobox keyboard model](https://www.w3.org/WAI/ARIA/apg/patterns/combobox/).
Discrete choices use `kit_choice_items` when their rows need shared groups,
icons, or descriptions, and `kit_choice` for simple named options.

### cmdk

- Ranking is `text_rank` (`src/support/text_rank.h`) over `text_search_score`
  (`src/support/text_search.h`), the search the
  graph's Add menu uses and, in its whole-word form, native `search_nodes` and
  `search_assets` inspection. Every query word must match: a word start in the label ranks
  above a match inside a word, then initials, then letters in order, and the
  keywords score half. An item's `boost` adds to its score; Studio gives recently
  and often chosen items up to 300, as Raycast's frecency does.
- Items that do not match are hidden. Items sort within groups, and groups sort
  by their best item. Matched words show in bold, or else their letters in order.
- Selection resets to the first item when the query changes.
- Keys:
  - Up and Down move; Home and End jump.
  - Meta+Up and Meta+Down go to the first and last item; Alt+Up and Alt+Down go to the previous and next group.
  - Ctrl+N/J/P/K also move.
  - Enter selects. Backspace on an empty query, or Escape, pops a page.
- Hovering selects an item. Beyond about 2,000 items, filter and virtualize externally: Studio ranks in C and windows the rows (`<choice-list virtualized>`, `ui_list` with each row's top).
- An item's description shows as a muted second line; tooltips stay hidden while the palette is open.

### DialKit

- Movement under `FEEL_DRAG_DP` (4 dp) is a click.
- Sliders:
  - A click jumps to the value with a spring (stiffness 300, damping 25).
  - Dragging past the ends rubber-bands: a 32 px dead zone, then up to 8 px of stretch following `8·sqrt(min(overflow/200, 1))`.
  - The handle springs back with visual duration 0.35 and bounce 0.15.
- Keys: arrows move one step, Shift or Page keys ten, Home and End go to the limits.
- Spring control:
  - Simple mode: duration 0.1–1 s (default 0.3) and bounce 0–1 (default 0.2).
  - Advanced mode: stiffness, damping and mass, defaulting to 200/25/1.
- The config's shape picks the widget:
  - A number becomes a slider.
  - A boolean becomes a toggle.
  - A color string becomes a picker.
  - A nested object becomes a folder.

### Motion

- Springs are exact damped oscillators. `visual duration` and `bounce` map to physics as `stiffness = (2π / (1.2·duration))²` and `damping = 2·clamp(1 − bounce, 0.05, 1)·√stiffness`.
- Defaults by property:
  - Positions use stiffness 500 and damping 25.
  - Scale uses 550 and 30.
  - Opacity and color tween for 0.3 s with ease `(0.25, 0.1, 0.35, 1)`.
- Every animation is interruptible: it continues from its current value and velocity.
- Presence keeps an exiting child until its exit finishes. Layout animation measures before and after and animates transforms, never width or height.
- Reduced motion turns off transform and layout animation but keeps opacity.
- Kit:
  - `kit_presence` uses the RCSS transitions declared for `data-starting-style` and `data-ending-style`.
  - `kit_layout` uses a 0.35 s critically damped spring.
- Authored UI: `presence` on a `data-if` element does what `kit_presence` does, as the web's `@starting-style` and exit animations do. The element shows with `data-starting-style` and no transition for one frame. When `data-if` hides it, it keeps its display with `data-ending-style` and no pointer events until its transitions end. Showing it while it leaves turns it back. A binding's first evaluation, Reduce Motion and elements without `presence` show and hide at once (`src/ui/motion.c`, over `third_party/rmlui/data-if-handler.patch`).
  - Studio motion tokens: press 110 ms, popovers 140 ms in and 100 ms out, drawers 200 ms in and 100 ms out.

### NumberFlow

- Each digit is a 0–9 wheel. The transform and spin take 900 ms with a spring curve; opacity takes 450 ms ease-out.
- Trend defaults to the sign of the change, so all digits spin the same way and wrap: 9 → 0 spins forward.
- Digits that disappear spin to 0 while fading, and new digits fade in. Shifted characters slide by FLIP.
- Edges are masked at 0.25 em vertically.
- It does not animate:
  - under reduced motion
  - when the value is hidden
  - on continuously changing values; clocks and live readouts stay static tabular text.
- Authored UI: `<number-flow data-attr-value="expr" precision="1" grouping="" prefix="$" suffix="%"/>`. Its `aria-label` is the text it shows, as NumberFlow's is; a value that is not a number shows nothing. It lays itself out, so a theme sets only its face and line height.

### Torph

- Text is segmented into characters, or into words when it contains spaces. Each segment is keyed by its text plus its occurrence, and old and new strings are matched by LCS.
- Numbers match by place value.
- Timing is 400 ms, expo-out `(0.19, 1, 0.22, 1)`:
  - Kept segments FLIP to their new positions.
  - Leaving segments scale to 0.95 and fade over the first 25% of the duration.
  - Entering segments start at 0.95 and fade in over 50%, after a 25% delay.
- The container's size transitions from the old size.
- Use it for labels that change in place: button labels, status, the transport target. Never use it on text being edited.
- Authored UI: `<morph-text>{{ expr }}</morph-text>` morphs its text content, with white space collapsed as HTML shows it. It lays out the new text hidden for one frame before it moves, so no layout pass runs twice. It is an inline-block that sizes to its text, and its `aria-label` is its text.

### Sonner

- Toaster:
  - 3 visible toasts, 4 s lifetime, 356 px wide, 14 px gap.
  - Placed bottom-right with a 24 px offset; removal unmounts after 200 ms.
- Collapsed stack:
  - Toasts behind the front one move `gap × index` and scale `1 − 0.05 × index`.
  - They take the front toast's height and their content is hidden.
- Hover expands the stack to real heights. Timers pause while it is expanded or the window is hidden.
- Motion:
  - Transform, opacity and height take 400 ms.
  - A toast enters from `translateY(100%)`.
  - Swipe dismisses at 45 px or 0.11 px/ms.
- Alt+T focuses the stack; Escape collapses it.
- Types are success, error, info, warning and loading. Reusing an id updates the toast in place. A toast may carry an action, such as Undo.

### Virtuoso

- Items are measured once rendered and kept in an offset tree; `fixedItemHeight` skips measurement. Our rows use a fixed height, `--row` for authored Lists.
- The window keeps its length as the list scrolls (`ui_list_window`), so a `data-for` over it changes values in rows it keeps instead of making rows: a scroll step costs about a third of making the window anew (ui_bench case 9).
- Overscan is in pixels.
- Output logs use `followOutput`: they stay pinned only when already within 4 px of the bottom.
- Prepending keeps the scroll anchored.
- Grouped lists keep their headers sticky.

### dnd-kit

- Pointer activation:
  - A drag handle starts immediately.
  - Otherwise a drag starts after `FEEL_DRAG_DP` (4 dp) of movement, dnd-kit's distance constraint. Holding still never starts one: dnd-kit's delay constraint is for touch, and with a mouse it turned a slow click on a tile into a drag that swallowed the click.
- Keyboard: Space or Enter picks up, arrows move, Space, Enter or Tab drops, Escape cancels.
- Collision uses pointer intersection first, then shape intersection. Ours is the pointer's: the nearest element under it, in any document of the context, that takes the type.
- Sources and targets are attributes (`src/ui/drag.h`), so authored and kit lists, and C surfaces through `ui_drag_dropped`, share one session, as Unity's `DragAndDrop` crosses its windows and Unreal's `FDragDropOperation` its Slate panels. A tree row's zones are Unreal's `EItemDropZone`: its top and bottom quarters drop above and below it, its middle onto it (`data-drop-zones="tree"`); a row that holds nothing splits in halves (`"list"`).
- Motion:
  - Drops animate for 250 ms `ease`.
  - Sortable neighbors shift over 250 ms with `(0.25, 1, 0.5, 1)`.
- Auto-scroll starts within the outer 20% of a container.
- The dragged item moves by transform only.
- Pickup, over, drop and cancel are announced.

### Shiki

- Output is lines of token runs, each with a color and font-style bits: italic 1, bold 2, underline 4, strikethrough 8.
- Grammar state carries from line to line, so a stream highlights incrementally.
- Themes define token colors.
- The kit version keeps this model: a lexer per language emits runs, and RCSS classes carry the theme.

### Liveline

- Smoothing:
  - Values lerp independently of frame rate, with `factor = 1 − (1 − speed)^(dt / 16.67 ms)`, dt clamped to 50 ms and speed 0.08.
  - Speed rises by up to 0.2 when the gap is small.
  - The Y range lerps at 0.15.
- Curves are monotone splines (Fritsch–Carlson) that never overshoot.
- Range and window:
  - 12% margin; the minimum span is 10% of the range.
  - The window is 30 s.
- Scrubbing shows a crosshair and value badge. A pulsing live dot marks the latest sample.
- No DOM nodes per point. Paused charts freeze smoothly and catch up on resume.

### Recharts

- Margins are 5 px.
- Axes: 5 "nice" ticks, end labels preserved, 5 px minimum tick gap, tick size 6.
- The tooltip follows the cursor with a 10 px offset; the chart is focusable, and Left/Right move the active point.
- Enter animation runs 1.5 s, and is skipped under reduced motion.

### zustand

- One store holds state and its actions.
- Updates:
  - `set` merges shallowly.
  - Listeners receive `(state, previous)`.
  - Readers select a slice and update only when it changes.
- The persist middleware saves the whole store under one key, with a version for migration.
- Kit version:
  - `studio_prefs` is the one store for view settings and layout.
  - Each preference is a row of `studio_settings[]` (`studio_settings.c`): its JSON path, the saved key panels bind, its category, label, description, keywords, type and range. The preferences file, the saved keys and the Settings page all read and write through it; the area layout and command shortcut overrides are kept by hand.
  - Studio applies changes by comparison with the last applied copy.
  - It saves 0.5 s after the last change: a JSON file on desktop, `localStorage["chibi.studio"]` in the browser.
  - Missing fields keep their defaults.
  - Keyboard uses the command registry for defaults, area contexts, aliases and repeat policy. Overrides persist by command ID. Record captures one chord before editor input, Escape cancels, and Reset restores the default and its aliases. Overlapping assignments are shown on both rows; an override takes priority over a default binding.
- Authoring state never goes in this store; it belongs to the C model.

### next-themes

- The stored choice (system, dark or light) is separate from the resolved theme.
- The theme is applied before the first frame, so there is no flash.
- The system preference follows OS changes.
- While the palette changes, transitions are suspended for one frame so elements do not change color out of step: `body.theme-switching` sets `transition: none`.

### clsx and cva

- `KIT_CX(k, "area", { "fill", fill })` joins classes whose condition holds, in order, without de-duplication.
- Variants are enums mapped to classes in one table in `kit_controls.c`. An icon without a label derives the square button, a compound variant. `cls` is only for layout hooks.
- State never goes in classes: it goes in `data-*` attributes.

### Chrome DevTools and the UI Toolkit Debugger

- Pick mode (⇧⌘C, Inspect Element, or the Inspector's Pick) highlights the element under the pointer; a click selects it and Escape ends it. Nothing else takes the pointer while picking.
- The highlight fills the margin, border and padding bands and the content box in DevTools' colors: margin `#f6b26b`, border `#ffe599`, padding `#93c47d` and content `#6fa8dc`, about two thirds opaque. A label names the element and its size.
- Picks look through a surface into the document it shows: the graph canvas's, and the game UI in the Scene, and the UI Canvas through its page zoom's crop. Text and `pointer-events: none` content, such as a HUD's, are picked by their boxes.
- The tree reveals a pick. An element's only text shows on its row, and text nodes show as their text. Hovering a row highlights its element.
- Styles lists the declarations that win, inline first and then by rule, most specific first, with each rule's file and line (`ui_style_origin`). Inherited declarations follow under the ancestor that gives them. RmlUi keeps only winning declarations, so nothing is struck through as overridden.
- Force state holds `:hover`, `:active`, `:focus` and `:checked` on the selection until it is cleared.

### Unity IMGUIContainer (native slots)

- An authored layout names an element; native code draws into it each frame, and what the element holds otherwise stays.
- The kit marks the element it adopts, `data-kit-slot="<key>"`, and finds it again before input and emission. Authored content is rebuilt behind the kit's back (a mount reloading its asset, data-for recreating an item), and RmlUi's pools give a new element the old one's address, so the mark, not the address, tells them apart. A slot whose element is gone drops its nodes without touching their elements and builds anew in the new one.
- A slot not begun in a frame loses its content, as any kit item does.
- Popups, menus and tooltips a slot opens live in the kit's layer, as elsewhere.
- Focus follows IMGUIContainer's: a click on authored chrome leaves the keyboard to the editor (`kit_release_focus`), while a field in a slot keeps it, as an IMGUI text field does.

### Embedded document lifecycle

The shared engine interfaces are [`ui/rml.h`](../src/ui/rml.h) and
[`ui/iframe.h`](../src/ui/iframe.h); Studio's kit creates and owns its documents.
Preserve these contracts when changing document creation or settling:

- Creating an empty document must not update unrelated data models; their dirty
  bindings still update in the ordinary Context update. Preserve stylesheets,
  body classes, listeners, identity and visibility, and set its source URL before
  attachment and plugin notification so relative resources resolve correctly.
- Fitted documents grow and shrink in the same frame, including when a later
  document dirties an earlier holder. Refresh skips only `OnUpdate`, preserving
  animation, style, scroll, layout and other callbacks; the ordinary Context
  update still invokes `OnUpdate` once. Retain full updates when boxes or heights
  change, and preserve pre/Context/post settling and its four-pass bound.
- Resolve stable attachment identities and observers after callbacks that can
  detach or replace documents. This does not establish arbitrary callback
  safety for every holder traversal.

Regression coverage must preserve three distinct cases:

| Case | Required assertions |
| --- | --- |
| Bound document replacement | Repeated whole-document replacement retains the surviving model and updates conditionals/lists; retired observers expire, live-document counts return to baseline, exact label reads stay one or two, topology reads do not accumulate, and unmount/destruction leave no bound reads. Require zero new RmlUi warnings. |
| Layout refresh | Synthetic animation time advances opacity through completion and resolves geometry without extra parent/child `OnUpdate` calls; the normal Context update invokes each callback exactly once. |
| Removal during settling | A callback removes both visited and unvisited attachments without skipping a survivor; callback counts, survivor geometry, later resize and teardown remain correct. |

These are ownership and behavior assertions, not wall-clock thresholds,
performance loops, sleeps or rendering work. Setup and teardown must run even
after an assertion fails, and failures must reach the focused and aggregate exit
status. In `chibi-studio`, `tests/held/engine_oracles/test/editor/`
retains `kit_empty_documents.c`, `iframe_bindings.c` and `kit_documents.c`, but
that package has no executable target. Those retained sources are not current
runnable coverage of all three contracts; historical measurements do not close
that coverage gap or establish current integration acceptance.

### Unity UI Builder and UMG Designer

- Text stays the source of truth: every designer change is a `text_edit` of the document through `ui_source`, one undo step shared with the Text Editor. A scrub or a picker session joins one step.
- Hierarchy rows follow dnd-kit's sortable tree: the top and bottom quarters of a row drop before and after it, its middle inside it; an element that holds no elements takes halves. The dragged row's target shows as a line or a filled row.
- The keyboard sensor, over the Hierarchy: Space or Enter picks the selected row up, Up and Down step through every gap between rows at each depth (`ui_design_slots`, folded rows' children left out), Space, Enter or Tab drops and Escape cancels. Pickup, each place and the drop are announced in the status bar; a change to the text puts the row down.
- The Canvas outlines the element under the pointer, and the selection with its name and size in dp, as UMG labels its selected widget; a click there selects. A drag over it splits the element under the pointer as a row splits, along its parent's flow (a flex row's, or inline boxes', runs across): its edges drop before and after it, marked by a line between siblings, and its middle inside it, outlined.
- Selection is shared: the Hierarchy, the Canvas and the Text Editor's cursor select the same element, found through `data-src` lines.
- Details shows the rules that match the selection's tag, id and classes, pseudo classes included; their values edit in place, and declarations are added and removed there, written as the rule is. Extract to Class moves the inline style into a new rule, written as the RCSS's rules are, and gives the element the class: one undo step over both parts.

### Zep (kit_code)

Zep is the one library here that is linked: `zep_bridge.cpp` only forwards, and
`kit_code.c` routes input and draws.

- The editor object keeps the text, cursor and undo history; `kit_code` shows it for a frame. Edits from elsewhere arrive as `text_edit`s through `kit_code_apply`, one undo step.
- A caller whose document keeps the history sets `desc.history`: the undo keys (the primary modifier and Z or Y, Vim's `u` and Control+R) report `KIT_CODE_UNDO` and `KIT_CODE_REDO` and change nothing, and the editor keeps no history of its own. The Text Editor steps the UI document's buffer then, as Studio's Undo does, so there is one history (`ui_code_record`, `ui_code_replay`, `ui_code_follow`).
- Keys:
  - Vim reads the physical Control key; the standard keys read the primary modifier (Command on a Mac), with Option and Command arrows moving by words and to line ends there.
  - Save, Search, Settings (`Mod+,`), UI Scale and the drawers go back to the host as `kit_code_result.chord`, as do primary chords the standard keys do not edit with, and every Command chord in Vim on a Mac except Z (undo), V (paste), and C and X, which copy and cut the selection.
  - F12 reports `KIT_CODE_DEFINITION`, Shift+F12 `KIT_CODE_REFERENCES` and Control+Space `KIT_CODE_SUGGEST`, as in VS Code; Vim takes Control+[ for Escape.
  - Tab types; keys never reach RmlUi's focus navigation.
  - `:w`, `:wq` and `:x` report `KIT_CODE_SAVED`; `:<line>` moves there; commands that would open files or windows do nothing. The command and status lines take no room until an ex command starts.
- A press places the cursor and dragging selects; a double click selects the word, names with `-` whole; a press with the primary modifier reports `KIT_CODE_DEFINITION` there. The wheel scrolls as far as it scrolls a list, `FEEL_SCROLL_POINTS` a notch, sideways too.
- With `desc.no_wrap`, lines run on past the edge: the view scrolls sideways to keep the cursor in view, and with the wheel's sideways turn.
- Completions show under the cursor while the caller supplies them: Up and Down choose, Enter or Tab accept, Escape dismisses until the list changes. The list scrolls to its choice.
- Diagnostics mark the gutter and underline the range the caller gives (`kit_code_marker.start`, `end`), else their line.
- Hover: once the pointer rests on a glyph for the tooltips' delay, `kit_code_hover` names its byte, and the caller describes it in `desc.hover` (a title line, then detail lines). A card in the overlay layer shows that under the pointer, after the diagnostics of its line, so it never clips at the editor's edge. A key or a press hides it until the pointer moves.
- After RmlUi releases its font resources (`rml_font_generation`), as destroying a backend does, the editor fetches its face again.
- Text is JetBrains Mono through RmlUi's font engine, a mesh per run of one color, cached until a render no longer draws it. Colors come from `theme/code.rcss`.
- Highlighting spans carry classes, as Shiki's tokens do: a span draws in the color the theme gives its `tok-*` classes, so a refining class such as `tok-tag` can override its base (`tok-keyword`). The Text Editor takes its spans from `ui_source`.
- The browser hands pasted text only to a `paste` event, which a Command+V the canvas lets through raises; the web build keeps it for the paste a frame later.

## Common mismatches to catch

- **Toasts built by hand, or a modal used for a notice.** Use `kit_toast`. The status-bar report is for the last operation; toasts are for results the user might act on, such as Undo after Delete, or must not miss, such as a failed save.
- **A plain container with hand-written open, close, focus or outside-press logic.** Use kit popups, menus and the command (Base UI): they handle focus return, Escape and outside dismissal, typeahead and stacking.
- **A number animated by rewriting its text every frame.** Use `kit_number_flow` for discrete changes. Continuous values stay static.
- **1,000 or more rows emitted directly.** Use the `List` widget or `kit_list` over `ui_list_window` (Virtuoso) before capping or paging results.
- **A tangle of parameters or duplicated statics for shared state.** Authoring state stays in the C model. View settings and layout go in `studio_prefs` (zustand). Transient widget state lives in the kit node.
- **Class strings built with ternaries or `snprintf`.** Use `KIT_CX` (clsx) for conditions and `kit_variant`/`kit_size` (cva) for variants. State goes in `data-*` attributes.
- **Instant show and hide, or animation interpolated by hand in C.** Use `kit_presence` with RCSS starting and ending styles, and `kit_layout` for moves (Motion).
- **A label swapped instantly where continuity matters.** Use `kit_morph` (Torph).
- **A drag state machine written inside a panel.** Use `data-drag` and `data-drop` (`src/ui/drag.h`) or the kit's wrappers (dnd-kit) for their activation constraint, marks, zones and Escape.
- **A theme, density or motion toggle that flashes or animates.** Apply preferences before the first frame and suspend transitions for the switch (next-themes).
- **A chart built from many elements.** Use the chart element (Liveline, Recharts).
- **Colors matched with regular expressions at the call site.** Use the shared token-run highlighter (Shiki).
- **Source text edited in a text area.** Use `kit_code` (Zep) for anything longer than a note: it keeps undo, Vim and the standard keys, and takes edits as `text_edit`s.
