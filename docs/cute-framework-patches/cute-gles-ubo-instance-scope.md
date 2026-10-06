# Give generated uniform block instances a shader-stage prefix

Retain this patch. Cute's GLSL transpiler gives every anonymous uniform block the generated instance name `cf_ubi_<block-name>` in both shader stages.

A vertex shader and fragment shader may each declare `uniform_block` with different members. Cute already assigns their block declarations a stage-specific name. Its generated instance names still collide, which can make a GLES program fail to link when the driver checks those declarations together.

The patch prefixes the generated instance with `vs` or `fs`, and updates the rewritten member expressions to match. The vertex stage uses `cf_vs_ubi_uniform_block`; the fragment stage uses `cf_fs_ubi_uniform_block`.

Chibi uses this pattern directly. `src/render/shaders/arena3d.vert` declares a vertex `uniform_block` containing `u_view_projection`. `src/render/shaders/arena3d_scene.glsl` declares a fragment `uniform_block` containing scene and lighting values. The names describe independent stage data.

Illustrative shader pair, not a new reproduction from this audit.

```glsl
// Vertex shader.
layout(set = 1, binding = 0) uniform uniform_block {
    mat4 transform;
};

// Fragment shader.
layout(set = 3, binding = 0) uniform uniform_block {
    vec4 tint;
};
```

This changes generated GLSL identifiers, not the original material uniform names or C binding calls. It addresses anonymous blocks. Explicitly named block instances keep their authored names. No claim is made that all drivers reject the old names.

[Upstream member rewriting](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/libraries/cute/cute_spirv.h#L7897-L7904) and [upstream generated block instance](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/libraries/cute/cute_spirv.h#L7990-L7992).

Validation here is source comparison. No fresh shader compilation or GLES link is claimed.

## Apply the patch

This diff targets Cute Framework commit [`7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`](https://github.com/RandyGaul/cute_framework/commit/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-gles-ubo-instance-scope.patch
git apply cute-gles-ubo-instance-scope.patch
```

Patch SHA-256: `f4a0893cb2fa9218b1c565e2849b3638895efab9fd3a6c2f2a8d2f55217422ed`.
