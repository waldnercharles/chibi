# Editor UI references

SDK5 packaging and bounded CPU validation now pass. Installed build
`cef077585ca28f1edfb80ccc00d6344026bec1bf347104974b1303d08d210a21`
has manifest SHA256
`4aab3609c818da3813741e160ab972068e9540e0206a3773af8253921c230baa`
and engine source snapshot
`5cb31f58592436059bd0ed49e44013551d2063eb045e6f17bf3128b0f0346ba9`.
All five required groups passed, along with owning workspace-authoring,
workspace-services, host-services, authored-surface, code-control, event-payload
and random-isolation selectors. Strict installed-SDK packaging also passed.
Whole-application SDK5 compilation and Home startup remain pending at this
update. Future SDK6 work is staged and unvalidated. These CPU/package results do
not establish rendered interaction or browser acceptance.


## Current ownership

Studio is an authored application in `chibi-studio`. Its RML defines structure,
RCSS defines appearance, and `.chibi` behaviors own editor state, commands and
workflows. Paths under `projects/studio/` below are relative to that repository.
Engine paths under `src/ui/` and `src/authoring/` provide reusable capabilities
through an installed SDK. The native Studio kit and Zep bridge were retired from
the migration candidate on 2026-10-14. Studio's `NATIVE_RETIREMENT.json` records
original paths and blobs at baseline `34f13325c3fb4e1888dbd64bf45d809e07c782d3`.

Use the shared authored controls before creating a panel-specific equivalent.
The [authoring guide](studio-ui-authoring.md) describes the current composition
and event model. [Graph references](editor-graph-references.md) cover graph
interaction design. [Retirement maps](native-retirement/graph.md) distinguish
source replacements from outstanding acceptance work.

## Current implementation map

| Concern | Authored owner in `projects/studio/content/` | Reusable engine capability |
| --- | --- | --- |
| Fields and choices | `ui/NumberField`, `VectorField`, `ColorField`, `Checkbox`, `EnumField`, `AssetField`, `ChoiceList`, `Select` | `src/ui/controls` numeric, color and choice mechanics |
| Menus and dialogs | `ui/Menu*`, `Dialog`, `StudioMenu`, `StudioSubmenu`; authored command handlers | `src/ui/popover.c` focus, dismissal and stacking |
| Layout and styling | `ui/Panel`, `Splitter`, `PropertyRow`, `StudioShell`, `StudioWorkspace`; `ui/theme/tokens.rcss` | RmlUi layout, measurement and data binding |
| Palette and resource pickers | `behaviors/StudioCommands.chibi`, `AssetBrowser.chibi`, shared choice controls | Bounded resource/catalog queries and ranking |
| Notices | `behaviors/StudioNotices.chibi`, `ui/StudioNotices.rml/.rcss` | Generic event delivery and UI rendering |
| Text and navigation | `behaviors/StudioText.chibi`, `StudioDocuments.chibi`, `ui/StudioTextEditor` | Generic code control, UTF8 editing, document and semantic services |
| Designer and DOM inspection | `behaviors/StudioUIDesigner.chibi`, `StudioUICanvas.chibi`, `StudioDOMInspector.chibi` | Rooted source candidates, static UI preview, layout and style inspection |
| Material, model and scene views | `StudioVisual.chibi`, `StudioModel.chibi`, `StudioScene.chibi` and corresponding RML | Visual authoring, managed texture views, detached runtime, geometry and evidence services |
| Preferences and workspace state | `behaviors/StudioPreferences.chibi`, `StudioShell.chibi` | Platform preference storage |
| Drag/drop, lists and motion | Shared authored controls and behavior policy | `src/ui/drag.c`, `list.c`, `motion.c`, `text_motion.c` |

This map records source ownership. A control's presence does not certify its
rendered appearance or interaction parity. The installed SDK4 whole-application
compile passed, but its bounded Home startup failed before UI because an
inactive project required `workspace_resources`. SDK5 corrects the service
contract; runtime validation remains pending at this update.

## Using external references

The contracts below were read from primary documentation and source on
2026-09-23. They preserve useful interaction targets and historical measurements.
They are not fresh checks of those projects or acceptance results for Studio.
React/DOM components cannot run directly in this RmlUi application. Implement
component anatomy and workflow in shared RML, RCSS and `.chibi`; extend an engine
control only for a reusable input or rendering capability.

Tailwind output is not RCSS. A historical v4.1.13 fixture emitted unsupported
`@layer`, `@property`, `@supports`, nesting and custom-property expressions.
Stylesheet-load success did not mean those styles applied. Use the supported
subset in `src/support/rcss.h`, inspect parser diagnostics and capture rendered
states with the installed SDK's bounded replay tooling.

The remaining contract notes retain former `kit_*`, Zep and `src/editor/` names
as historical reference identifiers. Recover those implementations from the
Studio baseline above. They describe prior behavior and design targets, not
current dependencies or instructions to restore native controllers. Any stated
"done" or numeric behavior below belongs to that recorded implementation until
fresh authored acceptance establishes it.

## Historical reference contracts

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
elements and never reach the editor behind it. `docs/prototypes/studio/kit.html` lists every
control's contract.

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
- Authored UI: `presence` on a `data-if` element does what `kit_presence` does, as the web's `@starting-style` and exit animations do. The element shows with `data-starting-style` and no transition for one frame. When `data-if` hides it, it keeps its display with `data-ending-style` and no pointer events until its transitions end. Showing it while it leaves turns it back. A binding's first evaluation, Reduce Motion and elements without `presence` show and hide at once (`src/ui/motion.c`, over `cmake/patches/rmlui-data-if-handler.patch`).
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

## Checks for new authored UI

- Reuse shared notices for operation results and recovery actions. Keep detailed
  diagnostics behind an explicit action rather than filling the main panel.
- Use authored menus and dialogs over generic popover focus and dismissal.
- Match controls to reflected types and ranges. Keep choices searchable or
  scrollable with at most seven visible rows.
- Keep canonical source and grouped undo in document services. Authored behaviors
  own temporary drafts, gesture policy and correlated completion.
- Put interaction state in `data-*` attributes and appearance in shared RCSS.
- Use list windowing for large inventories, generic chart elements for plots and
  managed texture views for rendered previews.
- Keep continuously changing numbers static. Respect reduced motion and verify
  focus, disabled, error and cancellation states in actual rendered replay.
- Report compilation, CPU service checks, startup, rendered interaction and
  browser acceptance separately. Historical kit screenshots do not validate an
  authored replacement.
