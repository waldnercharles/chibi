# 3D rendering

A scene with `presentation "3d"` draws through `render/scene3d.c`. Entities with
`GameTransform` and `GameGeometry3D` become lit solids; gameplay x/y maps to world
x/z and `elevation` is the center height. The renderer writes scene-linear RGBA16F
into `render/scene3d_post.c`, which applies bloom, exposure and the tone map.

## Camera

A `GameCamera` with a `GameTransform3D` looks along its global -Z with +Y up, so
its local +X points right in the image. Its `offset` moves the eye across the
ground (gameplay y is world z), as it moves an overhead camera's target. One
with only a `GameTransform` frames its span from 10 degrees off overhead. `fov`
is the vertical field of view in radians, from 0.00001 degrees (Unity's
narrowest) to below 180, and zero picks 35 degrees. `near_clip` and `far_clip`
are set together, with far at most a million times near, or both left at zero,
which scales them with the span's height: far is at least 200, and near stays
in front of the framed target. Wider ranges round the far plane toward infinity
in float depth, which breaks picking and fog. The span's aspect shapes the image
either way. Panning or zooming the
viewport returns it to the overhead framing. Fog conceals view rays that never
reach the ground ahead, such as those above the horizon, as it does ground past
its grid. Picking, ground queries and fog unproject view rays about the eye, so
they stay precise however far the eye is from the origin.

## Lighting

- **Directional Light** (`GameDirectionalLight`): color, intensity and the world
  direction the light travels. A scene shows at most one; a second visible one is
  an error, and hidden ones are left out. Fog never culls it. With none, only
  ambient, point and spot lights shade the scene. `shadows` lets opaque solids
  block it, and `angular_diameter`, the sun's apparent size from 0 to 20
  degrees, softens their shadows (see Shadows).
- **Point Light** (`GamePointLight`): up to 32 per view, ranked by estimated
  influence on the camera. `shadows` lets opaque solids within its radius
  block it (see Point and spot light shadows).
- **Spot Light** (`GameSpotLight`): a point light that shines along
  `direction` in a cone. It has full strength out to `inner_angle` across and
  fades to nothing at `outer_angle`, falling off with the square of how far a
  point lies into that edge. The outer angle can be 2 to 160 degrees, and the
  inner angle 0 up to the outer; angles out of range are an error, even on a
  hidden light. Spot and point lights share the 32-light ranking, and a spot
  light without a direction lights nothing. `shadows` works as it does for a
  point light.
- **Environment** (`GameEnvironment`): exposure, bloom, a sky (see Sky), and a
  hemisphere ambient that blends `ambient_ground` below to `ambient_sky` above,
  scaled by `ambient_intensity`. A sky's light replaces the hemisphere ambient.
  Without an Environment the scene uses `game_environment_defaults()`.

Omitted KDL fields are zero, so author every lighting field you need. Studio adds
both components with their defaults filled in.

## Shadows

A Directional Light with `shadows` renders opaque solids' depth from the light
into up to four cascades (`render/scene3d_shadow.c`), each a 2048x2048 square
of one depth atlas. The lit pass uses the first cascade, nearest first, whose
square holds the lookup, and compares against it with a 3x3 grid of hardware
2x2 comparisons.

- **Softness**: a sun with an angular diameter casts percentage-closer soft
  shadows. Each lookup searches its cascade for nearer depths, as far
  around the point as the sun's cone spreads on its way up to the cascade's
  near plane, then spreads 32 comparisons over a disc as wide as the sun
  makes the penumbra at those blockers' average distance. Shadows stay sharp
  where they meet their caster and blur with distance from it; the real sun
  is about 0.5 degrees. Each pixel turns its disc differently, which leaves a
  fine grain in wide penumbrae. A penumbra under 1.5 texels, or no blocker
  found, uses the 3x3 grid.
- **Cascades**: the view depths the solids in view actually show, clipped to
  the camera frustum, are split at equal ratios, one cascade per factor of four
  up to four cascades. Anything nearer than the cascades reach shares the
  first. A view that spans less than a factor of four, such as the default
  near-overhead camera over a floor, uses one cascade and one square.
