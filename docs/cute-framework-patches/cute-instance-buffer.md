# Expose shared instance staging through the public C header

Retain this patch. Upstream already implements standalone instance buffers and one-draw overrides for GLES and SDL_GPU, but declares those functions only in an internal header.

The patch moves four declarations to `include/cute_graphics.h` and gives them the public API calling convention. It adds no new upload or draw implementation.

Chibi uploads all scheduled SDF instance records once in `src/render/mesh_material.c`. Its draw batches then bind ordered byte ranges from that shared buffer. Without public declarations, C application code cannot use this existing capability through the supported header.

Illustrative C excerpt. It assumes `mesh` declares matching per-instance attributes and `Instance` has the same stride. It has not been reproduced separately in this audit.

```c
uint64_t staging = cf_make_instance_buffer(count * sizeof(Instance), sizeof(Instance));
cf_update_instance_buffer(staging, instances, count);

cf_apply_canvas(canvas, false);
cf_apply_mesh(mesh);
cf_apply_instance_buffer_override(staging, batch_count, first * sizeof(Instance));
cf_apply_shader(shader, material);
cf_draw_elements();

cf_destroy_instance_buffer(staging);
```

Upload before beginning the render pass. Each override applies to one draw and must precede `cf_apply_shader`. Set it again for each batch. Offsets must be stride multiples and the requested range must fit the uploaded data. The API uses an opaque integer handle and does not introduce application-level range validation.

This patch relies on implementations already present in the audited revision. It does not depend on the GLES buffer-streaming patch, which changes a different upload path.

[Internal declarations](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/internal/cute_graphics_internal.h#L315-L336), [dispatch implementations](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/cute_graphics.cpp#L1275-L1278), [GLES implementation](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/cute_graphics_gles.cpp#L1693-L1736), and [SDL_GPU implementation](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/cute_graphics_sdlgpu.cpp#L1817-L1860).

Validation here is source comparison. The example does not claim a new draw or capture.

## Apply the patch

This diff targets Cute Framework commit [`7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`](https://github.com/RandyGaul/cute_framework/commit/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-instance-buffer.patch
git apply cute-instance-buffer.patch
```

Patch SHA-256: `36450d9db54a5ac55c4a8365742be9a4627d0136fdd9c5bae7e8bcdc8023f8a3`.
