# Keep the cached GLES scissor box equal to GL's box

Retain this patch. Upstream copies the requested state into its cache even when scissoring is disabled and it has not called `glScissor`.

GL keeps the last scissor box when the scissor test is off. Consider a clipped draw using box A, a pass that disables scissoring and requests box B, then a clipped draw using box B. Upstream records B during the disabled pass. The final draw sees B in the cache and skips `glScissor`, leaving GL on A. Pixels outside A disappear.

The patch preserves the previously applied box in the cache while scissoring is disabled. When the next pass enables box B, the changed box causes the required GL call.

Chibi alternates scene passes and RmlUi passes. `src/ui/rml_gpu.c` pushes a canvas scissor for UI batches, and `test/editor/rmlui_render.c` contains an existing fixture for UI rendering after an earlier pass leaves a tiny scissor. That fixture is evidence of the relevant application behavior, not a claim that it was run in this audit.

This patch only changes GLES state caching. It does not alter the SDL_GPU backend, clipping coordinates, or scissor rectangles supplied by the application.

[Audited upstream scissor comparison and cache assignment](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/cute_graphics_gles.cpp#L638-L682).

Validation here is source comparison. No fresh GPU execution is claimed.

## Apply the patch

This diff targets Cute Framework commit [`7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`](https://github.com/RandyGaul/cute_framework/commit/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-gles-scissor-cache.patch
git apply cute-gles-scissor-cache.patch
```

Patch SHA-256: `72b0d8277a6c6c31017c86d9eefe09a84c734cc5fad15ff4e62d495699aac7c9`.