- **Fit**: each cascade's orthographic light view covers what the solids in
  view show within its depths, plus a 4-texel margin. A soft sun widens the
  margin by the widest penumbra the scene's depth allows, up to 128 texels,
  so casters past the view's edge still shade it; that costs up to 13% of
  the cascade's resolution. Its depth range also takes in opaque solids the
  camera culls, which still cast into the view when they reach the covered
  area. Each square's size steps by quarter octaves and its origin snaps to
  whole texels, so edges hold still while the camera pans, apart from a
  resolution step when the covered area crosses a size.
- **Bias**: each lookup lifts the surface point along its geometric normal by
  half a texel of its cascade facing the light, up to 2.5 texels edge on.
  Contact shadows start that far from the caster. Soft comparisons also
  follow the surface's plane, so wide discs don't shade the surface itself.
- **Memory**: the atlas grows to fit the most cascades a view has needed: one
  square, two side by side, or a 2x2 grid. Depth ranges with nothing in view
  take no square. It stores 32-bit float depth where the device can, then
  24-bit, then 16-bit, so four cascades take at most 64 MB. A soft sun adds
  a half-resolution map of each 2x2 block's nearest depth in half floats,
  at most 8 MB.
- **Not covered**: transparent surfaces, particles and trails cast nothing.
  Cascade edges change resolution without a blend.
  A penumbra's radius stops at 128 texels of its cascade, and a caster
  thinner than the disc's tap spacing shades its penumbra unevenly.

### Point and spot light shadows

A Point Light or Spot Light with `shadows` renders opaque solids' depth from
the light into 512x512 faces of one depth atlas
(`render/scene3d_light_shadow.c`), one row per light: six faces for a point
light, and one along its cone for a spot light. At most four point and spot
lights cast shadows in a view: the ones that rank highest among those with
`shadows`; the rest light without them. Ranks follow the camera, so which
lights cast can change as it moves.

- **Faces**: each of a point light's faces looks along one axis, +x, -x, +y,
  -y, +z or -z, across a little more than 90 degrees, so lookups near its edge
  stay on it. A spot light's face looks along its axis across a little more
  than its outer angle, so a wider cone spreads the same texels thinner and
  casts coarser shadows. Near a wide cone's edge, lookups that the bias below
  lifts past the face read its edge. The lit pass reads the face that holds
  the direction from the light to the surface point, comparing a 3x3 grid of
  hardware 2x2 comparisons like a sun without angular size.
- **Casters**: a solid casts for a light when its bounds reach within the
  light's radius, into each face whose view they reach, including solids the
  camera culls. A solid whose box holds the light casts nothing for it, so a
  lamp's own bulb leaves it lit. Solids nearer the light than 1% of its radius
  cast nothing.
- **Bias**: each lookup lifts the surface point along its geometric normal by
  half a texel of its face at the point's distance, up to 2.5 texels for a
  surface edge on to the light or turned from the face's axis, since a face's
  depth runs along its axis rather than toward the light.
- **Memory**: the atlas grows to the most shadowed lights a view has needed,
  3072 texels wide and 512 tall per light, so four lights take at most 24 MB
  at 32-bit depth. A spot light uses one face of its row.
- **Not covered**: point and spot light shadows have hard edges, since neither
  light has a size.

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
  scene time is paused; zero adapts every frame. The first frame, any jump
  back in scene time, and the frame after `app_scene_renderer_reset` (Studio
  resets on scene load, restart and stop) snap to the metered value.

## Sky

The Environment's `sky` names an equirectangular Radiance `.hdr` image under
`content/`, such as `content/skies/field.hdr` (`render/scene3d_sky.c`). It is
drawn on the far plane, so it fills every pixel no surface covers, and an empty
`sky` draws none.

- **Files**: the image must be twice as wide as it is tall and 2 to 4096 texels
  wide, in the `32-bit_rle_rgbe` format with rows top to bottom (`-Y +X`), flat
  or run-length encoded, as Poly Haven and most HDR tools write it. Radiance's
  original run-length encoding, which no current tool writes, is rejected. Row 0 looks
  straight up; the middle column looks along -Z, the column three quarters
  across along +X, and the left and right edges along +Z.
- **Rotation**: `sky_rotation` turns the sky about the up axis, in degrees
  counterclockwise seen from above, so 90 brings what lay along +X to -Z. It
  must be finite.
