# Copy depth for later SDF visibility tests

Keep this patch. Upstream commit `7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d` exposes a canvas's depth texture but has no `cf_canvas_copy_depth` API. Its GLES texture region copy attaches the source to a color attachment, so that operation does not replace a depth copy. See the immutable [canvas depth accessor](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/include/cute_graphics.h#L1704), [GLES texture copy](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/cute_graphics_gles.cpp#L1325), and [SDL GPU texture copy](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/cute_graphics_sdlgpu.cpp#L1275).

The patch adds a rectangular, GPU-only copy from one canvas's depth attachment into another's. SDL GPU uses its texture copy operation after ending the current pass. GLES uses `glBlitFramebuffer` with `GL_DEPTH_BUFFER_BIT` and `GL_NEAREST`. The GLES path disables scissor during the copy, restores the cached scissor state, and restores the framebuffer binding. A scissor left over from the previous draw must not crop the requested copy.

Chibi's `src/render/mesh_material.c` takes snapshots between actors when hidden-surface rejection is enabled. Later SDF draws sample the snapshot to skip ray marching behind existing geometry. The separate texture avoids reading the depth attachment currently being written. Chibi copies actor bounds where possible to reduce copied pixels.

An illustrative C excerpt, assuming two ordinary, distinct, single-sample canvases with identical dimensions and depth formats:

```c
/* Render earlier geometry into scene, then finish its draw batch. */
cf_canvas_copy_depth(scene, snapshot, 0, 0, width, height);
/* Reapply canvas, shader, viewport, and scissor for the next draw. */
CF_Texture prior_depth = cf_canvas_get_depth_stencil_target(snapshot);
/* Bind prior_depth to the next shader's depth sampler. */
```

Create the snapshot with `CF_TEXTURE_USAGE_SAMPLER_BIT` in `depth_stencil_target.usage` when later shaders sample it. A depth renderbuffer can be a GLES copy source, but a sampleable destination needs a texture. Color is unchanged. Destination stencil is unspecified.

The rectangle uses identical source and destination coordinates and the backend's native render-target origin, matching `texelFetch`. Width and height must be positive, and the rectangle must fit both canvases. The contract requires single-sample canvases with matching extents and depth formats. SDL GPU asserts those conditions and rejects attached subresources. The GLES implementation checks extent, format, and bounds but does not explicitly assert sample counts. Callers must still obey the single-sample contract. There is no multisample resolve or coordinate conversion here.

The copy ends the SDL GPU render pass. Reapply rendering state before drawing again. Chibi's subsequent actor and SDF paths do so. Shader-written SDF depth also requires the separate fragment-depth compiler patch.

Source inspection supports retention. The existing `test/sdf_visibility.c` contains a depth-copy exercise, but this document does not claim that a new GPU run completed during the audit.

## Apply the patch

This diff targets Cute Framework commit [`7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`](https://github.com/RandyGaul/cute_framework/commit/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-depth-snapshot.patch
git apply cute-depth-snapshot.patch
```

Patch SHA-256: `159988bdd15f16a5c3e7fb8ac5248a62d3355a3a2cf538139cc33d83fa00535e`.
