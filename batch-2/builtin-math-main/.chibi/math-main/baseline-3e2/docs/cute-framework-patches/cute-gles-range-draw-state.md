# Keep draw state between GLES range draws

Keep this patch until upstream keeps the viewport across `cf_draw_elements_range` calls on GLES. It is needed at upstream commit `96e6034aa6bc44665856ee480962a9ddab0b87cc`.

`cf_draw_elements_range` documents that callers draw several sub-ranges of one mesh "without rebinding anything between draws", and `cf_draw3d_mesh_range` batches do exactly that. They apply the shader, viewport and scissor once, then draw one range per record. SDL_GPU and WebGPU keep the viewport in the render pass. Cute's GLES backend resets its pending draw state to the whole canvas after every draw, so every range after the first one fills the whole canvas.

Chibi's point and spot light shadows draw each face of a light into its own tile of one depth atlas. When one face's casters mix primitives, such as a box and a sphere, they form one batch with a range per primitive. On GLES, and so in WebGL 2 browsers, the later ranges were stretched across the whole atlas and wrote near depths into every face. Floors around the light turned dark wherever those depths landed. The sun's cascades share one atlas the same way.

The patch removes the reset from `cf_gles_draw_elements_range`. A whole-mesh `cf_draw_elements` still resets as before, and applying a canvas still starts from the whole canvas.

`test_tools --scene3d-gles` runs the 3D renderer cases on GLES. Its `point_lights_cast_from_mixed_primitives` case fails without this patch.

## Apply the patch

This diff targets Cute Framework commit [`96e6034aa6bc44665856ee480962a9ddab0b87cc`](https://github.com/RandyGaul/cute_framework/commit/96e6034aa6bc44665856ee480962a9ddab0b87cc). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-gles-range-draw-state.patch
git apply cute-gles-range-draw-state.patch
```

Patch SHA-256: `eecf37a8140c931bc6f1f0959be5b2de5a6149da7a00d5944c8111028a40b0e0`.