- **Exposure**: `sky_exposure` scales the sky by two to its power, from -20 to
  20 stops. The scene's exposure, auto exposure and tone map then treat it like
  any other light.
- **Storage**: preparation decodes the file once and the prepared project
  carries its RGBE texels, 4 bytes each, so a 4096-wide sky takes 32 MB of the
  player's 64 MB resource budget. The renderer resamples it bilinearly on the
  CPU into a half-float cube whose faces are the largest power of two up to a
  quarter of its width; values past 65504 saturate. It rebuilds the cube only
  when the sky's texels change, so preparing the same file again costs no
  rebuild, but the frame that first shows a 4096-wide sky pays for six
  1024-texel faces and for integrating its light over all of its texels.
- **Order**: the sky draws after opaque surfaces and before trails and
  translucent surfaces, which blend over it.
- **Import**: the `import_sky` controller operation checks that a file decodes
  as a sky, then copies it into the project's content, by default to
  `content/skies/` with the file's own name. It never replaces an existing file.

  ```sh
  printf '%s\n' '{"contract":1,"operation":"import_sky","source":"/downloads/meadow_2k.hdr"}' | \
    chibi_ctl --project-root "$PWD"
  ```

  The response's `path` is the key to put in `sky`; `destination` picks another
  `content/... .hdr` path.
