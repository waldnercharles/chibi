# 3D rendering

A scene with `presentation "3d"` draws through `render/scene3d.c`. Entities with
`Transform` and `Geometry3D` become lit solids; gameplay x/y maps to world
x/z and `elevation` is the center height. The renderer writes scene-linear RGBA16F
into `render/scene3d_post.c`, which applies bloom, exposure and the tone map.

An entity with `Transform3D` and `Geometry3D` becomes a solid too. Its
global transform, through any ChildOf parents, places the solid's center, turns
it and scales it, and its `elevation` is ignored. An entity with both
transforms draws at its Transform3D. Its Visual Transform turns and scales the
solid within that frame, and a body's drawn pose is interpolated between
physics steps as a Transform's is. A transform that mirrors a solid draws it
mirrored with its faces lit from outside, judged by its axes' directions so
that a tiny mirror still turns them. One that flattens it onto a plane, a line
or a point hides it: a zero scale in its Visual Transform or in a Transform3D
placing it, however the turns between them round, or axes float cannot keep
apart at all, such as one a tiny Geometry3D dimension underflows to zero. A
tiny, thin or sheared frame that keeps some volume stays. Picks
and bounds follow the same placement, a model surface's included, and a Render
State's fog culling reads the ground under its drawn pose. Point and spot
lights, particles, trails and Surface Influences still place from
`Transform`, and SDF and Mesh Surface 2D surfaces belong to the 2D
renderer, which ignores Transform3D.

## Camera

A `Camera` with a `Transform3D` looks along its global -Z with +Y up, so
its local +X points right in the image. It looks from its drawn pose, so a
camera riding a body moves between physics steps as the body does. Its `offset` moves the eye across the
ground (gameplay y is world z), as it moves an overhead camera's target. One
with only a `Transform` frames its span from 10 degrees off overhead. `fov`
is the vertical field of view in radians, from 0.00001 degrees (Unity's
narrowest) to below 180, and zero picks 35 degrees. `near_clip` and `far_clip`
are set together, with far at most a million times near, or both left at zero,
which scales them with the span's height: far is at least 200, and near stays
in front of the framed target. Wider ranges round the far plane toward infinity
in float depth, which breaks picking and fog. The span's aspect shapes the image
either way. Panning or zooming the
viewport returns it to the overhead framing. Fog conceals view rays that never
reach the ground ahead, such as those above the horizon, as it does ground past
its grid. Picking, ground queries and fog unproject view rays about the eye and
start them at the eye the camera keeps, so they stay precise however far the
eye is from the origin.

## Lighting

- **Directional Light** (`DirectionalLight`): color, intensity and the world
  direction the light travels. A scene shows at most one; a second visible one is
  an error, and hidden ones are left out. Fog never culls it. With none, only
  ambient, point and spot lights shade the scene. `shadows` lets opaque solids
  block it, and `angular_diameter`, the sun's apparent size from 0 to 20
  degrees, softens their shadows (see Shadows).
- **Point Light** (`PointLight`): up to 32 per view, ranked by estimated
  influence on the camera. `shadows` lets opaque solids within its radius
  block it (see Point and spot light shadows).
- **Spot Light** (`SpotLight`): a point light that shines along
  `direction` in a cone. It has full strength out to `inner_angle` across and
  fades to nothing at `outer_angle`, falling off with the square of how far a
  point lies into that edge. The outer angle can be 2 to 160 degrees, and the
  inner angle 0 up to the outer; angles out of range are an error, even on a
  hidden light. Spot and point lights share the 32-light ranking, and a spot
  light without a direction lights nothing. `shadows` works as it does for a
  point light.
