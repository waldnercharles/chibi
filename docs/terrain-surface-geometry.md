# Destruction-linked surface geometry

## Scene3D rendering

`Camera.orthographic` defaults false. `scene3d_terrain` reuses revision-owned material meshes and
builds indirect proxies from occupied collision rectangles, capped at 4096.
Actual mesh triangles remain shadow casters when their bounds contain a light.
`IndirectOccluder.enabled=false` suppresses a concave mesh's enclosing GI box
without suppressing its rendering or direct shadows; it is not a replacement
for correct authored proxies. Terrain extrusion still approximates relief.

`scene3d_volume` adds optional homogeneous single scattering, opaque-depth
extinction and local/sun shadow visibility. Environment fog density defaults
zero, resolution is 0.25..1 and samples are 4..64. Conservative depth reduction
keeps reduced-resolution fog behind foreground silhouettes. The empty-visible-
geometry path still prepares offscreen shadow casters. Fog does not inject a
second copy of spotlight illumination into GI.

## Cutaway recess

A straight contour extrusion shows only camera-facing caps under a side-on
orthographic camera, so cave interiors read as a flat wall. `MaterialSurface3D`
now has an optional cutaway interior: `recess_width` and `recess_depth` give
empty cells within that width of solid a surface that eases from the contour
lip to full depth away from the camera. The camera-facing caps become the
section plane; the receding surface is what lights reveal. Zero width keeps
the previous vertical wall exactly.

Recess distance comes from published occupancy through an exact Euclidean
distance transform, not authored field values, so cuts and removed fragments
recede by their surviving neighbors. `recess_smoothing` measures it from the
morphological opening instead: solid thinner than twice that width, such as
lavacicles, still renders and collides but hangs in front of the recess rather
than dragging a ridge into it. The contour and walls come from the recess
dilation, so no wall is built at the collision contour. Terrain halos widen to
`recess_width + recess_smoothing + 2` cells (capped at 128); a mask whose halo
is narrower fails explicitly instead of producing seams. Detached fragments
drop the recess. Masks above 256 cells per side are rejected for recess.

Limits: the recess is a single height per gameplay point, so it cannot model
arches or overhangs in depth. Indirect proxies still extrude only occupied
rectangles; the recess surface casts direct shadows but does not occlude probe
GI. In HRC 2D views a recess draws over open cells without changing transport.
`--material-surface-mesh` covers cut recession, open wide cuts, chunk seams and
the halo requirement, and smoothing; `--scene3d-terrain` covers cache revision
on option changes with opening-preserving proxies.

## Scope and acceptance

The live material mask remains the source of truth before and after this change.
Cuts, collision, transport coverage and mesh boundaries consume the same
published revision. Generated mesh buffers are derived state, never a second
editable terrain or checkpoint payload. Game-specific geology, textures,
composition and lighting values remain authored.

## Chosen route

Use the existing HRC mesh-material path with an optional `MaterialSurface3D`
component. Generate a shallow slab with geometric relief and beveled cave
boundaries from occupancy and signed samples. Preserve the mask's existing
optical coverage and avoid drawing its old color over the mesh.

This is smaller than moving gameplay to a new 3D world, and preserves the
existing destruction and persistence work. The full scene3d renderer already
supports metallic PBR, shadowed spotlights and probe GI, but its GI currently
traces imported meshes as bounding boxes. Using that unchanged would seal cave
holes. HRC remains planar transport with depth-aware visible-surface shading;
this increment does not claim arbitrary 3D GI or hidden-layer visibility.

An explicit bounded spotlight pass supplies aimed diffuse/specular illumination
and tests visibility against the current mask/depth data. Its contribution is
direct-only. A diffuse-receiver contribution to HRC bounce requires a separate
energy-accounting check before being enabled. A dim lens may remain visible,
but must not duplicate the spotlight as a bright isotropic emitter.

`HRC2D.volume_density` enables homogeneous, isotropic single scattering for
these spotlights. It is extinction per world unit, defaults to zero and is
independent of the legacy `fog` fluence gain. Eight midpoint samples integrate
each camera ray after clipping it to the light sphere, forward cone and nearest
visible surface. Source and camera transmittance use Beer attenuation. Volume
rays check both endpoint cells; direct surface rays retain their self-shadow
exclusions. This remains a screen-space approximation: hidden geometry and
occluders outside the padded canvas are unknown. Neither scattering nor direct
spot shading enters the indirect bounce feedback.

