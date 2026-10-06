# Allow up to eight canvas color targets

Keep `cute-eight-targets.patch`. Upstream `7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d` still defines [CF_MAX_CANVAS_TARGETS as four](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/include/cute_graphics.h#L1549). Its backend canvas and pipeline arrays use that constant. Its [GLES canvas creation](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/cute_graphics_gles.cpp#L1367) clamps requests to the constant.

Chibi's material raster writes six outputs in one draw. `render_material_canvas` in `src/render/material_raster.c` allocates scene color, emission, albedo, normal, visibility and cover targets. The imported-mesh material pass in `src/render/mesh_material.c` requests five outputs. Four array entries cannot represent those passes; application code writes beyond the upstream target array when filling the fifth and sixth entries.

The patch raises `CF_MAX_CANVAS_TARGETS` to eight. On GLES it checks both `GL_MAX_DRAW_BUFFERS` and `GL_MAX_COLOR_ATTACHMENTS` before allocation and returns an empty canvas when the request exceeds either limit. SDL_GPU defines [MAX_COLOR_TARGET_BINDINGS as eight in SDL 3.4.0](https://github.com/libsdl-org/SDL/blob/release-3.4.0/src/gpu/SDL_sysgpu.h#L38). WebGL2 and GLES 3.0 only guarantee four, so this change does not make Chibi's six-target pass portable to every web device. Applications must handle canvas creation failure.

## Example

Illustrative C code. This allocates six single-sample RGBA16F outputs, matching the structure of Chibi's material pass. A compatible shader must write all required outputs.

```c
CF_CanvasParams params = cf_canvas_defaults(512, 512);
params.target_count = 6;
for (int i = 0; i < 6; ++i) {
    params.targets[i].pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    params.targets[i].filter = CF_FILTER_LINEAR;
}
CF_Canvas canvas = cf_make_canvas(params);
if (!canvas.id) {
    /* Report that this device cannot create the requested canvas. */
}
```

The patch does not add floating-point format support, GLES MRT multisampling, or a fallback that splits a pass into several draws. It has no dependency on another local patch. This audit confirms the active five- and six-target call sites and the missing upstream capacity; it does not claim device validation of all eight attachments.

## Apply the patch

This diff targets Cute Framework commit [`7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`](https://github.com/RandyGaul/cute_framework/commit/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-eight-targets.patch
git apply cute-eight-targets.patch
```

Patch SHA-256: `1a30b56adbfd4812aa8601223a3c5281884664102136ab765bd5ce5112ffe25d`.
