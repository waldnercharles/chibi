# Reuse identical custom shape registrations

Keep `cute-custom-shape-cache.patch`. In upstream `7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`, [cf_make_custom_shape](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/cute_draw.cpp#L2436) appends source and recompiles the full custom-shape dispatcher on every registration. It does not check for identical source already registered.

Chibi registers a fixed shader string in `app_sdf_shapes_init`, in `src/render/sdf_shapes.c`. The scene viewport calls this initializer when its shape handle is empty. Another viewport or a recreated scene can request the same source while Cute's draw context and registry are still alive. Without reuse, each request adds a duplicate dispatcher branch and recompiles GPU pipelines.

The patch compares the source string and coverage mode with existing entries. An exact match returns the existing shape ID without compiling again. Different source or a different coverage mode still creates a new registration. It does not normalize whitespace, evict old shapes, or bound growth from genuinely different source strings. Its cache lasts only as long as the draw context.

## Example

Illustrative C code, called after Cute draw initialization. The second registration returns the first handle when registration succeeds.

```c
const char *source =
    "float sdf(vec2 p, ShapeParams s) { return length(p) - 20.0; }";
CF_CustomShape first = cf_make_custom_shape_coverage(source);
CF_CustomShape second = cf_make_custom_shape_coverage(source);
/* first.id == second.id, with no second pipeline rebuild. */
```

Apply [cute-custom-coverage.patch](https://gist.github.com/waldnercharles/d43c4946532037526135b7ea258e422d) first. The cache key includes its coverage flag, so this patch is not standalone against upstream. Backend availability and shader compilation follow upstream custom-shape registration. This audit establishes the repeated-registration path from source inspection; it does not report a measured compilation-time saving or a new runtime reproduction.

## Apply the patch

This diff targets Cute Framework commit [`7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`](https://github.com/RandyGaul/cute_framework/commit/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d). Run these commands from a checkout of that commit.

Apply [cute-custom-coverage.patch](https://gist.github.com/waldnercharles/d43c4946532037526135b7ea258e422d) first. The cache uses its registration function and per-shape coverage flag.

```sh
git apply --check cute-custom-shape-cache.patch
git apply cute-custom-shape-cache.patch
```

Patch SHA-256: `7be06921af478057e892ac83dafbe76ae0b09b9be209bb1c0ea7f18a52ec343f`.
