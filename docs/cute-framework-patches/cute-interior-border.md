# Draw a border inside an SDF shape

Keep `cute-interior-border.patch`. Upstream `7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d` has outer outlines and glows, but no interior-border API. Its [effect shader](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/tools/builtin_shaders.h#L452) expands an outline outside the shape.

Chibi needs a border that preserves the authored silhouette. `src/editor/workspace/scene_viewport.c`, in `draw_appearance`, uses the border color and width from `GameAppearance`. An outer outline changes the visible size. Drawing a smaller shape over a larger one also changes translucent compositing and requires a second draw.

The patch adds `cf_draw_push_interior_border`. It erodes the fill by the requested width, assigns the remaining edge coverage to the border, and composites premultiplied border over fill. It encodes interior width as a negative value in the existing outline state. Ordinary `cf_draw_push_outline` clamps nonpositive widths to zero, so its previous disabling behavior remains intact. The matching pop is `cf_draw_pop_outline`.

The command renderer carries the effect payload for negative widths and disables opaque tile-cover claims for interior borders. A translucent border must not cause it to discard geometry behind the shape. The shared SDF shader handles filled and stroked shapes and can combine the border with glow. Sprites and text ignore this effect.

## Example

Illustrative C code, called after Cute draw initialization. This draws a radius-16 circle with a three-unit blue border inside its existing edge.

```c
cf_draw_push_color(cf_make_color_rgba_f(1, 0, 0, 0.4f));
cf_draw_push_interior_border(cf_make_color_rgba_f(0, 0, 1, 0.75f), 3);
cf_draw_circle_fill2((CF_V2){ 0, 0 }, 16);
cf_draw_pop_outline();
cf_draw_pop_color();
```

`test/interior_border.c` contains pixel comparisons for circles and diamonds with opaque and translucent fills, using both tiled and instanced draw paths. The test's existence is evidence of intended coverage, not a claim that it passed during this upgrade. This patch does not add a standalone graphics-backend API and has no dependency on another local patch.

## Apply the patch

This diff targets Cute Framework commit [`7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`](https://github.com/RandyGaul/cute_framework/commit/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-interior-border.patch
git apply cute-interior-border.patch
```

Patch SHA-256: `8cdfbfd97cf33e744eff27e3cccf7ff757335bf0c82594b8d3922fa37858045c`.
