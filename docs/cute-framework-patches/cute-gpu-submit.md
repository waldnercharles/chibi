# Submit GPU work without a CPU wait

Keep this patch. Upstream commit `7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d` exposes `cf_gpu_sync`, which waits for GPU completion, but does not expose `cf_gpu_submit`. Its internal SDL GPU flush submits the command buffer and leaves no replacement. See the immutable [public synchronization API](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/include/cute_graphics.h#L970) and [SDL GPU flush and sync implementations](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/cute_graphics_sdlgpu.cpp#L964).

The patch adds `cf_gpu_submit`. SDL GPU ends the active render pass, submits recorded commands, and acquires a new command buffer so recording can continue. GLES calls `glFlush`, which requests execution without waiting for completion.

Chibi uses this in `src/render/gpu_timing.c`. Its timestamp mode attributes completed command buffers to named rendering stages. Submitting at stage boundaries lets the host associate a command buffer with a stage. Using `cf_gpu_sync` at every boundary would stall the CPU and change the workload being measured.

An illustrative C excerpt:

```c
/* Record stage A's low-level GPU commands. */
cf_gpu_submit();
/* Record stage B's low-level GPU commands. */
cf_gpu_submit();
/* A profiler can associate separately submitted buffers with A and B. */
```

This API does not drain Cute's queued high-level drawing or create timestamps by itself. Flush high-level draw batches through the normal rendering API before using a submission as a measurement boundary. Chibi's host separately tracks submitted command buffers and reports their execution intervals.

SDL GPU submission ends the render pass; the next draw must establish the needed drawing state. GLES has no equivalent command-buffer boundary, so `glFlush` alone cannot provide the SDL profiler's attribution. WebGL spans use timer queries where available. Neither backend waits for completion here. Excessive submissions can change performance, so stage timing should be treated as instrumentation.

The rebased patch applies independently to the audited upstream. Source inspection supports retention; this document does not claim a new timing benchmark.

## Apply the patch

This diff targets Cute Framework commit [`7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`](https://github.com/RandyGaul/cute_framework/commit/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-gpu-submit.patch
git apply cute-gpu-submit.patch
```

Patch SHA-256: `726e59440b5f64e21c265995b5642d78475ba0bf579f6496d107d0d3e560147b`.
