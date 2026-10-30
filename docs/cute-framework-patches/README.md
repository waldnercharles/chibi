# Cute Framework upgrade and patch audit

Audited on October 9, 2026. The dependency now targets [`96e6034aa6bc44665856ee480962a9ddab0b87cc`](https://github.com/RandyGaul/cute_framework/commit/96e6034aa6bc44665856ee480962a9ddab0b87cc), Cute's `master` on that day. The previous pin was [`7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`](https://github.com/RandyGaul/cute_framework/commit/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d). The update brings in Cute's optional WebGPU backend, which the browser player uses; other builds leave it off.

Of 17 patches, 11 are now upstream, most adapted from Chibi's own patches. Six remain, and `cute-web-webgpu-device`, `cute-gles-range-draw-state` and `cute-sound-free-queue-music-volume` are new.

## Removed patches

| Patch | Upstream replacement |
| --- | --- |
| `cute-installed-sdl` | [7e20a238](https://github.com/RandyGaul/cute_framework/commit/7e20a238) adds `CF_USE_SYSTEM_SDL`, which finds an installed SDL3 through `SDL3_DIR` or `CMAKE_PREFIX_PATH`, and [735891b5](https://github.com/RandyGaul/cute_framework/commit/735891b5) pins the SDL archive's hash. Chibi's unused `CHIBI_SDL3_PACKAGE_PREFIX` setting is gone. |
| `cute-custom-coverage` | [41fb1e8c](https://github.com/RandyGaul/cute_framework/commit/41fb1e8c) makes every custom shape safe in the tiled path: user fields bin by their bounds, never claim opaque cover, and antialias from the field's slope. |
| `cute-custom-shape-cache` | [eadb6967](https://github.com/RandyGaul/cute_framework/commit/eadb6967) returns the existing id for an identical source. |
| `cute-gles-buffer-streaming` | [05b57ada](https://github.com/RandyGaul/cute_framework/commit/05b57ada). |
| `cute-zero-size-window` | [1175b052](https://github.com/RandyGaul/cute_framework/commit/1175b052). |
| `cute-gles-scissor-cache` | [04c16787](https://github.com/RandyGaul/cute_framework/commit/04c16787). |
| `cute-gpu-submit` | [222cf1bf](https://github.com/RandyGaul/cute_framework/commit/222cf1bf), with the same `cf_gpu_submit` signature. |
| `cute-gles-float-readback` | [5652e4a2](https://github.com/RandyGaul/cute_framework/commit/5652e4a2) tries the target's own read pair first and falls back to the guaranteed pair for every 16F, 32F, R8 and RG8 target. |
| `cute-fragment-depth` | [353704cc](https://github.com/RandyGaul/cute_framework/commit/353704cc), plus WGSL output in [a5c53e4a](https://github.com/RandyGaul/cute_framework/commit/a5c53e4a). |
| `cute-depth-snapshot` | [d0a23703](https://github.com/RandyGaul/cute_framework/commit/d0a23703) adds `cf_canvas_copy_depth(dst, src)`. It copies whole canvases because D3D12 and WebGPU copy depth only as whole textures, so `src/render/mesh_material.c` now copies its whole scratch canvas. |
| `cute-sanitizer-cleanup` | [35bb68b4](https://github.com/RandyGaul/cute_framework/commit/35bb68b4). |

`cute-eight-targets` and `cute-web-no-asyncify` remain, narrowed to what upstream still lacks.

## Retained patches

The rows follow the order `third_party/CMakeLists.txt` applies them in. Each patch applies independently to the pinned commit, and the ordered series applies cleanly.

| Patch | Why it remains |
| --- | --- |
| [cute-interior-border](cute-interior-border.md) | A border that erodes the fill, matching Chibi's entity raster. Upstream's outline now lies inside the edge but draws over the full fill. |
| [cute-imgui-render-target](cute-imgui-render-target.md) | Include native ImGui overlays in canvas captures. |
| [cute-eight-targets](cute-eight-targets.md) | Let Vulkan canvases have the eight color targets SDL_GPU binds. Upstream reports four on Vulkan. |
| [cute-web-no-asyncify](cute-web-no-asyncify.md) | Link the web build without Asyncify. |
| [cute-gles-ubo-instance-scope](cute-gles-ubo-instance-scope.md) | Give anonymous uniform block instances separate stage names. |
| [cute-instance-buffer](cute-instance-buffer.md) | Expose existing shared instance staging through the public C header. |
| [cute-web-webgpu-device](cute-web-webgpu-device.md) | Start WebGPU in the browser from a device the page requests, since the player links without Asyncify. |
| [cute-gles-range-draw-state](cute-gles-range-draw-state.md) | Keep the viewport between GLES range draws, so batched shadow casters stay in their atlas tile. |
| [cute-sound-free-queue-music-volume](cute-sound-free-queue-music-volume.md) | Grow the deferred free queue by pointers and apply the music volume once. |

The October 6 audit published each patch it kept as an unlisted gist, and `gists.json` records that publication. The gists hold the October 6 versions and were not republished for this update. The patches in `third_party/cute_framework`, which the third-party archive builder applies, and these documents are current.

## Behavior changes from upstream

- Strokes of closed shapes (boxes, circles, triangles) run inward from the edge instead of straddling it, and `chubbiness` became `rounding`, a corner radius that keeps the shape's extent ([976ac895](https://github.com/RandyGaul/cute_framework/commit/976ac895)). Chibi's runtime draws its entities through its own raster and passes zero rounding to Cute, so only Studio's overlays and gizmos move by half a stroke.
- Draw lists replay the live render state unless a recording pushed its own ([4e2662b6](https://github.com/RandyGaul/cute_framework/commit/4e2662b6)).
- 2D shapes can test and write depth with `cf_draw_push_z` ([3a66cb58](https://github.com/RandyGaul/cute_framework/commit/3a66cb58)). Nothing changes until a caller pushes a Z.
