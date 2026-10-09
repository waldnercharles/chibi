# Give Vulkan canvases eight color targets

Keep `cute-eight-targets.patch`. Upstream raised `CF_MAX_CANVAS_TARGETS` to eight in [5f115922](https://github.com/RandyGaul/cute_framework/commit/5f115922) and added `cf_query_max_canvas_targets`, which `cf_make_canvas` enforces. On SDL_GPU it reports eight for D3D12 and Metal but four for Vulkan, the spec's guarantee, because SDL_GPU does not expose the device's `maxColorAttachments`.

Chibi's material raster writes six outputs in one draw (`render_material_canvas` in `src/render/material_raster.c`), and the imported-mesh material pass in `src/render/mesh_material.c` writes five. With upstream's limit, both canvases fail to create on Vulkan, which is the default backend on Linux.

SDL_GPU binds up to [eight color targets](https://github.com/libsdl-org/SDL/blob/release-3.4.0/src/gpu/SDL_sysgpu.h#L38), and desktop Vulkan drivers, lavapipe and MoltenVK report eight color attachments. The patch makes `cf_sdlgpu_query_max_canvas_targets` return eight on every SDL_GPU backend and updates the header, the graphics topic and Cute's MRT test to match. GLES and WebGPU keep upstream's device queries.

A Vulkan device that really allows only four attachments would now fail when the pipeline is created rather than when the canvas is. No such desktop device is known.

## Apply the patch

This diff targets Cute Framework commit [`96e6034aa6bc44665856ee480962a9ddab0b87cc`](https://github.com/RandyGaul/cute_framework/commit/96e6034aa6bc44665856ee480962a9ddab0b87cc). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-eight-targets.patch
git apply cute-eight-targets.patch
```

Patch SHA-256: `7c4fe672133c80a8a57f4f841d8f9cf42738106c2964b9c365e802df35a7f239`.
