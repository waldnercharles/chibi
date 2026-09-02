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

The required acceptance path for editor usability is the unattended deterministic
native UI harness. It drives the real ImGui application through a semantic input
seam and asserts observable document, pane, and graph state; it is not an LLM
computer-use walkthrough or a screenshot-only check. Run it with:

```sh
make ui-integration
```

The command writes `build/ui-integration-artifacts/interaction-trace.txt` and
`screenshot.png`. Unit/API tests remain complementary coverage. Manual
computer-use may be useful for exploration, but is not UI evidence.

## Minimum walkthrough

1. Open or create a Prototype and add a reflected component.
2. Edit a component field in the Inspector and verify the visible value.
3. Open a Behavior Asset, create a Graph node, edit its authored literal, and save.
4. Run the active runtime target, inspect the entity and behavior state, then stop.
5. Reload the authored data and verify the edits persist.

The first gameplay slice adds movement, one weapon/projectile, and one enemy to
this walkthrough. Capture `/tmp/c-blueprints-screenshot.png` with
`build/app --headless --screenshot`; report its dimensions with the result.