## Changes and ownership

- `runtime/geometry/material_surface_mesh.[ch]`: pure CPU indexed geometry,
  stable material coordinates, finite unit normals, seam-aware sampling.
- `runtime/components.h` and authoring metadata: optional generic surface
  controls. Terrain chunks and fragments inherit them through existing bundles.
- `render/material_mesh.[ch]`: CPU mesh cache by incarnation, revision, options
  and material frame. Retire unseen entries; unchanged frames do no regeneration.
- `render/scene_renderer.c`, `mesh_surface.h`, `mesh_material.[ch]`,
  `material_raster.[ch]`: mesh submission, explicit dynamic GPU replacement and
  retirement, preserving terrain transport and preventing duplicate color.
- `render/spot_lights.[ch]`, `radiance.[ch]`, HRC shaders: reuse the authored
  SpotLight contract, collect transforms and perform bounded aimed lighting.
- SDK build/export lists and focused C tests: make the feature reusable outside
  the game and test both ordinary material specimens and the motivating cave.
- Voidweaver: real noncolliding rear mesh layers, surface styling, lamp tuning,
  matched ambient-on/off and breach comparisons. Preserve original sources.

## Lifecycle invariants

Build only from published material boundaries after damage and halo updates.
Mask incarnation prevents allocator-address reuse from reviving stale geometry.
Options and material-frame edits invalidate geometry without changing occupancy.
Replacement, reset and chunk retirement release both CPU and GPU buffers.
No new side walls at internal chunk boundaries. Missing neighbors are distinct
from known air. Stable source coordinates keep fragments and cuts aligned with
their material. Collision uses exact cell rectangles; mesh contour tolerance
must be bounded relative to cell size and stated in tests.

## Shared authored Height

`MaterialSurface3D.material_height` selects the bound surface material's authored
Height instead of the default seeded geometric relief. Height is signed world-unit
displacement from `elevation`; `relief` is not applied again. Existing materials
without this option retain the original geometry path. The retained `MaterialFrame`
maps each mask-local point into stable material coordinates, including after a
fragment detaches or rotates.

The pure CPU builder accepts a borrowed immutable `material_surface_height`
descriptor. Its callback samples that stable point and writes displacement; the
renderer owns material preparation and evaluation. Sampling uses a fixed reference
plane, `PartPosition = (material_x, 0, material_y)`, so Height cannot depend on its
own displaced result. The contour bevel and taper combine with that same authored
height, and physical mesh normals derive from the combined result. A shader's
default zero Normal therefore retains the generated geometric normal. The render
path must not add the already-consumed Height to the mesh a second time.

Every callback must succeed and return a finite displacement strictly between
`-depth` and `depth`. Otherwise the build fails and the previous output remains
unchanged. This bounds the top surface above the slab bottom and below
`elevation + depth`, including contour taper. Input is limited to 1,048,576 mask
cells; output is limited to 3,000,000 triangle corners. Spacing is at least the
source cell size. Contours keep their source density; current interior
tessellation also retains cell support, so spacing is not a full mesh-decimation
control. Generated surfaces remain open-backed and cannot represent arbitrary
enclosed volumes or multiple heights at one gameplay point.

This is a CPU material-sampling contract, not an arbitrary GPU readback or an
external material interchange guarantee. Preparation must reject unsupported or
unresolved inputs explicitly and include material dependency changes in geometry
cache invalidation. Shared coordinates and displacement avoid independent noise
sculpting, but do not establish bit-identical CPU/GPU shading or visual fidelity.

The generic `PlanarSurfaces` specimen now binds `SurfaceDestructible` to
`SurfaceRelief`, whose Height is `PartPosition.x * 0.05` and whose Normal remains
the generated mesh normal. The CPU selector has analytical plane/normal,
cross-chunk, carved-hole, depth-envelope and atomic failure cases.

## Verification

Use focused C selectors through the production supervisor. Check geometry
bounds, normals, holes, neighboring chunks, cut/reset revisions and unchanged
cache reuse. Use a second non-basalt specimen for the generic component. Check
forward/rear spotlight energy, intact/cut blockers and nonabsorbing rear walls
with bounded hidden GPU readbacks. Build products separately from tests.
The spotlight selector also checks zero-density compatibility, forward/rear
empty-space scattering, thin blockers, cut openings, front-surface clipping
and a source behind an opaque slab at the same projected pixel.
