# Expose shared instance staging through the public C header

Keep this patch. Upstream implements standalone instance buffers and one-draw overrides for GLES, SDL_GPU and WebGPU, but declares them only in an internal header.

The patch moves the four declarations to `include/cute_graphics.h` with the public calling convention. It adds no implementation.

Chibi uploads all scheduled SDF instance records once in `src/render/mesh_material.c`, then binds ordered byte ranges of that buffer per draw batch.

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

Upload before beginning the render pass. Each override applies to one draw and must precede `cf_apply_shader`. Offsets must be stride multiples and the range must fit the uploaded data.

## Apply the patch

This diff targets Cute Framework commit [`96e6034aa6bc44665856ee480962a9ddab0b87cc`](https://github.com/RandyGaul/cute_framework/commit/96e6034aa6bc44665856ee480962a9ddab0b87cc). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-instance-buffer.patch
git apply cute-instance-buffer.patch
```

Patch SHA-256: `a81f73cee98c7b606156cb72c7139d827122c8c11a7aa1c419d309f61980af82`.
