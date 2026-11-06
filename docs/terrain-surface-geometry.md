# Destruction-linked surface geometry

## Scope and acceptance

Add real shallow mesh surfaces to material masks and aimed surface lighting to
HRC. Voidweaver's retained basalt image is the visual target. Keep it open during
iteration and compare actual native captures. A functioning mesh or a passing
physics check does not establish reference fidelity.

The live material mask remains the source of truth before and after this change.
Cuts, collision, transport coverage and mesh boundaries consume the same
published revision. Generated mesh buffers are derived state, never a second
editable terrain or checkpoint payload. Game-specific geology, textures,
composition and lighting values remain authored in Voidweaver.

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
cross-chunk, carved-hole, depth-envelope and atomic failure cases. Formatting,
validation and rerunning those cases for this shared-Height extension are pending
the coordinated root build. The earlier checked-candidate results below describe
the preceding relief path, not acceptance of this extension.

## Verification

Use focused C selectors under the existing 60-second supervisor. Check geometry
bounds, normals, holes, neighboring chunks, cut/reset revisions and unchanged
cache reuse. Use a second non-basalt specimen for the generic component. Check
forward/rear spotlight energy, intact/cut blockers and nonabsorbing rear walls
with bounded hidden GPU readbacks. Build products separately from tests.
The spotlight selector also checks zero-density compatibility, forward/rear
empty-space scattering, thin blockers, cut openings, front-surface clipping
and a source behind an opaque slab at the same projected pixel.

In Voidweaver verify actual ship collision/traversal/reset and capture matched
640x360 ambient-on/off/breached frames under the existing preview budget.
Retain the first pass for comparison. No full suite, Studio, browser/device or
performance acceptance is implied. The inherited ParitySlope formatter hold
remains until its owner repairs it; do not suppress it.

## Dependency and coordination

Isolated checkout: `.worktrees/voidweaver-rock-surface/chibi`, branch
`codex/voidweaver-rock-surface-20261016`. Dependency snapshot tree
`4346e824d18b769c5c74cb5c8e7bc42bee977caf` contains all tracked and untracked
environment-authoring changes, including the authored absorption correction.
The original candidate is unmodified. Coordinate integration with thread
`01a14208-7219-7ef3-9f03-86aa1b3ec729`; no commits, merges or publication are
requested in this pass.

## Checked candidate, 2026-10-16

The controller/compiler/player and focused test executable build successfully
with Debug, sanitizers off and warnings enabled. Increment C files use
clang-format 22. No source warnings were reported in the final build; existing
duplicate-library linker warnings remain.

The supervised selectors `--material-surface-mesh`, `--material-mesh-cache`
and `--hrc-spot` pass (0.154, 1.110 and 2.906 seconds respectively, excluding
their outer wrapper). The last selector executes 18 bounded 64x64 GPU cases.
The non-basalt PlanarSurfaces fixture formats and executes four CPU frames.
Voidweaver validates, compiles and has matched native Metal on/off/cut captures;
its collision/traversal/reset batch retains the expected 712-cell breach.
See the game's `docs/engine-surface-pass.md` for exact artifact identities,
images and remaining visual/performance limits. Full native/render suites are
quarantined, not passed; no Studio, browser or packaged SDK acceptance ran.

Initial failures were resolved: Metal does not accept the translated numeric
`mix` with a boolean vector; equivalent numeric boundary arithmetic compiles.
The cache test initially mistook half-float negative zero for absorption;
it now checks numeric zero and independently checks the CPU hole. Scattering
visibility now tests both endpoint slabs, unlike direct surface rays.

The current images remain far below the reference. Surface geometry and
destruction linkage are implemented, not photorealistic basalt acceptance.