- **Environment** (`Environment`): exposure, bloom, a sky (see Sky), a
  hemisphere ambient that blends `ambient_ground` below to `ambient_sky` above,
  scaled by `ambient_intensity`, ambient occlusion (see Ambient occlusion) and
  global illumination (see Global illumination). A sky's light replaces the
  hemisphere ambient. Without an Environment the scene uses
  `environment_defaults()`.

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
- **Masked casters**: shadow passes draw depth alone, except for Mask
  materials, which run a shadow build of their program that discards the
  pixels below their cutoff, so cut-out leaves cast cut-out shadows. A masked
  solid outside the view keeps a material row for this. Each pass sees its
  View Direction from its light: toward the sun, or toward a point or spot
  light's position.
- **Not covered**: blended surfaces, particles and trails cast nothing.
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
  cast nothing. Masked solids cast their cut-out, as for the sun.
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

## Ambient occlusion

The Environment's `occlusion` darkens ambient and sky light where nearby
surfaces hide the sky from each other: creases, corners and the ground under
objects. It leaves the sun, point and spot lights alone, and where a
material's own Occlusion is lower, that holds. `occlusion_radius` is how far,
in world units, a surface looks for what occludes it; it must be positive and
finite while occlusion is on. A radius reaching across more than a quarter of
the frame's height, close to the camera, searches only that far.

`render/scene3d_occlusion.c` draws the opaque solids' depth from the camera,
culled as the lit pass culls them, with masked materials keeping only the
pixels they keep. Ground-truth ambient occlusion (GTAO) then searches that
depth along screen-space slices through each pixel's view direction for the
highest horizon on either side within the radius, and integrates the
cosine-weighted sky left open between them. The slices' turn and the steps'
offsets change over each 4x4 block of texels, and a blur averages each block,
weighting each texel by how close its point lies to the pixel's surface plane
so that occlusion stays on its own surface across edges. Translucent surfaces
neither occlude nor take the occlusion of what lies behind them, and pixels
past the frame's edge occlude nothing. The quality sets the slices, steps and
resolution:

| Quality | Slices | Steps per side | Resolution |
| --- | --- | --- | --- |
| Off | | | none; no depth pass |
| Low | 2 | 4 | half |
| Medium (default) | 3 | 6 | half |
| High | 4 | 8 | full |

Environments saved before this setting load with it Off. Studio adds an
Environment with Medium and a radius of 1, and a 3D scene without an
Environment draws with those defaults. Hosts that draw through
`render_scene3d_draw` call `render_scene3d_set_size` with the canvas's pixel
size first; `app_scene_renderer_draw3d` does. Samples snap to pixel centers,
which can lie just off their slice, so flat surfaces can lose a level or two
of 8-bit output.

Reference: Jimenez et al., [Practical Real-Time Strategies for Accurate
Indirect Occlusion](https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf),
and Intel's [XeGTAO](https://github.com/GameTechDev/XeGTAO).

## Global illumination

The Environment's `global_illumination` bounces light between 3D surfaces:
light from the sun, point and spot lights, emissive surfaces and the sky
reaches surfaces it doesn't shine on directly, off the walls, floors and
objects it lit first, in the colors of what it bounced off. Solids outside
the view bounce light too. It replaces the hemisphere ambient and the sky's
diffuse light on every 3D surface; the sky still gives reflections, and
ambient occlusion and a material's Occlusion still dim it. Off, the default,
lights surfaces with the ambient colors or the sky alone, and Environments
saved before this setting load with it Off.

`render/scene3d_gi.c` follows dynamic diffuse global illumination (DDGI). A
grid of probes covers the opaque solids' bounds, with an eighth of their
extent spare on each side. The grid stays while it covers the solids and
isn't more than twice as coarse as a grid fit to them again. Each frame every
probe traces its rays, evenly spread over the sphere and turned at random
each frame, through a simplified copy of the scene: each opaque built-in
shape as itself, a Box Frame as its twelve beams, and an imported mesh as the
box around it. Rays find the solids through a bounding volume hierarchy
built each frame, so tracing costs grow with the log of the solids. A ray
that hits a solid brings back the solid's emission plus its color times the
light there: the sun and lights with the lit pass's falloffs, blocked by
solids for the lights whose shadows the lit pass draws, but not by a solid
whose box holds the light, such as its bulb or a lantern's Box Frame, as in
the lit pass, and the probes' own light from the last
frame, which bounces light on again. A ray that hits nothing brings back the
sky's light from that way or, without a sky, the sky ambient color above the
horizon and the ground color below, so open ground takes the same light as
without global illumination. A mesh's box stands in for the mesh only from
outside: rays starting inside it pass through it, since the mesh within may
be open, like a building or a valley.

