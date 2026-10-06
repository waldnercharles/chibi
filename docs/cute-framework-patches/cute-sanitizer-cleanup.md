# Release shader, material and font allocations

`cute-sanitizer-cleanup.patch` fixes several allocation lifetimes inside Cute. The active Chibi need is shader and material cleanup. Chibi compiles authored shaders in `src/render/shader_program.c` and creates materials in its rendering passes. Repeated compilation and resource destruction should release the allocations made for each object.

## What changes

- The shader compiler tracks arena-backed AST expressions, statements and struct types whose child arrays use separate heap allocations. Cleanup frees function-call arguments, statement blocks, switch groups and struct field arrays before it releases the arena. It also frees the array, image and layout type maps.
- Draw and material destruction call `cf_destroy_arena` for owned arenas. Resetting an arena retains its blocks for reuse and does not release them.
- Font loading distinguishes borrowed memory from file data that Cute allocated. `cf_make_font_from_memory` borrows the buffer until font destruction. `cf_make_font` owns the buffer it reads, frees it on failure, and marks ownership on success. App shutdown destroys registered fonts and their glyph pixels.
- SDL GPU compute-shader destruction invokes the internal object's destructor before freeing it, so its member containers release their allocations.

## Upstream status

These fixes remain necessary at audited upstream commit `7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`. See [compiler cleanup](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/libraries/cute/cute_spirv.h#L9957-L10000), [material destruction](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/cute_graphics.cpp#L860-L868), [font loading and destruction](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/cute_draw.cpp#L2600-L2680), and [SDL compute-shader destruction](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/cute_graphics_sdlgpu.cpp#L2746-L2752).

Upstream separately fixed the stale global draw pointer in [commit 625e5442f11db43e81369a2cea94dfc36449a770](https://github.com/RandyGaul/cute_framework/commit/625e5442f11db43e81369a2cea94dfc36449a770). The rebased cleanup patch preserves that existing `s_draw = NULL` assignment. It does not add a second fix for it.

## Example

This C example illustrates the borrowed-buffer contract after the patch. It is not a reproduced sanitizer failure. It assumes a valid app and a valid TTF buffer allocated by the caller.

```c
CF_Result result = cf_make_font_from_memory(ttf_bytes, ttf_size, "ui");
if (!cf_is_error(result)) {
    /* Keep ttf_bytes alive while this font is registered. */
    cf_destroy_font("ui");
}
free(ttf_bytes);
```

Without the patch, font creation failure or font destruction frees the supplied buffer itself. A caller that supplies static bytes or frees its own buffer can hit an invalid free or double free. The patch makes ownership explicit in the header comments.

## Limits and validation

Compiler and material cleanup apply across graphics backends. The compute destructor change applies to SDL GPU. Chibi has no direct Cute font-loading or compute-shader calls in its checked-in source; those parts remain library fixes rather than demonstrated Chibi workloads.

This patch does not claim to fix every Cute leak. The rebased patch passes `git apply --check` against the audited upstream commit. This audit inspected allocation and cleanup code; it did not reproduce the examples under a sanitizer.

## Apply the patch

This diff targets Cute Framework commit [`7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`](https://github.com/RandyGaul/cute_framework/commit/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-sanitizer-cleanup.patch
git apply cute-sanitizer-cleanup.patch
```

Patch SHA-256: `54fb8f16d9b1899d3ac9ac04700733fd83abd3b90f3c7aa4147aa3e778c4545a`.
