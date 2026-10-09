# 3D rendering

A scene with `presentation "3d"` draws through `render/scene3d.c`. Entities with
`GameTransform` and `GameGeometry3D` become lit solids; gameplay x/y maps to world
x/z and `elevation` is the center height. The renderer writes scene-linear RGBA16F
into `render/scene3d_post.c`, which applies bloom, exposure and the tone map.

## Lighting

- **Directional Light** (`GameDirectionalLight`): color, intensity and the world
  direction the light travels. A scene shows at most one; a second visible one is
  an error, and hidden ones are left out. Fog never culls it. With none, only
  ambient and point lights shade the scene. `shadows` lets opaque solids block
  it (see Shadows).
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
into up to four cascades (`render/scene3d_shadow.c`), each a 2048x2048 square
of one depth atlas. The lit pass uses the first cascade, nearest first, whose
square holds the lookup, and compares against it with a 3x3 grid of hardware
2x2 comparisons.

- **Cascades**: the view depths the solids in view actually show, clipped to
  the camera frustum, are split at equal ratios, one cascade per factor of four
  up to four cascades. Anything nearer than the cascades reach shares the
  first. A view that spans less than a factor of four, such as the default
  near-overhead camera over a floor, uses one cascade and one square.
- **Fit**: each cascade's orthographic light view covers what the solids in
  view show within its depths, plus a 4-texel margin. Its depth range also
  takes in opaque solids the camera culls, which still cast into the view when
  they reach the covered area. Each square's size steps by quarter octaves and
  its origin snaps to whole texels, so edges hold still while the camera pans,
  apart from a resolution step when the covered area crosses a size.
- **Bias**: each lookup lifts the surface point along its geometric normal by
  half a texel of its cascade facing the light, up to 2.5 texels edge on.
  Contact shadows start that far from the caster.
- **Memory**: the atlas grows to fit the most cascades a view has needed: one
  square, two side by side, or a 2x2 grid. Depth ranges with nothing in view
  take no square. It stores 32-bit float depth where the device can, then
  24-bit, then 16-bit, so four cascades take at most 64 MB.
- **Not covered**: transparent surfaces, particles and trails cast nothing;
  point lights cast nothing. Cascade edges change resolution without a blend.

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
  `adaptation_speed` per scene second, from 0.1 to 100, so it holds still while
  scene time is paused; zero adapts every frame. The first frame and any jump
  back in scene time, such as a restart, snap to the metered value.

## Optional material inputs

Rhythm, Surface Influence, trails and particles feed materials only when a scene
has them. A scene without a valid rhythm clock draws with the beat at rest.

## Tests

`python3 tools/test_policy.py tool scene3d` renders a box or a floor in a hidden
64x64 app. It checks ambient, the directional light, the one-light limit
(hidden lights left out), drawing without a rhythm clock, both tone maps, auto
exposure's geometric mean, background rejection, easing (including slow easing
finer than a half-float step), snapping and settings checks, and sun shadows: a
box's shadow, no self-shadowing on lit faces, a solid the camera culls casting
into the view (collected across workers), the depth pass leaving earlier queued
draws alone, four cascades across a deep view each keeping a small shadow
sharp, two cascades side by side when the depths between hold nothing, and one
cascade for a floor that reaches far past a shallow view. It runs `test_tools`
from the desktop build, so set `CHIBI_TOOLS_BUILD_DIR=build/test/tools`.
llvmpipe leaks 112 bytes from its own worker threads, so LeakSanitizer hosts on
llvmpipe need `ASAN_OPTIONS=detect_leaks=0`. Debug ASan+UBSan on Linux with
Vulkan llvmpipe passed 17 cases and 92 assertions in 2.5 seconds on 2026-10-09.

`test/fixtures/scene3d` is the manual preview scene and enables every feature
above.
