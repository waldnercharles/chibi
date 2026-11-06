# Give generated uniform block instances a shader-stage prefix

Keep this patch. Cute's GLSL transpiler gives every anonymous uniform block the generated instance name `cf_ubi_<block-name>` in both shader stages, at upstream commit `96e6034aa6bc44665856ee480962a9ddab0b87cc` as before.

A vertex shader and fragment shader may each declare `uniform_block` with different members. Cute already gives their block declarations a stage-specific name, but the generated instance names still collide, which can make a GLES program fail to link when the driver checks those declarations together.

The patch prefixes the generated instance with `vs` or `fs` and updates the rewritten member expressions to match: `cf_vs_ubi_uniform_block` and `cf_fs_ubi_uniform_block`. Chibi's 3D shaders declare a vertex `uniform_block` and a fragment `uniform_block` with different members.

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

This changes generated GLSL identifiers, not material uniform names or C binding calls. Explicitly named block instances keep their authored names. The WGSL path is unaffected.

## Apply the patch

This diff targets Cute Framework commit [`96e6034aa6bc44665856ee480962a9ddab0b87cc`](https://github.com/RandyGaul/cute_framework/commit/96e6034aa6bc44665856ee480962a9ddab0b87cc). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-gles-ubo-instance-scope.patch
git apply cute-gles-ubo-instance-scope.patch
```

Patch SHA-256: `4e961d59aa3ea7058b6d4739331b55b939ead04d774836effbda33c81e1c1ac3`.
