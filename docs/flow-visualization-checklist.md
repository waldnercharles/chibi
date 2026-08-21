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
- [ ] Manually verify the visual treatment at 30, 60, and 144 FPS in the flow demo.
- [ ] Manually verify branch-end and stopped-flow fades in the running window.