Each probe keeps an octahedral map of the light arriving from each direction
(8x8 texels with their border) and of the mean distance and squared distance
its rays reached (16x16). Its first frames average evenly, then it keeps 95%
of its earlier values each frame. That averages the rays' noise over about
twenty frames, so light from a fast moving solid or light trails behind it.
When the grid is fit again, each probe starts from the light the old probes
held where it now stands, and its distances start over. Turning global
illumination off releases the probes and forgets their light. The lit pass
blends the eight probes around each point by nearness, by whether they lie
in front of the surface, and by the chance, from the distances they saw,
that they see the point, which keeps light from leaking through walls. A
probe holds no light from the frame it lies inside a solid, and where none of
the eight holds any,
the surface takes the light it would without global illumination. The
quality sets the most probes and the rays each traces:

| Quality | Most probes | Rays per probe |
| --- | --- | --- |
| Off (default) | none; no passes | |
| Low | 512 | 32 |
| Medium | 2048 | 64 |
| High | 4096 | 128 |

Rays take a solid's color and emission from its Material component; a shader
graph's computed base color and emission don't reach them. Translucent and
masked solids and Triangle Outlines let rays through. Global illumination
traces at most 4096 solids, a Box Frame counting as twelve, and more is an
error. The grid fits every opaque solid, so one far from the rest, such as a
projectile flying off, spreads the probes thinner while it lasts. Solids
spanning more than about 1e18 units pass what the shaders' float math holds, so
such a scene takes its light without global illumination.

The probes' light takes one of the 16 samplers SDL lets a fragment shader
bind on every backend, after the scene's six maps, so it works on Vulkan,
Metal, Direct3D 12, GLES 3 and WebGPU alike. A lit program's images, its
parameters, those maps and its ramp table can fill all 16, so with global
illumination on, a 3D surface material reads at most seven images rather than
eight, and a material that reads eight is an error naming it. Only the lit
programs drawn while global illumination is on read the probes, so scenes
without it compile none of its code, and turning it on compiles their
materials' programs again.

To compare it, use the Room scene of `test/fixtures/scene3d`: an
open-topped room with red and green walls, a block and an emissive lamp,
under the field sky with Medium occlusion and Medium global illumination.
Setting its Environment's `global_illumination` to `"Off"` gives the baseline
of direct light, the sky's light and ambient occlusion.

The probes start dark and settle over about twenty frames, so compare after
warmup frames. On hardware opted into profiling, a performance run's
`pass_timing` charges each frame's 3D work to "3D setup" (the sky's
filtering and the scene's uploads), "3D shadow maps", "3D ambient
occlusion", "3D screen light" (when on), "3D global illumination", "3D
surfaces" and "3D post-processing".

