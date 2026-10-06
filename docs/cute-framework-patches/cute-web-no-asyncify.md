# Run Chibi's browser frames without Asyncify

`cute-web-no-asyncify.patch` removes Cute's inherited Emscripten Asyncify flags and its forced `-O1` compile option. It also changes the WebGL streaming-buffer fallback so it reuses the next ring slot without calling `cf_sleep` inside the frame. It deletes the slot's fence and clears the CPU bookkeeping before reuse.

## Why retain it

At audited upstream commit `7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`, [Cute's web configuration](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/CMakeLists.txt#L308-L339) still enables Asyncify. Its [WebGL ring fallback](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/cute_graphics_gles.cpp#L552-L594) polls a fence and calls `cf_sleep(0)` until the slot becomes ready.

WebGL fence completion depends on returning control to the browser. A frame that exhausts the buffer ring cannot block and wait as a native GL frame can. Cute uses an asynchronous yield to handle that case. Chibi instead schedules browser frames through `emscripten_set_main_loop` in `src/host/main.c` and avoids suspending that draw call. WebGL orders the later buffer upload after the earlier draw commands that used the slot, so reuse can proceed through the API's ordering rules.

The patch removes Asyncify instrumentation from the generated module. This audit does not claim a measured reduction in build time or output size.

## Example

This C example illustrates Chibi's scheduling model. It is not a standalone reproduction of ring exhaustion.

```c
#include <emscripten.h>

static void browser_frame(void)
{
    update_game();
    draw_game();
    /* Return to the browser after this frame. */
}

int main(void)
{
    initialize_game();
    emscripten_set_main_loop(browser_frame, 0, 1);
    return 0;
}
```

The functions other than `emscripten_set_main_loop` are application placeholders.

## Limits

This is a Chibi build choice, not a general replacement for Cute's asynchronous APIs. Applications using suspendable sleeps, coroutines or fibers may still need Asyncify or another compatible implementation. Removing the flag does not automatically rewrite those application calls.

The slot change applies only under `CF_EMSCRIPTEN`. Native GL keeps its wait behavior, and SDL GPU does not use this ring. The patch relies on ordered WebGL buffer operations; it does not authorize writing into memory mapped for an unfinished native GPU operation.

Validation for this explanation is source inspection. Browser build and runtime results belong to the upgrade audit report.

## Apply the patch

This diff targets Cute Framework commit [`7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`](https://github.com/RandyGaul/cute_framework/commit/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-web-no-asyncify.patch
git apply cute-web-no-asyncify.patch
```

Patch SHA-256: `a1d8e222aed262605f923ec1d6c765f9e83eca9684337cd88be41c1129fc8cff`.
