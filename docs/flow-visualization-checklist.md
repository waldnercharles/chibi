# Event and data flow visualization

Implementation checklist for the Unreal Engine 5-style graph flow treatment.
The requirements are defined by `docs/spec.md` (`DBG-15`, `DBG-16`, and
`UI-44` through `UI-50`) and the Phase 8 notes in `docs/roadmap.md`.

- [x] Record traversed exec and data edges without coupling the renderer to evaluation.
- [x] Preserve traversal timestamp and activation order.
- [x] Bound traversal history and discard the oldest records first.
- [x] Animate a continuous bubble stream on each edge using the latest traversal record.
- [x] Match Unreal's 64-unit spacing and 192-unit-per-second pacing in canvas units.
- [x] Use an Unreal-style attack, sustain, and release envelope for edge heat and bubbles.
- [x] Refresh the active stream on branch switches instead of stacking per-step particles.
- [x] Shrink bubble size and opacity through the release envelope when execution stops.
- [x] Cover traversal ordering and the history bound with headless checks.
- [x] Headlessly sample the time envelope at 30, 60, and 144 FPS and compare
  equal elapsed-time states (`UI-48`).
- [x] Confirm the renderer consumes traversal records and has no evaluator
  callback in its render path (`UI-49`).
- [x] Launch the bounded flow-demo window with
  `./build/app --flow-demo --frames 600 --fps 60`; it dispatches `Begin` and
  exits cleanly after the requested frames.
- [x] Repeat the bounded launch smoke at 30 and 144 FPS; both runs dispatch
  `Begin` and exit cleanly after the requested frames.
- [ ] Manually verify the visual treatment at 30, 60, and 144 FPS in the flow demo.
- [ ] Manually verify branch-end and stopped-flow fades in the running window.

The remaining two rows require a window session. The executable procedure is
`docs/editor-phase3-manual.md`; the headless equivalent is covered by the
`flow_visual_*` checks in `test/self_check.c`.
