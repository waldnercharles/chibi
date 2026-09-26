# Editor UI references

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

## Map

| Library | Problem | Kit equivalent | State |
| --- | --- | --- | --- |
| [Base UI](https://base-ui.com) | Behavior contracts for fields, menus, popovers, dialogs, tooltips | Controls in `kit_controls.c` (toggle groups: `kit_toggle_group`) and `kit_number.c`; popups, menus (color swatches: `kit_menu_item_desc.swatch`) and tooltips in `kit_popup.c` | Done |
| [shadcn/ui](https://ui.shadcn.com) | Semantic tokens, variant discipline, control anatomy | Tokens in the Studio theme (`projects/studio/content/ui/Theme.rcss`); Textarea as `kit_text_area` | Done |
| [cmdk](https://github.com/dip/cmdk) | Command palette: ranking, groups, keyboard | `kit_command` (`kit_command.c`) | Done; pages and group jumps open |
| [DialKit](https://github.com/joshpuckett/dialkit) | Live tuning of values, scrub feel, spring and easing controls | `kit_number` (`kit_number.c`; scrub, fine/coarse/snap) | Partial; rubber-band and spring controls open |
| [Motion](https://github.com/motiondivision/motion) | Springs, enter/exit presence, layout (FLIP) animation | `kit_presence`, `kit_layout`, springs in `kit_motion.c` | Done |
| [NumberFlow](https://github.com/barvian/number-flow) | Number changes as spinning digit wheels | `kit_number_flow` (`kit_text.c`) | Done |
| [Torph](https://github.com/lochie/torph) | Text continuity when a label changes | `kit_morph`, `kit_button_desc.morph` | Done; place-value numbers open |
| [Sonner](https://github.com/emilkowalski/sonner) | Toast notifications | `kit_toast` (`kit_toast.c`) | Done; Alt+T focus open |
| [Virtuoso](https://github.com/petyosi/react-virtuoso) | Lists with thousands of rows, follow-output logs | `kit_list` (`kit_list.c`), fixed row height | Done; measured heights open |
| [dnd-kit](https://github.com/clauderic/dnd-kit) | Accessible drag and drop | `kit_drag_source`, `kit_drop_target` (`kit_drag.c`) | Done; keyboard sensor and auto-scroll open |
| [Shiki](https://github.com/shikijs/shiki) | Syntax highlighting as styled token runs | `highlight_runs` / `highlight_markup` (`editor/ui/highlight.c`), `kit_markup` | Done: log, JSON, expressions |
| [Liveline](https://github.com/benjitaylor/liveline) | Streaming line chart that feels alive | `kit_chart` (`kit_chart.c`, element `kit-chart`) | Done |
| [Recharts](https://github.com/recharts/recharts) | Axes, ticks, tooltips for static charts | `kit_chart` ticks, scrub value, Performance legend | Partial; bars open |
| [zustand](https://github.com/pmndrs/zustand) | One store, selective updates, persistence | `studio_prefs` | Done |
| [next-themes](https://github.com/pacocoursey/next-themes) | Theme preference: system, no flash, no mixed transitions | Theme in `studio_prefs` | Done |
| [clsx](https://github.com/lukeed/clsx) | Conditional class lists | `KIT_CX` | Done |
| [cva](https://github.com/joe-bell/cva) | Variants mapped to classes in one table | `kit_variant`, `kit_size` | Done |

## Contracts

### Base UI

State lives in `data-*` attributes (`data-open`, `data-pressed`, `data-highlighted`,
`data-disabled`, `data-starting-style`, `data-ending-style`). Menus open on press,
switch on hover within a menubar and open submenus after 100 ms. Tooltips show
after 500 ms and stay warm for 400 ms within a group. Number fields repeat held
steps after 400 ms, then every 60 ms. Popups close on Escape or an outside press
and return focus to their trigger. `docs/prototypes/studio/kit.html` lists every
control's contract.

### cmdk

- Ranking follows `command-score`:
  - A continuous match scores 1, a match after a space 0.9 and after another separator 0.8.
  - A mid-word jump scores 0.17 and a transposition 0.1.
  - Each skipped character multiplies the score by 0.999 and a case mismatch by 0.9999.
- Zero scores are hidden. Items sort within groups, and groups sort by their best item.
- Selection resets to the first item when the query changes.
- Keys:
  - Up and Down move; Home and End jump.
  - Meta+Up and Meta+Down go to the first and last item; Alt+Up and Alt+Down go to the previous and next group.
  - Ctrl+N/J/P/K also move.
  - Enter selects. Backspace on an empty query, or Escape, pops a page.
- Hovering selects an item. Beyond about 2,000 items, filter and virtualize externally.

### DialKit

- Movement under 3 px is a click.
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

### Torph

- Text is segmented into characters, or into words when it contains spaces. Each segment is keyed by its text plus its occurrence, and old and new strings are matched by LCS.
- Numbers match by place value.
- Timing is 400 ms, expo-out `(0.19, 1, 0.22, 1)`:
  - Kept segments FLIP to their new positions.
  - Leaving segments scale to 0.95 and fade over the first 25% of the duration.
  - Entering segments start at 0.95 and fade in over 50%, after a 25% delay.
- The container's size transitions from the old size.
- Use it for labels that change in place: button labels, status, the transport target. Never use it on text being edited.

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

- Items are measured once rendered and kept in an offset tree; `fixedItemHeight` skips measurement. Kit rows use a fixed height.
- Overscan is in pixels.
- Output logs use `followOutput`: they stay pinned only when already within 4 px of the bottom.
- Prepending keeps the scroll anchored.
- Grouped lists keep their headers sticky.

### dnd-kit

- Pointer activation:
  - A drag handle starts immediately.
  - Otherwise a drag starts after 5 px of movement, or after a 200 ms hold within 10 px.
  - Touch starts after a 250 ms hold within 5 px.
- Keyboard: Space or Enter picks up, arrows move, Space, Enter or Tab drops, Escape cancels.
- Collision uses pointer intersection first, then shape intersection.
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
  - Studio applies changes by comparison with the last applied copy.
  - It saves 0.5 s after the last change: a JSON file on desktop, `localStorage["chibi.studio"]` in the browser.
  - Missing fields keep their defaults.
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

## Common mismatches to catch

- **Toasts built by hand, or a modal used for a notice.** Use `kit_toast`. The status-bar report is for the last operation; toasts are for results the user might act on, such as Undo after Delete, or must not miss, such as a failed save.
- **A plain container with hand-written open, close, focus or outside-press logic.** Use kit popups, menus and the command (Base UI): they handle focus return, Escape and outside dismissal, typeahead and stacking.
- **A number animated by rewriting its text every frame.** Use `kit_number_flow` for discrete changes. Continuous values stay static.
- **1,000 or more rows emitted directly.** Use `kit_list` (Virtuoso) before capping or paging results.
- **A tangle of parameters or duplicated statics for shared state.** Authoring state stays in the C model. View settings and layout go in `studio_prefs` (zustand). Transient widget state lives in the kit node.
- **Class strings built with ternaries or `snprintf`.** Use `KIT_CX` (clsx) for conditions and `kit_variant`/`kit_size` (cva) for variants. State goes in `data-*` attributes.
- **Instant show and hide, or animation interpolated by hand in C.** Use `kit_presence` with RCSS starting and ending styles, and `kit_layout` for moves (Motion).
- **A label swapped instantly where continuity matters.** Use `kit_morph` (Torph).
- **A drag state machine written inside a panel.** Use kit drag and drop (dnd-kit) for its activation constraints, keyboard support and announcements.
- **A theme, density or motion toggle that flashes or animates.** Apply preferences before the first frame and suspend transitions for the switch (next-themes).
- **A chart built from many elements.** Use the chart element (Liveline, Recharts).
- **Colors matched with regular expressions at the call site.** Use the shared token-run highlighter (Shiki).
