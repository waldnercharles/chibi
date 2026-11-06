# Draw a border inside an SDF shape

Keep `cute-interior-border.patch`. Since [976ac895](https://github.com/RandyGaul/cute_framework/commit/976ac895), upstream's `cf_draw_push_outline` is a band inside the edge drawn over the full fill, like a CSS border. A translucent outline therefore shows the fill beneath it.

Chibi's entity raster (`src/render/shaders/hrc/entity_raster.frag`) erodes the fill instead: the border takes the shape's edge coverage and a translucent border shows what is behind the shape. Studio's scene viewport draws appearances through Cute with `cf_draw_push_interior_border` so that its preview matches the runtime.

The patch adds `cf_draw_push_interior_border`. It encodes the width as a negative value in the outline stack, so `cf_draw_pop_outline` pops it. `cf_draw_push_outline` clamps nonpositive widths to zero, so its behavior is unchanged. In the shared SDF shader, a negative width erodes the fill by the width, gives the rest of the shape's coverage to the border, and places the premultiplied border over the eroded fill. Effects follow what was drawn, as upstream's outline does: a stroke's own band, or the filled shape. The border composes with glow. Sprites and text ignore it.

Because the border can be translucent where an opaque fill would not be, interior borders never claim opaque tile cover and never write 2D depth.

## Example

Illustrative C code, called after Cute draw initialization. This draws a radius-16 circle with a three-unit blue border inside its edge.

```c
cf_draw_push_color(cf_make_color_rgba_f(1, 0, 0, 0.4f));
cf_draw_push_interior_border(cf_make_color_rgba_f(0, 0, 1, 0.75f), 3);
cf_draw_circle_fill2((CF_V2){ 0, 0 }, 16);
cf_draw_pop_outline();
cf_draw_pop_color();
```

The `interior-border` tool selector (`test/render/interior_border.c`) compares circles and diamonds with opaque and translucent fills against a CPU reference, through both the instanced and tiled draw paths.

## Apply the patch

This diff targets Cute Framework commit [`96e6034aa6bc44665856ee480962a9ddab0b87cc`](https://github.com/RandyGaul/cute_framework/commit/96e6034aa6bc44665856ee480962a9ddab0b87cc). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-interior-border.patch
git apply cute-interior-border.patch
```

Patch SHA-256: `31b56685e3bc889f8553900052ff1016a88c5945f5c4a39bb227e53f06e2e00b`.
