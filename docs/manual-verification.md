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

## App recording

Use `build/app --record /tmp/c-blueprints-recording.mp4 --frames 180` for a
deterministic 30 FPS recording of the app's Scene canvas. The recorder captures
the rendered app canvas, not the desktop, so unrelated windows are never
included. `--record` may be used without a path to use
`/tmp/c-blueprints-recording.mp4`; recording keeps the Scene canvas at its
initial even dimensions. A failed encoder or save is reported and returns a
non-zero status. Drive the real native app for acceptance evidence; do not
describe browser or desktop footage as app evidence.
