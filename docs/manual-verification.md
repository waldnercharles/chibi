# Manual editor verification

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

## Resize-pane recording evidence

The canonical evidence path is deterministic and uses the native app's input
seam. It records only the app canvas; no desktop capture, browser footage, or
ffmpeg conversion is involved:

```sh
make record-resize-pane RECORD_PATH=/tmp/c-blueprints-resize-pane.mp4 FRAMES=48
```

The command injects a press-drag-release at the left dock splitter, draws the
recording cursor and click ring, and fails unless the exposed left pane width
increases by at least half the requested 240-pixel drag. The output is an MP4
created directly by Cute Framework. Inspect decoded frames and report the
assertion line (`resize-pane assertion passed: ...`) with the artifact.
