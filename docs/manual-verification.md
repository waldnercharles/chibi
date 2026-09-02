# Manual editor verification

## Application flags

The native app accepts these command-line flags:

- `--help` prints the available flags and exits.
- `--headless` runs without showing a window.
- `--frames <count>` exits after rendering the requested number of frames.
- `--fps <rate>` sets the target frame rate.
- `--screenshot [path]` runs headlessly, renders three frames, saves a PNG, and exits. The path defaults to `/tmp/c-blueprints-screenshot.png`; use `--frames` to choose a different capture frame.

For example:

```sh
build/app --screenshot /tmp/c-blueprints-screenshot.png
```

Use computer-use against the real native editor. This is the acceptance path for
editor usability and must not be replaced by headless tests. Record the command,
what was authored, the runtime action, and the screenshot dimensions.

## Minimum walkthrough

1. Open or create a Prototype and add a reflected component.
2. Edit a component field in the Inspector and verify the visible value.
3. Open a Behavior Asset, create a Graph node, edit its authored literal, and save.
4. Run the active runtime target, inspect the entity and behavior state, then stop.
5. Reload the authored data and verify the edits persist.

The first gameplay slice adds movement, one weapon/projectile, and one enemy to
this walkthrough. Capture `/tmp/c-blueprints-screenshot.png` with
`build/app --headless --screenshot`; report its dimensions with the result.
