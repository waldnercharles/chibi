# 3D rendering

A scene with `presentation "3d"` draws through `render/scene3d.c`. Entities with
`GameTransform` and `GameGeometry3D` become lit solids; gameplay x/y maps to world
x/z and `elevation` is the center height. The renderer writes scene-linear RGBA16F
into `render/scene3d_post.c`, which applies bloom, exposure and the tone map.

## Lighting

- **Directional Light** (`GameDirectionalLight`): color, intensity and the world
  direction the light travels. A scene has at most one; a second is an error. With
  none, only ambient and point lights shade the scene. `shadows` lets opaque
  solids block it (see Shadows).
- **Point Light** (`GamePointLight`): up to 32 per view, ranked by estimated
  influence on the camera.
- **Environment** (`GameEnvironment`): exposure, bloom, and a hemisphere ambient
  that blends `ambient_ground` below to `ambient_sky` above, scaled by
  `ambient_intensity`. Without an Environment the scene uses
  `game_environment_defaults()`.

Omitted KDL fields are zero, so author every lighting field you need. Studio adds
both components with their defaults filled in.

## Shadows

A Directional Light with `shadows` renders opaque solids' depth from the light
into one 2048x2048 depth map (`render/scene3d_shadow.c`), then the lit pass
compares against it with a 3x3 grid of hardware 2x2 comparisons.

- **Fit**: an orthographic light view covers every solid within the depth range
  the camera sees them at, so solids outside the view still cast into it. The
  map's size steps by quarter octaves and its origin snaps to whole texels, so
  edges hold still while the camera pans.
- **Bias**: each lookup lifts the surface point along its geometric normal by
  half a texel facing the light, up to 2.5 texels edge on. Contact shadows start
  that far from the caster.
- **Not covered**: transparent surfaces, particles and trails cast nothing;
  point lights cast nothing. A view much deeper than the scene's detail spreads
  the map thin; cascades are the planned fix.

The material preview sphere draws without shadows.

## Display

The Environment also sets how scene-linear light reaches the display:

- **Tone mapping**: `AgX`, the default and the 2D path's curve, is filmic and
  desaturates bright highlights. `Neutral` (Khronos PBR Neutral) keeps colors
  below its knee as authored and only compresses highlights. Both share
  `shaders/tonemap.shd` with the 2D present pass.
- **Auto exposure**: meters the mean log2 luminance of the frame's lit pixels
  and moves it to middle gray (0.18); `exposure` then acts as compensation in
  stops. Pixels darker than 2^-10, such as an empty background, are left out,
  so the boost tops out near 7.5 stops, and a frame with no lit pixels keeps
  the authored exposure. It eases toward the metered value at
  `adaptation_speed` per scene second, so it holds still while scene time is
  paused; zero adapts every frame. The first frame and any jump back in scene
  time, such as a restart, snap to the metered value.

## Optional material inputs

Rhythm, Surface Influence, trails and particles feed materials only when a scene
has them. A scene without a valid rhythm clock draws with the beat at rest.

## Tests

`python3 tools/test_policy.py tool scene3d` renders a box or a floor in a hidden
64x64 app. It checks ambient, the directional light, the one-light limit,
drawing without a rhythm clock, both tone maps, auto exposure's geometric
mean, background rejection, easing, snapping and settings checks, and sun
shadows: a box's shadow, no self-shadowing on lit faces, and a solid outside the
view casting into it. It runs `test_tools` from the
desktop build, so set `CHIBI_TOOLS_BUILD_DIR=build/test/tools`. llvmpipe leaks
112 bytes from its own worker threads, so LeakSanitizer hosts on llvmpipe need
`ASAN_OPTIONS=detect_leaks=0`. Debug ASan+UBSan on Linux with Vulkan llvmpipe
passed 11 cases and 56 assertions in 1.4 seconds on 2026-10-09.

`test/fixtures/scene3d` is the manual preview scene and enables every feature
above.
