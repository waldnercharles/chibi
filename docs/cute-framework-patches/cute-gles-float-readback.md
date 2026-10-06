# Read RGBA16F canvases through the GLES float readback pair

Retain a narrowed version of this patch. Upstream commit `b945aa36885f78974b4c3bec419b32566e4440b2` fixed readback sizes and added per-format readback. The earlier patch's blanket RGBA format handling is now obsolete.

One portability issue remains. Upstream reads RGBA16F attachments with `GL_RGBA` and `GL_HALF_FLOAT`. GLES guarantees `GL_RGBA` and `GL_FLOAT` for floating-point color buffers. The half-float pair depends on the implementation. WebGL2 makes the float pair valid through [EXT_color_buffer_float](https://registry.khronos.org/webgl/extensions/EXT_color_buffer_float/). A driver can reject the HALF_FLOAT pair, and upstream then returns a failed readback.

The rebased patch reads RGBA16F attachments as four floats per pixel and converts those values back to packed IEEE half-floats. The public result remains eight bytes per pixel, matching the target layout and SDL_GPU readbacks. It keeps upstream's integer formats, channel counts, error handling, and pack alignment for other target formats. RGBA32F already uses the guaranteed float pair and needs no extra conversion.

Chibi's lighting and SDF targets use RGBA16F. `test/sdf_quality.c` reads eight bytes per pixel, then decodes four half-floats to compare material height and normal values. A failed readback prevents that check from inspecting the rendered values.

Illustrative C excerpt for an existing RGBA16F canvas. It has not been reproduced as part of this audit.

```c
CF_Readback readback = cf_canvas_readback(canvas);
if (readback.id && cf_readback_ready(readback)) {
    int bytes = width * height * 4 * sizeof(uint16_t);
    uint16_t *pixels = malloc(bytes);
    if (cf_readback_size(readback) == bytes)
        cf_readback_data(readback, pixels, bytes);
    free(pixels);
}
if (readback.id) cf_destroy_readback(readback);
```

This only covers RGBA16F readback on GLES and WebGL2. It does not add support for every half-float channel layout, compressed format, or unreadable attachment. A valid renderable floating-point attachment is still required. The conversion is exact for finite values originating in half-float storage; it canonicalizes NaN payloads. GLES readback remains synchronous and needs a temporary sixteen-byte-per-pixel buffer.

[Upstream format-aware readback commit](https://github.com/RandyGaul/cute_framework/commit/b945aa36885f78974b4c3bec419b32566e4440b2) and [audited readback implementation](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/cute_graphics_gles.cpp#L3360-L3403).

Validation here is source comparison and patch applicability. No fresh GPU readback is claimed.

## Apply the patch

This diff targets Cute Framework commit [`7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`](https://github.com/RandyGaul/cute_framework/commit/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-gles-float-readback.patch
git apply cute-gles-float-readback.patch
```

Patch SHA-256: `2fac74ad52242831d3083018ae4aaf170bb439f943d460d041f87fd637353117`.
