# Bounded UI replay

Native `chibi-author` and `chibi-player` accept `--ui-replay REQUEST.json` and
`--ui-replay-result RESULT.json`. Both flags require an explicit matching
`--frames` from 1 through 60, `--fixed-dt`, and `--size` at most 640x360. The
existing final `--capture PNG` takes one readback. Browser interactive validation
uses ordinary browser input separately; CLI replay is refused in the web host.
Run affected selectors only through the production 60-second supervisor.

The request is strict UTF-8 JSON with version 1, `frames`, `events` and
`assertions`. It is limited to 256 KiB and 256 combined event/assertion records.
Duplicate properties, unknown fields, unknown input kinds, invalid values and
nonmonotonic frames are refused. Frames start at zero. Events run before the
frame; assertions observe the resulting UI after its draw/update. Allow frames
for authored event delivery before checking its outcome.

```json
{
  "version": 1,
  "frames": 12,
  "events": [
    {"frame": 3, "type": "click", "selector": "#name"},
    {"frame": 4, "type": "text", "text": "New name"}
  ],
  "assertions": [
    {"frame": 9, "type": "data", "entity": "Application", "name": "name", "equals": "New name"}
  ]
}
```

Pointer types are `click`, `pointer-move`, `pointer-down`, `pointer-up` and
`wheel`. They require a selector resolving exactly one actual RML element.
The host refuses hidden, disabled, empty, covered or clipped-away targets. It
resolves the target's current geometry and performs normal pointer input; it
never invokes an editor handler or writes application data. Optional `x`/`y`
are normalized positions in the element, defaulting to its center. Wheel
`delta` is required and bounded to -100 through 100. Positive values scroll
down and negative values scroll up, matching RmlUi. Pointer button is left.

Keyboard types are `key-down` and `key-up`, each requiring `key`. Supported keys
are A-Z, 0-9, Space, Tab, Enter, Escape, Backspace, Delete, Insert, Home, End,
PageUp, PageDown, arrows, F1-F12 and ordinary punctuation. Optional `modifiers`
is an array of unique Ctrl, Shift, Alt, Meta and Mod names; Mod resolves to the
host's primary modifier. `text` delivers up to 16 KiB of nonempty UTF-8 text to
the normally focused control. Text does not change focus or bypass validation.
Replay supplies deterministic window focus; all control focus comes from input.

Assertions are either `visible` with `selector` and boolean `equals`, or `data`
with a dot-separated authored scene object path, a UI data name, and
bool/number/string `equals`. Explicit ECS names also resolve when they do not
conflict with a different authored scene object. Resolution checks the current
instance and live entity generation; a missing owner cannot match global data.
Equality preserves types. Selectors, names and entity paths are
limited to 512 bytes; expected strings to 4 KiB. A failed assertion or input
refusal ends the run with a nonzero exit and an incomplete result. No hidden
application state setter exists.

Results record dispatched input, resolved pointer coordinates, actual typed
assertion values and owner resolution, dimensions, renderer and behavior backends, fixed timestep,
requested/completed frames and installed SDK identity. SHA-256 identifies the
exact request bytes, exact prepared application artifact and final PNG bytes.
The artifact identity describes the compiled application; it does not claim a
fingerprint of every editable workspace file. SDK identity covers exact installed
`package.json` bytes and its package version/build ID; absent identity marks
`complete: false`. `passed` reports the bounded UI assertions independently.
`result_sha256` hashes the compact result JSON before that property is appended.
Output is limited to 2 MiB.

The ordinary parser dispatcher covers UTF-8 ownership, typed expectations,
duplicate/unknown/invalid intent refusal and combined byte/record budgets. It
creates no renderer. An actual application replay, screenshot and browser run
are separate acceptance categories.
