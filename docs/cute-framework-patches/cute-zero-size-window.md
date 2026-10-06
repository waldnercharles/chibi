# Keep a valid canvas while the window has no area

`cute-zero-size-window.patch` stops Cute from rebuilding its default canvas when the computed width or height is zero or negative. It also rejects a pixel-density value that is not positive, including NaN. App creation falls back to density 1; later density refreshes keep the last valid value.

## Why retain it

At audited upstream commit `7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`, [canvas recreation](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/cute_app.cpp#L162-L169) still passes the computed dimensions directly to `s_canvas`. The creation path and [input refresh](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/cute_input.cpp#L511-L521) still use `pixel_scale <= 0.0f`. That comparison is false for NaN, so NaN can reach canvas-size arithmetic.

An SDL window can report no area during layout or visibility transitions. Browser canvases are one trigger. SDL's pixel density can then involve zero divided by zero. Chibi embeds its web runtime in changing editor layouts and reads Cute's pixel scale for Studio rendering. Keeping the last valid canvas and density allows a later resize with valid dimensions to recover.

The rebased patch preserves upstream's new `cf_draw_on_app_canvas_resized` callback for valid dimensions. It skips both canvas allocation and that callback while the window has no area.

## Example

This C fragment illustrates the comparison that needed changing. It is not a reproduced browser failure.

```c
#include <math.h>
#include <stdbool.h>

float density = NAN;
bool old_rejects = density <= 0.0f;   /* false */
bool new_rejects = !(density > 0.0f); /* true */
```

## Limits

This patch guards canvas recreation and density updates. It does not redesign app creation around a zero-sized initial window or filter every invalid floating-point value. Positive infinity also passes the positivity check. The guards apply to both native and web Cute builds; the NaN trigger depends on the window system and SDL backend.

The rebased patch passes `git apply --check` against the audited upstream commit. No browser transition was reproduced during this source audit.

## Apply the patch

This diff targets Cute Framework commit [`7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`](https://github.com/RandyGaul/cute_framework/commit/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-zero-size-window.patch
git apply cute-zero-size-window.patch
```

Patch SHA-256: `0b301e14ab694af08d070fc9834152e51412ca8da625a7e62972418eea4b3171`.
