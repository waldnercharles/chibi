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
native UI harness. It drives the real ImGui application through queued native
mouse input and asserts observable document, pane, and graph state; it is not an LLM
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

## Resize-pane recording evidence

The canonical evidence path is deterministic and uses the native app's input
seam. It records only the app canvas; no desktop capture, browser footage, or
ffmpeg conversion is involved:

```sh
make record-resize-pane RECORD_PATH=/tmp/c-blueprints-resize-pane.mp4 FRAMES=48
```

The command injects a press-drag-release at the left dock splitter, draws the
recording cursor and click ring, and fails unless the exposed left pane width
changes by exactly the requested 240-pixel drag (within one pixel for framebuffer
quantization). The output is an MP4 created directly by Cute Framework. Inspect
decoded frames locally and report the assertion line (`resize-pane assertion
passed: ...`); this repository does not claim that visual evidence is CI-attested.
