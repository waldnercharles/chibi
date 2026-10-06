# Compile shaders that write fragment depth

Keep this patch. Upstream commit `7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d` does not recognize `gl_FragDepth` in its GLSL compiler. Its identifier lookup reports an undeclared identifier when no symbol exists. See the immutable [reference lookup](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/libraries/cute/cute_spirv.h#L3587).

The patch declares a float fragment output when the compiler first encounters `gl_FragDepth`. It adds that output to the SPIR-V entry point interface, decorates it with `FragDepth`, and emits the `DepthReplacing` execution mode. HLSL translation returns the value through `SV_Depth`; Metal translation returns it through `[[depth(any)]]`.

The symbol lives in the compiler's global scope even when its first use occurs inside a helper function or branch. Later functions must refer to the same output after the first function's scope has ended. Shaders that never reference `gl_FragDepth` receive no extra depth output.

Chibi's `src/render/shaders/sdf_material.frag` writes depth at the ray's intersection with the signed distance field. A rasterized bounding shape only supplies a starting region for the ray march. Its interpolated depth cannot represent the hit on the actual surface. Correct fragment depth lets the SDF participate in depth testing with meshes and other SDFs. `test/sdf_visibility.c` also uses a shader that seeds depth explicitly.

Illustrative GLSL, rather than a reproduced standalone test:

```glsl
layout(location = 0) out vec4 color;
layout(set = 3, binding = 0) uniform uniform_block {
    float hit_depth;
};
void write_hit() {
    gl_FragDepth = hit_depth;
}
void main() {
    write_hit();
    color = vec4(1.0);
}
```

The application must compute depth using the projection and depth convention used by its rendering pass. It must enable the appropriate depth testing and writes. This patch adds compiler support; it does not calculate depth. Every surviving shader path must write a defined depth when it uses this output. Explicit fragment depth can affect early depth testing.

The patch covers SPIR-V, HLSL, and Metal generation. Native GLSL already defines this builtin, but Cute's compiler still needs to accept it when compiling the shader. Source inspection supports retention; this document does not claim fresh execution on every backend.

## Apply the patch

This diff targets Cute Framework commit [`7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`](https://github.com/RandyGaul/cute_framework/commit/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-fragment-depth.patch
git apply cute-fragment-depth.patch
```

Patch SHA-256: `8c3fd9203aa92c93152c14ba80f05432d364d19e1e24d9305f60bbd075220ed0`.
