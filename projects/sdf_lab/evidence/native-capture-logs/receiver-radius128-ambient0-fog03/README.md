# Receiver-light enabled capture set

These six preview captures were made after correcting the receiver settings. All outputs used 320×180, `render_frames=256`, `freeze_simulation=false`, and the Vulkan backend; each raw capture is 55,296,000 bytes (240 frames × 320 × 180 × 4). Capture buffers were processed sequentially.

The current 2D `HRCLabCornell2D` scene uses ambient 0, fog 0.3, HRC intensity 7.5, surface radius 128, relief 1.5, bevel 1, and material `edge_light=1` on all six solid receivers; the only nonzero material emission is the moving emitter at 0.08. The no-bounce control differs only by `feedback=false` and `bounce_intensity=0`. The pre-Depth capture used commit `8cb3ba2c72217ce58b4fd8381e99e864d185527a` with only unsupported `surface_mode`, `surface_depth_gain`, and `surface_thickness` fields removed; all supported receiver fields match. Its 240-frame raw sequence is byte-identical to the current flat capture.

The three current SDF Cornell modes use ambient 0, fog 0.3, HRC intensity 3, exposure 0.2, emitter intensity 6, zero SDF material emission, `edge_light=1` on every solid, radius 128, relief 1.5 and bevel 1. Feedback is off. Depth alone uses gain 1, thickness 12, source radius 260, low quality and bounded segments. The resulting mode panels are documented in the project README.

The `.log`, request, run and compact response files preserve native render metadata. Each response omits only its embedded preview PNG payload; stills and videos are separate renderer captures. Raw RGBA sequences remain in `/tmp/hrc-cornell-receiver-radius128-20261005/` for this workspace session.
