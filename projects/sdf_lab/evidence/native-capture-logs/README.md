# Native capture audit

Five successful `preview_scene` captures were made with the Vulkan backend: the pre-Depth flat fixture, the current flat Blur fixture, and the current Blur, Gather, and Depth thickness scenes. Each request used `render_frames=256`, `freeze_simulation=false`, and 320×180 output. Each response records 240 render samples; each raw RGBA sequence is 55,296,000 bytes (240×320×180×4). The bounded captures were not scaled to larger targets.

The historical capture used source commit `8cb3ba2c72217ce58b4fd8381e99e864d185527a` and a temporary copy of the current flat fixture with only unsupported mode and Depth fields removed. Its complete raw sequence matches the current flat Blur sequence byte for byte. The three current mode requests differ only by `surface_mode`.

The `.log` and request files are preserved here. Response JSON retains renderer and performance metadata with the inline preview PNG payload removed; the corresponding renderer still is `../hrc-cornell-modes-depth.png`. Full responses and raw sequences remain in `/tmp/hrc-cornell-comparisons-20261005-run2/` and `/tmp/hrc-cornell-3d-modes-ambient006-20261005/` for this workspace session.
