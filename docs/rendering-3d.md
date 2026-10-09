# 3D rendering

A scene with `presentation "3d"` draws through `render/scene3d.c`. Entities with
`GameTransform` and `GameGeometry3D` become lit solids; gameplay x/y maps to world
x/z and `elevation` is the center height. The renderer writes scene-linear RGBA16F
into `render/scene3d_post.c`, which applies bloom, exposure and the tone map.

## Lighting

- **Directional Light** (`GameDirectionalLight`): color, intensity and the world
  direction the light travels. A scene has at most one; a second is an error. With
  none, only ambient and point lights shade the scene.
- **Point Light** (`GamePointLight`): up to 32 per view, ranked by estimated
  influence on the camera.
- **Environment** (`GameEnvironment`): exposure, bloom, and a hemisphere ambient
  that blends `ambient_ground` below to `ambient_sky` above, scaled by
  `ambient_intensity`. Without an Environment the scene uses
  `game_environment_defaults()`.

Omitted KDL fields are zero, so author every lighting field you need. Studio adds
both components with their defaults filled in.

## Optional material inputs

Rhythm, Surface Influence, trails and particles feed materials only when a scene
has them. A scene without a valid rhythm clock draws with the beat at rest.

## Tests

`python3 tools/test_policy.py tool scene3d` renders a box in a hidden 64x64 app
and checks ambient, the directional light, the one-light limit, and drawing
without a rhythm clock. It runs `test_tools` from the desktop build, so set
`CHIBI_TOOLS_BUILD_DIR=build/test/tools`. llvmpipe leaks 112 bytes from its
own worker threads, so LeakSanitizer hosts on llvmpipe need
`ASAN_OPTIONS=detect_leaks=0`. Debug ASan+UBSan on Linux with Vulkan llvmpipe
passed 4 cases and 12 assertions in 0.85 seconds on 2026-10-09.
