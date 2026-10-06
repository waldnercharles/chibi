# Render warped custom fields without unsafe distance culling

Keep `cute-custom-coverage.patch`. Upstream `7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d` registers custom SDFs through [cf_make_custom_shape](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/cute_draw.cpp#L2436), then uses their field values in [tile-distance rejection](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/tools/builtin_shaders.h#L1543). There is no coverage-only registration mode.

Chibi's `src/render/sdf_shapes.c` warps positions with angular sine waves and localized traveling plucks before evaluating the base shape. The resulting field still defines the desired visible contour, but it need not satisfy the distance bound that tile culling assumes. A field value at the tile center can overestimate the distance to visible coverage elsewhere in the tile. The renderer can then skip a tile that contains part of the shape. An unsafe opaque-cover claim can also remove the background beneath it.

The patch adds `cf_make_custom_shape_coverage`. These shapes use the caller's conservative bounds for tile binning. They skip distance-based tile rejection and never claim opaque tile cover. The fragment shader still evaluates the supplied field unchanged for fill, stroke, antialiasing and border effects. Chibi calls this API in `app_sdf_shapes_init` and supplies deformation padding in `app_sdf_shapes_draw`.

The caller must enclose every visible part of the field in the submitted bounds. This API cannot rescue a clipped bounding box, and it does not turn a warped field into a true distance function. Stroke widths and antialiasing remain dependent on that field's scale. Coverage-only shapes cannot participate in shape groups because smooth CSG requires distance bounds. The patch rejects that combination.

## Example

Illustrative C code, called after Cute draw initialization. A radius-20 circle gains an eight-unit radial ripple. The conservative bounds include the ripple. The angular warp makes the ordinary distance-culling assumption unsafe.

```c
const char *source =
    "float sdf(vec2 p, ShapeParams s) {\n"
    "  float a = atan(p.y, p.x);\n"
    "  return length(p) - 20.0 - 8.0 * sin(12.0 * a);\n"
    "}\n";
CF_CustomShape shape = cf_make_custom_shape_coverage(source);
CF_Aabb bounds = { { -29, -29 }, { 29, 29 } };
cf_draw_custom_shape_fill(shape, bounds, NULL, 0);
```

Registration still requires the upstream command draw renderer and successful shader compilation. The patch does not add a fallback renderer or guarantee support on every device. `test/sdf_coverage.c` compares a warped diamond against a CPU reference and checks background preservation in tiled and instanced paths. Those tests were not executed as part of this document audit.

This patch applies independently to upstream and can be combined with `cute-interior-border.patch`. `cute-custom-shape-cache.patch` adds registration reuse on top of this API.

## Apply the patch

This diff targets Cute Framework commit [`7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`](https://github.com/RandyGaul/cute_framework/commit/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-custom-coverage.patch
git apply cute-custom-coverage.patch
```

Patch SHA-256: `a25399671e969e0c69ca8f1094b66a7746c2493fb1a03d25064f1ad4d1fa6228`.
