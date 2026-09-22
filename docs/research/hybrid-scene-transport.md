# Integrated optical transport reference

September 21, 2026. Worktree `.worktrees/gi-research`, based on `0f9de0ea`.
This integrated HRC renderer is the current development baseline. See
[scene renderer optimization](scene-renderer-optimization.md) for current work.
The [unified solver experiments](unified-directional-transport.md) have not
justified replacing HRC.

## Implemented

`render/optics.c` traces bounded primary light paths through scene geometry.
`render/scene_optics.c` adapts authored components; the transport module does not
query ECS. `GameLight2D` supplies linear color, power, aperture width and angular
spread. `GameOpticalSurface` supplies IOR, mirror fraction and Beer absorption.
`GameOptics2D` bounds resolution, ray count and interface events. Opaque mirror
color and diffuse reflectance use the material albedo.

The tracer supports circles, oriented boxes, capsules, lines and filled polygons.
A cached, balanced, stackless BVH bounds geometry traversal work. The hierarchy
is rebuilt when geometry or material records change. Custom shape shaders,
deformation and metaballs currently return an explicit diagnostic. Existing
unmarked absorbing geometry is treated as an opaque receiver by this reference;
its volumetric material interpretation has not been generalized.

Fresnel children share their parent's power. Four interface events bound each
ray to five stack entries and 31 path segments. RGB fixed-point atomic sums are
scaled from a conservative per-frame power/grid bound. Primary fields are rebuilt
each frame. They do not retain stale light history.

The persistent medium raster supplies optical depth independently of its density
simulation. The tracer integrates attenuation and first scattering along each
crossed light-grid cell. It produces angular-mean direct radiance, an isotropic
volume/diffuse source for HRC, and surface radiance for composition. HRC integrates
the injected source in world units and carries later diffuse/volume bounce.
Sharp direct paths bypass the HRC blur. Diffuse surface deposition is a grid
approximation: an isotropic source just outside the receiver doubles its power
so the receiver can absorb the inward half. Thin walls and adjacent surfaces need
further evaluation. Diffuse-to-specular chains are not solved by this hybrid.

`GameHRC2D.max_light_size` bounds transport and light history independently of the
display raster. Zero leaves it uncapped. Materials and final composition retain
full display resolution. Preview reports actual material, work and history sizes;
`benchmark_hrc_lab.py --max-light-size` configures both prototypes and overrides.

## Validation and evidence

`build-release/test_suite --radiance` passes the existing renderer checks plus
production-source integration at multiple optical depths and pixel scales,
homogeneous Beer attenuation, first-scattering power, lens concentration against
the independent double-precision reference, mirror transmission rejection,
geometry-cache invalidation, diffuse source power, and immediate primary-light
removal. A 65-object fixture exercises the BVH. The Release `make test` target
also passed, including MCP authoring/protocol checks and the headless app smoke.
Logs are `hybrid-evidence/radiance-cleanup.log` and `hybrid-evidence/make-test.log`.

Two authored fixtures use the actual scene pipeline:
`HRCLabOpticalLens` and `HRCLabOpticalPrism`. Both are created and validated through
Chibi MCP against this worktree's `projects/hrc_lab` root.

Artifacts live under ignored `build-release/hybrid-evidence`:

- `cap-4k`: old persistent-fog scene with a 640-pixel light cap, one repeat,
  8.55 ms median / 9.12 ms p95.
- `lens-1440`: initial hybrid, 640 diffuse grid, 480 optical grid, 4096 rays,
  10.73 ms median / 11.23 ms p95. This initial image is overexposed.
- `lens-balanced-4k`: 480 diffuse/optical grids, 2048 rays, two repeats,
  medians 17.03 / 16.94 ms; p95 18.23 / 18.42 ms.
- `prism-balanced-4k`: same budgets, medians 17.05 / 16.93 ms;
  p95 18.27 / 18.38 ms. The fixture originally missed the mirror; the placement is corrected in the
  later optimization follow-up.

All are hidden-window, frozen-scene, uninstrumented completion timings on the
shared development host. They exclude simulation, visible presentation and
readback. They do not establish 4K60 or a minimum GPU requirement.

Temporary synchronized pass probes found final composition dominant: about
8.69 ms with RGBA32F optical fields, versus about 1.42 ms for optical tracing and
1.23 ms for HRC cascades. A single-variable RGBA16F trial lowered the instrumented
composite to about 7.30 ms. These serialized probes change scheduling and are
**not** additive estimates of the uninstrumented frame. The temporary probes and
half-precision experiment were removed; RGBA32F remains the validated reference.
