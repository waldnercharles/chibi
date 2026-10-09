# Append GLES uploads before reusing busy buffers

Retain this patch. The audited upstream revision still calls `s_acquire_or_wait` for every vertex, index, and mesh-instance upload.

An application that uploads and draws many small batches in one frame can exhaust the buffer ring. Upstream then waits for the GPU even when the active allocation still has unused bytes. That turns repeated uploads into CPU/GPU synchronization points.

The patch appends each upload after the active slot's previous data when enough space remains. When all slots are busy and there is no room, it re-specifies the buffer storage with `glBufferData`, allowing GL to keep the old allocation for commands already queued. It clears the old fence and in-flight marker when replacing that storage.

This affects the GLES mesh upload path. It does not change the SDL_GPU backend or the separate standalone instance-buffer implementation. Orphaning avoids this explicit CPU wait, but does not guarantee that every driver will allocate without stalling. No performance claim follows from source inspection alone.

Chibi repeatedly uploads mesh data for rendering, including UI quad uploads in `src/ui/rml_gpu.c` and trail vertex and index uploads in `src/render/scene3d_trail.c`. Its shared SDF staging buffer uses a separate upstream instance-buffer API, so that staging path is not the reason to keep this patch.

Illustrative C excerpt, with a configured mesh, shader, material, and canvas. This sequence has not been reproduced as part of this audit.

```c
cf_apply_canvas(canvas, false);
for (int i = 0; i < batch_count; ++i) {
    cf_mesh_update_vertex_data(mesh, batches[i].vertices, batches[i].count);
    cf_apply_mesh(mesh);
    cf_apply_shader(shader, material);
    cf_draw_elements();
}
```

[Audited upstream allocation path](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/cute_graphics_gles.cpp#L1116-L1140).

Validation here is source comparison. This document does not claim a current benchmark or GPU run.

## Apply the patch

This diff targets Cute Framework commit [`7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`](https://github.com/RandyGaul/cute_framework/commit/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-gles-buffer-streaming.patch
git apply cute-gles-buffer-streaming.patch
```

Patch SHA-256: `57ddc97e2d0ff8dedecd2fb75cc04ba35d012c40c2eed67785f26c040451b7cc`.