Reference: Majercik et al., [Dynamic Diffuse Global Illumination with
Ray-Traced Irradiance Fields](https://jcgt.org/published/0008/02/01/), and
Ramamoorthi and Hanrahan, [An Efficient Representation for Irradiance
Environment Maps](https://graphics.stanford.edu/papers/envmap/envmap.pdf),
whose cosine factors per harmonic band turn the sky's irradiance back into
the light it gives off.

## Screen light (2026-10-17)

`Environment.screen_light` lets glowing particles and surfaces light what is
around them, which the probes above miss: particles cast nothing and small
emitters fall between probes. It traces radiance cascades
(`render/scene3d_cascades.[ch]`, `shaders/scene3d_cascades*`), is opt-in, and
needs `occlusion` on because its probes stand on the occlusion's depth.
`World` is the mode to use; `Screen` remains for comparison.

- **Probes:** every 8 pixels on the visible surfaces, each with a 4 x 4
  octahedral map of directions. Each of five cascades doubles the spacing and
  the map side and traces rays four times as long: [0, 1], [1, 5], [5, 21],
  [21, 85] and [85, 341] first-cascade probe spacings measured on the probe's
  surface, so the shape holds at any scale. Cascades trace top-down and merge
  with the one above where a ray ends unblocked, blending its four probes by
  depth.
- **Energy:** each frame turns every ray to another point of its direction's
  cell and shifts its samples, and the gather weights each direction by the
  solid angle its texel covers, (2 / side)^2 over the cube of its octahedral
  point's distance from the center. Each probe's irradiance is blurred over
  its 3 x 3 neighbors on the same surface and accumulated, each frame counting
  a tenth. Accumulation starts over where the camera moves or a probe's depth
  changes, and drops values that are not finite or are negative, which
  otherwise feed back through the last frame's color for good.
- **World tracing** marches the terrain volume's carved density on its coarse
  lattice and a grid of the frame's particles, each spread as a sphere whose
  chord shows its emission. The same grid's alpha marks cells whose centers
  lie in an opaque object's box (ships, debris, any non-terrain solid), which
  block rays past the first cell. Light reaches surfaces from emitters off the
  screen and stops at rock and objects the camera cannot see; what a ray meets
  gives back the last frame's color where the camera sees it. Without a
  terrain volume it traces the screen. A carve change relays only the lattice
  points it can flip (`terrain_volume_changed_lattice`).
- **Screen tracing** marches the depth with a thickness of four spacings and
  takes the last frame's color where a ray passes just behind a surface.
  Nothing off the screen counts.
- **Packing:** the lit pass's 16 samplers are taken, so the result shares the
  occlusion's binding: screen light over pi in rgb, ambient occlusion in alpha.
  Translucent draws read none.

Bake-off on Voidweaver's basalt bay (`Basalt3DLightLab*`), 1080p, Apple M4
Pro, warmed medians: off 27.2 ms; Screen 28.7 ms (its pass 3.95 ms); World
28.9 ms (3.7 ms). With the round-05 Cradle's small blue jets, World brightened
the rock and hull near them by 6% and Screen by 2%; with larger glowing
particles World brightened them by 50% and Screen by 2%, because a few-pixel
emitter rarely meets a ray marched over the screen. Hardened World measures
27.1 ms against 25.6 ms off (pass 3.9 ms). In the cut benchmark, a hit's
lattice relay takes 0.16 ms against 3.6 ms for the whole lattice.

Known limits:

- The rock atlas still uploads whole (2.6 MB in the bay) after a cut; Cute
  has no region update.
- Objects block at the grid's 16-unit cells and only past the first cell.
- Only particles emit into the world grid; emissive surfaces count only where
  the camera sees them. Light bounces through the last frame's color, so it
  lags a frame.
- A moving camera restarts the accumulation each frame; a follow camera will
  need probe reprojection.

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
- **Sky materials**: `sky_material` names a surface material to draw as the sky
  instead of an image; an Environment sets one or the other. The material is
  evaluated along every direction looked along: World Position, Object
  Position and Part Position are that unit direction, Normal and View
  Direction point back toward the camera, UV holds its equirectangular
  coordinates and Time is the scene's. The sky shows its Emission, plus Base
  Color when Unlit. Rotation and exposure apply as to an image. For light,
  the renderer renders it at time zero into a 256-texel cube for reflections
  and global illumination, and evaluates its graph on the CPU over a 64 x 32
  grid for diffuse light, rebuilding both when the material, its defaults or
  the shader catalog change. It cannot read images; use an image sky for
  photographs. The **Star Field** node draws point stars that stay about a
  pixel wide at any field of view; see [shader graphs](shader-graphs.md).
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

A `MeshAsset` source names a binary `.mesh` file in the content folder,
such as `content/meshes/Camera.mesh`. The file holds indexed triangles. Each
vertex has a position, normal, texture coordinate and tangent. The tangent's w
is 1 or -1, and the bitangent is cross(normal, tangent) times w; normal maps
take it as the direction of +v, down the image. Meshes baked
from models also store each vertex's part position and object-to-part frame;
other meshes read back with their own position and an identity frame. Material
groups name runs of whole triangles in the index list and cover all of it.

The file is the engine's prepared mesh payload
(`src/runtime/geometry/mesh_prepared.c`, version 3), so one reader validates
authored files and player artifacts. Changing that format therefore means
rewriting projects' `.mesh` files and bumping the project version. A file is at
most 64 MiB, with at most 1,000,000 vertices, indices and groups together. The
reader refuses non-finite or out-of-bounds values, zero normals or tangents,
indices outside the vertex list, triangles below
`RENDER_MESH_MIN_TRIANGLE_AREA`, and groups that split a triangle or skip
indices, and names the first vertex, triangle or group at fault. The model
baker writes this format, with tangents generated from its box UVs by
`render_mesh_asset_tangents`; each triangle counts by its area, so slivers
don't steer their corners, and the sums run in double, so a mesh gets the same
tangents at any size within float's range. A bake makes at most `MODEL_BAKE_TRIANGLE_MAX`
(166,496) triangles, the most a file holds when every triangle owns its three
vertices. Runtime C code reads a loaded mesh's vertices and indices through
`render_mesh_asset_load`. The JSON mesh format (`.mesh.json`) is retired:
export the model again to get a `.mesh` file.

An entity with a `MeshAsset` draws its mesh in the lit 3D pass and, with
`MeshSurface2D`, in the planar material renderer. Its `Geometry3D`
dimensions scale the mesh's own coordinates, so dimensions of 1 keep them as
authored, and its transforms place it as they place a shape. It cannot also
have Mesh Parts. Each material group draws its run of the index list with the
group's surface material, or with the entity's Shader material when the group
names none; a name that is no surface material fails the frame. Surface graphs
read the vertex texture coordinates, or with source UV bounds the box
projection the planar pass uses, and the vertex part positions and frames.

In the planar `MeshSurface2D` pass, meshes and SDF shapes use the entity's
`Material` for HRC absorption even when a surface graph supplies their visible
color. Each absorption channel is `Material.scene.a * Material.absorption *
(1 - Material.filter.rgb)`, scaled by the graph's `Surface.Opacity`. Absorber
coverage is also scaled by that opacity. Set absorption to zero to keep a
visible surface from absorbing planar light; set both scene alpha and
absorption above zero for an absorber. Omitted `Material` fields are zero, and
surfaces without a `Material` do not absorb. Filter alpha is unused; HRC reduces
these RGB coefficients to scalar optical depth, so this does not provide colored
transmission. Mesh groups and model-part graphs share the entity's coefficients;
use separate entities when parts need different absorption.

This planar surface path draws any positive graph opacity as visually opaque;
zero opacity cuts the surface out. Zero absorption does not disable geometric
depth testing or HRC Depth-mode height occlusion. The lit 3D pass has its own
transparency and lighting contracts.

Meshes cast and receive shadows as shapes do, including from outside the view;
picking meets their triangles, and bounds hold their extents. A pick tests
only the triangles in boxes its ray crosses, from a tree of boxes built on a
mesh's first pick and freed whenever the mesh catalog changes. The ray is
cut to the mesh's box before it goes into the mesh's coordinates, with half
the cut span as its direction, and the box and triangle tests run in double,
so a mesh near or past float's range that its Geometry3D scales down to view
picks as it draws. Translucent
meshes sort back to front by the center of their bounds, so a mesh whose
vertices lie away from its origin sorts where its triangles are. Cute sorts
translucent draws by the translation of their transform, so a mesh's GPU copy
holds its positions about that center and its draws translate to it. A mesh goes to
the GPU when it first draws and is freed once a reload replaces it or the
catalog drops it; `render_scene3d_last_work` counts the meshes held. The copies
stay ordered by mesh identity, so each draw finds its own by bisection.

Normal maps and every node that reads the surface UV's gradients follow a
mesh's interpolated vertex tangent and bitangent, the frame its maps were baked
against, so they shade smoothly across triangles. The gradients keep their
screen-space lengths, so unequal UV scales still weigh each tangent component.
Tangents turn with the mesh's transform, and each vertex keeps its tangent's
direction even along an axis scaled far shorter than another. Their w picks
the bitangent's side, so mirrored UVs work. A mesh whose UVs are box-projected
from source bounds keeps the screen-space frame, which its tangents don't
describe, as shapes do.

## Importing glTF models

The glTF importer, archived with the game projects in
`archive/src/assets/model_import.c`, copies a glTF 2.0 model, a `.glb` or a
`.gltf` beside its buffers and images, into the project's content. The model's
name defaults to the file's name without its extension. It must be an asset
name, a letter and then letters, digits, `-` and `_`, at most 63 bytes, and no
prototype may already declare it. An import writes:

- `content/meshes/<name>.mesh`: the triangles of every mesh node, placed by
  their node transforms, with one material group per glTF material in glTF
  order and a last one for primitives that name none.
- `content/materials/<name>_<material>.kdl`: a Standard material for each glTF
  material the mesh's triangles use, and `<name>_Default` for primitives with
  none.
  Characters an asset name refuses become `_`, and a name that matches an
  earlier one, ignoring case, gets `_2`, `_3` and so on. `<name>` keeps at
  most its first 47 bytes here, so the material's own name always fits.
- `content/textures/<name>/<image>.png` or `.jpg`: each image a material uses,
  copied byte for byte and named for the image, else its file, else
  `Image<n>`.
- `content/prototypes/<name>.kdl`: a prototype with Transform3D, Geometry3D of
  1 by 1 by 1 and a Mesh Asset naming the mesh.

It never replaces a file and removes everything it wrote when a step fails.
The node tree flattens into one mesh because Studio cannot open prototypes
with children yet, and nothing reads the nodes until skinning and animation do.

- **Frames**: UVs copy as they are, since glTF and Chibi both put (0, 0) at an
  image's top left. Tangents take the engine's frame, whose bitangent points
  down the image, toward +v: the import generates them from the normal
  texture's transformed UVs when that texture is present,
  and negates the w of a primitive's authored tangents, since glTF's
  bitangent points up the image. A primitive without normals gets each
  triangle's flat normal. A node that mirrors, by the signs of its and its
  parents' scales, flips its tangents' w and its triangles' winding. A node
  whose scale collapses its surfaces onto a line or a point is skipped. One
  that flattens them onto a plane keeps the triangles with area there, their
  normals turned to the plane on the side they faced. Triangles that a
  transform leaves with no area are left out with a warning. Vertices no kept
  triangle uses, or no index names, are left out too, so they neither widen
  the mesh's bounds nor count toward its limits. Normals and tangents go
  through the node frames in double, so a frame near float's range keeps
  their directions.
- **Materials**: each factor is a Shader Parameter at a fixed slot, so
  entities can override it: Base Color (0), Opacity (1, unless Opaque),
  Metallic (2), Roughness (3), Emission (4, times
  `KHR_materials_emissive_strength`), Normal Scale (5, with a normal image)
  and Occlusion Strength (6, with an occlusion image). Each glTF texture adds
  one Image Texture node that scales its factor. Base color and emission
  images sample as sRGB color and the others as linear data. Metallic reads
  the blue channel of the metallic-roughness image and roughness its green;
  occlusion is `mix(1, red, strength)`; normal images use the OpenGL
  tangent convention, whose green tilts up the image and whose unit frame
  keeps UV density from changing the normal direction. Alpha Mode and Alpha
  Cutoff carry over, and `KHR_materials_unlit` sets Unlit. An unlit material
  keeps only its base color and its image, since glTF makes its other properties fallbacks
  for viewers that light it. An image has one filter, its sampler's
  `magFilter`: `NEAREST` samples Nearest and anything else Bilinear, with a
  warning when its `minFilter` differs. Engine images have no mip levels, so
  a mipmapped `minFilter` reads the full image, as every image does, without
  a warning. A sampler that clamps both axes samples Clamp; every other wrap
  repeats, with a warning.
- **Texture transforms**: each of the five texture slots independently applies
  `KHR_texture_transform`: scale, counterclockwise rotation in radians about
  the UV origin, then offset. The material writes a Mapping node only for a
  nonidentity transform, converting radians to the node's degrees. Mesh UVs
  stay unchanged so one slot cannot move another slot's image. An extension's
  `texCoord` overrides its textureInfo value, but the effective set must be
  `TEXCOORD_0`. Zero and negative scales are valid. A supplied tangent frame
  stays authoritative even when the normal lookup is transformed; missing
  tangents use the transformed normal coordinates. This follows the
  [extension's transform order](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_texture_transform)
  and the [Khronos renderer's tangent handling](https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/main/source/Renderer/shaders/material_info.glsl).
- **Refused**: a required extension other than `KHR_materials_unlit`,
  `KHR_materials_emissive_strength` and `KHR_texture_transform`; malformed or
  nonfinite texture transforms, or ones that overflow on used UVs; a texture
  that reads a UV set other than `TEXCOORD_0`; an image that is not a PNG or
  JPEG of 1 to 2048 pixels a side;
  a material factor beyond float range; primitives that are not triangles; a
  buffer or image named by an absolute path or a scheme other than `data:`, or
  one that `..` or a symbolic link leads outside the model's folder; a model
  over 256 MiB together with the files it names; a mesh past the `.mesh`
  limits above, refused before the import builds the vertices of the
  primitive, or of a node's copy of it, that would pass them;
  a material name that another native graph in the project already declares,
  or a prototype name another prototype does, and a folder or file of
  declarations it cannot read or parse; materials that would bring the
  project's native graphs past the 1,024 it can prepare; and a content
  folder, or a folder in it the import writes to, that is a symbolic link.
- **Warned and left out**: vertex colors, morph targets, skins, animations,
  cameras, lights and other extensions the model uses. Double-sided
  materials draw single-sided.

## Occlusion and alpha modes

A material's Occlusion scales the ambient and sky light it receives, diffuse
and specular alike, and leaves the sun, point and spot lights alone. Its Alpha
Mode decides how it draws. A Blend material whose Opacity is wired or below
one is translucent: it draws after the solids, back to front, without writing
depth, and casts nothing. An Opaque material ignores Opacity. A Mask material
keeps a pixel whole where Opacity reaches the Alpha Cutoff and discards it
below; it draws with the solids, writes depth and casts shadows of the pixels
it keeps. Lowering folds both modes into the material's Opacity, so the planar
and 2D paths shade them unchanged, and the catalog keeps the mode to sort the
draw and to cut its shadows.

## Optional material inputs

Surface Influence, trails and particles feed materials only when a scene has
them. Materials can animate with shader Time and ordinary math. The engine no
longer supplies a rhythm clock or Beat Phase/Pulse shader inputs.

Project version 39 removes stored Rhythm and VisualMotion components and
replaces beat shader nodes with zero constants. Source that calls
`scene_rhythm()` or `trigger_visual_reaction()`, or reads the removed native
components, needs an authored decision before migration can finish. Remove
those uses or express the desired effect through ordinary behaviors that write
`VisualTransform` and `Geometry3D.elevation`. The canned spin and recoil system
is retired; visual transforms, simulation clocks, math and tasks remain.

## Tests

The renderer's GPU and CPU tests (`--scene3d`, `--scene3d-gles`, `--scene3d-gi`,
`--texture-transform`, `render-resources`, `sky-files`, `mesh-files` and
`model-import`) are archived with `src/render` and the player; see
`archive/README.md`, "Native player, renderer and UI host". Nothing runs them
until a Chibi renderer replaces it.