- **Light**: a sky lights every surface in place of the hemisphere ambient,
  turned by `sky_rotation` and scaled by `sky_exposure` like the sky itself.
  Diffuse light comes from nine spherical-harmonic irradiance terms
  ([Ramamoorthi and Hanrahan](https://graphics.stanford.edu/papers/envmap/envmap.pdf)),
  integrated on the CPU over every texel. Reflections read a 128-texel
  half-float cube with six mips, prefiltered on the GPU with the GGX lobe for
  roughness 0 at the first mip to 1 at the last, and scale it by a 32x32
  split-sum table
  ([Karis](https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf)).
  Both are redone whenever the cube is.
- **Not covered**: nothing blocks sky light, so a surface inside a closed box
  is lit as if it stood in the open.

## Meshes

A `GameMeshAsset` source names a binary `.mesh` file in the content folder,
such as `content/meshes/Camera.mesh`. The file holds indexed triangles. Each
vertex has a position, normal, texture coordinate and tangent. The tangent's w
is 1 or -1, and the bitangent is cross(normal, tangent) times w. Meshes baked
from models also store each vertex's part position and object-to-part frame;
other meshes read back with their own position and an identity frame. Material
groups name runs of whole triangles in the index list and cover all of it.

The file is the engine's prepared mesh payload
(`src/runtime/geometry/mesh_prepared.c`, version 3), so one reader validates
authored files and player artifacts. Changing that format therefore needs a
project migration step that rewrites projects' `.mesh` files. A file is at
most 64 MiB, with at most 1,000,000 vertices, indices and groups together. The
reader refuses non-finite or out-of-bounds values, zero normals or tangents,
indices outside the vertex list, triangles below
`RENDER_MESH_MIN_TRIANGLE_AREA`, and groups that split a triangle or skip
indices, and names the first vertex, triangle or group at fault. The model
baker writes this format, with tangents generated from its box UVs by
`render_mesh_asset_tangents`; each triangle counts by its area, so slivers
don't steer their corners. A bake makes at most `MODEL_BAKE_TRIANGLE_MAX`
(166,496) triangles, the most a file holds when every triangle owns its three
vertices. Runtime C code reads a loaded mesh's vertices and indices through
`render_mesh_asset_load`. The JSON mesh format (`.mesh.json`) is retired:
export the model again to get a `.mesh` file.

Imported meshes draw through the planar material renderer
(`GameMeshSurface2D`); the lit 3D path does not draw them yet.

## Optional material inputs

Rhythm, Surface Influence, trails and particles feed materials only when a scene
has them. A scene without a valid rhythm clock draws with the beat at rest.

## Tests

`make agent-gpu-test AGENT_SELECTOR=--scene3d` renders a box or a floor in a
hidden 64x64 app. It checks cameras looking along their Transform3D, field of
view and clip distances, the lenses a camera rejects, ground rays at the widest
clip range, panning dropping a lens, fog concealing rays that miss the ground,
and ground rays and fog from an eye far from the origin. It checks ambient, the
directional light, the one-light limit
(hidden lights left out), drawing without a rhythm clock, both tone maps, auto
exposure's geometric mean, background rejection, easing (including slow easing
finer than a half-float step), snapping and settings checks, and sun shadows: a
box's shadow, no self-shadowing on lit faces, a solid the camera culls casting
into the view (collected across workers), the depth pass leaving earlier queued
draws alone, four cascades across a deep view each keeping a small shadow
sharp, two cascades side by side when the depths between hold nothing, one
cascade for a floor that reaches far past a shallow view, and a sun's size
softening a shadow's edge far from its caster but not at its foot, in every
cascade, and from a caster just outside the view. Point light shadows are
checked across a grid of floor against the box between it and each lamp, for
one lamp in four spots whose shadows cross between faces, and for two lamps
outranked by a brighter light that casts nothing; five lamps that cast give
shadows to the four ranked highest; a sphere lit from between three faces
keeps its light; a long wall casts for a lamp near one end; a slab the camera
culls casts too; and a sphere drawn into the same face as the box and the floor
stays in that face. A spot light matches a point light inside its inner
angle, matches a point light dimmed by the square of the fade partway across
its edge, and leaves the floor dark past its outer angle. Its shadows are
checked across the same grid, from a slanted cone and from one pointing
straight down, and beside a point light's in the same atlas. Every accepted
outer angle, from 2 degrees with a hard edge to 160, lights the floor below at
full strength, and angles out of range are rejected. Near the edge of a
hard-edged cone 160 degrees across, lookups stay on the spot light's face
rather than reading the shadows of the light before it. Looking along each
axis, unturned and turned a quarter, every corner of the view shows the sky
in the direction it looks; a black box hides the sky where it stands and
leaves it showing around it, and a translucent box blends over it; a quarter
turn of rotation brings +X ahead; one stop of exposure matches texels twice as
bright, both in the sky and in the light it casts; and missing skies, exposure
out of range and rotation that isn't finite are rejected. Under an octant sky,
unturned and turned a quarter, each face of a white box takes each channel
from the half of the sky it faces, the hemisphere ambient gives way, and a
mirror box shows the octant each view ray reflects into; a rough box gathers
the neighboring octants. A sky brightening toward an axis lights the faces
along it more than those across it, unturned and turned an eighth, which
reaches every irradiance term but the xy and yz ones; render-resources checks
those on the CPU. Unlit color ramps show their color whether their stops start
the program's ramp table or lie past the first 4 KB of it, and a stop color
edit shows without a recompile. It is a GPU check that runs the main build's
`test/tools/test_tools` after the agent's device preflight, so build it first
with `make build BUILD_TARGET=test_tools`.
llvmpipe leaks 112 bytes from its own worker threads, so LeakSanitizer hosts on
llvmpipe need `ASAN_OPTIONS=detect_leaks=0`. Debug ASan+UBSan on Linux with
Vulkan llvmpipe passed 42 cases and 9218 assertions in 7.4 seconds on 2026-10-09.

`make agent-gpu-test AGENT_SELECTOR=--scene3d-gles` runs the same cases on GLES,
the backend of the web player's WebGL 2 fallback. GLES needs a display rather
than SDL's offscreen driver, so the agent runs it under `xvfb-run`, and it needs
a Linux host with Xvfb. Without `xvfb-run` it reports `blocked`. Debug ASan+UBSan with Mesa llvmpipe GLES 3.2 passed 42
cases and 9218 assertions in 6.0 seconds on 2026-10-09.

`python3 tools/test_policy.py tool render-resources` checks on the CPU that the
sky's cube faces follow the GPU's face selection table and hold exact half
floats; that the irradiance terms give the exact cosine integral of an even
sky, an octant sky and skies quadratic along three axes; that the split-sum
table matches a fine numerical integral of the GGX lobe; and that skies
publish, and are charged, with a prepared generation, and
`python3 tools/test_policy.py tool sky-files` decodes flat and run-length
files, rejects malformed ones, prepares skies from mounted content and imports
them without replacing a file. Both need the in-tree build above.
`python3 tools/test_policy.py tool mesh-files` checks `.mesh` tangents,
preparation and refusals the same way.

`test/fixtures/scene3d` is the manual preview scene and enables every feature
above.
