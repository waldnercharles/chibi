# Persistent density and HRC transport

This implementation starts at `3eac4446` on `cwaldner/gi-research`. It adds a
persistent participating medium to the native 2D renderer and two authored
fixtures in `projects/hrc_lab`: `HRCLabPersistentFog` and `HRCLabFogPinhole`.
The main checkout is unchanged. Rendering requires the native HRC compute path;
the GLES3 fallback does not draw this participating medium.

## What runs

`GameMedium` defines one rectangular world-space density grid per scene. It is
owned by the runtime, stepped at the engine's fixed rate, and retained across
renders and camera movement. It needs neither particles nor `GameMaterialWorld`.
The renderer uploads its scalar density and samples it once per output pixel.
There is no particle binning or overlap loop for this field.
The full-screen optical field stores scalar R16F extinction; scattering color
is uniform. At padded 4K this uses 16.2 MiB, saving 48.6 MiB against an RGBA16F
field. Ray integration reuses the already computed transmission. Empty particle
worlds also skip the existing six-image fluid raster pass.

The fixture uses 140 x 84 cells, spaced eight world units apart. Density has a
positive base with bounded broad variation, so the screen does not develop
particle-sized holes. Velocity prescribes periodic wind. Bilinear advection
preserves mass and bounds up to floating-point error, but gradually smooths
detail. It is a simple density solver, with no pressure solve, obstacle
interaction, buoyancy, diffusion, or fluid-body coupling. Existing reactive
particle gas and liquid simulation remains available independently.

The field uses these authored values:

| Field | Meaning |
| --- | --- |
| `origin`, `columns`, `rows`, `cell_size` | Fixed domain in game Y-down world coordinates |
| `density` | Initial average density |
| `variation` | Initial relative variation, from zero to one |
| `detail_size` | Approximate world-space variation wavelength, rounded to fit periodic boundaries |
| `velocity` | Constant world-units-per-second advection |
| `extinction` | Extinction per world unit at density one |
| `scattering` | Packed linear RGB scattering albedo; alpha unused |
| `depth` | Camera-facing homogeneous slab depth in world units |

Changing domain or initial density/variation refills the field. Changing wind
or optical properties preserves its current density. Removing its component,
replacing its entity, or resetting the runtime releases/reinitializes the field.
A second active medium is rejected. The maximum domain is 512 x 512 cells.
Snapshot inspection includes cell count, mass, density bounds and a density
hash. Recording/replay hashes include the simulated density, not only ECS values.

## Light integration

Medium optical depth is `density * extinction * world_pixel_size`. HRC's step
length then integrates extinction over world distance. Medium scattering shares
HRC's diffuse incident field and feedback loop; scattering albedo is weighted
against existing material extinction. This is isotropic planar scattering with
temporal feedback, not a validated full 3D multiple-scattering reference.

Camera composition uses `T = exp(-density * extinction * depth)` and
`L = T * background_and_surface + (1 - T) * scattering_albedo * incident_light`.
This is a homogeneous slab approximation toward the camera. Density varies in
XY, with one constant camera depth. It does not provide camera depth ordering
between arbitrary volumes and surfaces. Adding `GameMedium` replaces the
uniform artistic `GameHRC2D.fog` term for that scene. Empty space outside the
authored domain has no medium scattering.

The attenuation and constant-source integral follow the transport equations in
[PBRT's transmittance chapter](https://pbr-book.org/4ed/Volume_Scattering/Transmittance).
The existing HRC surface model, spatial prefilter and angular reconstruction are
still approximations; this change does not make every material physically exact.

A first capture exposed a source-strength discontinuity: adding weak extinction
to an emissive clear pixel could switch HRC's emission interpretation at an
opacity threshold. The medium path now integrates a constant source with
`q * (1 - exp(-sigma_t * distance)) / sigma_t`, using its continuous limit in
thin media. Unabsorbed sources use world distance, so resolution changes do not
multiply their power. Emission alpha carries medium extinction through the
existing prefilter; visible emitter RGB is retained for composition.

## Quarter-resolution prefilter

The exact 4:1 reduction previously evaluated sixteen Gaussian emission/bounce
taps and sixteen uniform extinction taps, all sampling mip level two. Within
each of the four bilinear quadrants, these reads can be combined at the weighted
mean coordinate. The Gaussian offset is 0.9378234991 canvas pixels; the uniform
offset is one pixel. This reduces 48 texture reads to 12 per work pixel while
preserving the filter algebra. Other scale ratios retain the existing path.

## Validation

- `make build` and `make test` passed in the Release worktree configuration,
  with sanitizers disabled. GPU tests are also run explicitly with
  `build-release/test_suite --radiance`.
- Runtime tests cover ten seconds of advection, mass and bounds, uniform fog
  under large wrapped displacement, deterministic reset/replay, optical edits,
  vacuum, invalid inputs, duplicate components, and entity removal.
- The medium-only prototype runs without particles, physics or material-world
  settings. In the full fixture, 11,760 cells remain present with zero particles.
  Mass changes from 752640 to 752641.875 after 600 steps, about 0.00025%.
- GPU readback of a 64-world-unit path returns transmission 0.527478 against
  the double-precision reference 0.527292. It is identical at 64- and 128-pixel
  output sizes and after a vertical camera move. Half-float storage explains
  the small error.
- Camera slab checks at depths 0, 30 and 3000 match the expected display value
  within two 8-bit levels. A separate production composite check uses known
  incident radiance and colored albedo to test all three scattering channels.
  With no incident light, scattering adds no brightness.
- Eighteen production DDA tests check source integration from vacuum through
  extinction 10, at three canvas/work ratios representing the same world path.
  Relative error stays below 0.1%.
- Native captures and GPU tests use hidden background windows. They still
  consume the host GPU; they do not open or activate editor windows.

## Desktop measurements

Apple M4 Pro, Metal, Release, trace levels 3, edge-aware reconstruction, fixed
scales, no simulation advancement. Each run warms up for 240 render frames,
then retains 80 timing samples. Two other editor processes were active. These
are hidden-window scene times including GPU completion, not isolated GPU
pass timestamps or full gameplay measurements.

| Scene / output / scale | Variant | Median ms | Run range ms | Median p95 ms | Runs |
| --- | --- | ---: | ---: | ---: | ---: |
| HRCLabPersistentFog / 3840 wide / 0.25 | RGBA medium | 30.05 | 29.83–30.48 | 32.85 | 3 |
| HRCLabPersistentFog / 3840 wide / 0.25 | R16 medium | 29.62 | 29.17–29.70 | 32.56 | 3 |
| HRCLabPersistentFog / 2560 wide / 0.5 | R16 medium | 28.13 | 27.94–28.54 | 29.58 | 3 |
| HRCLabFogPinhole / 3840 wide / 0.25 | R16 medium | 28.98 | 28.98–28.98 | 32.43 | 1 |

The three interleaved 4K pairs compare the RGBA field and repeated exponential
calculations against the scalar field and transmission reuse. Their images
have mean absolute RGB difference 0.00375/255, maximum 1/255. All three pairs
produce the same difference metrics. The small timing change does not establish
a broad performance gain on an isolated GPU.

A separate three-pair prefilter experiment reports 30.38 ms before and 30.12 ms
after the four-tap rewrite. Mean RGB difference is 0.01076/255 and maximum is
1/255. The texture-read reduction is established; the frame-time difference is
smaller than desktop variability.

An initial pinhole run was discarded because a scene override retained scale
0.5 despite the requested 0.25. The benchmark now edits scene overrides and
rejects mismatched effective scales. The pinhole row above is the corrected run.

A lower-cost 1440p setting at scale 0.25 measured **13.54 ms** median
across three runs (13.52–13.91 ms), with median p95 **15.00 ms**.
Against the 1440p scale-0.5 capture, mean absolute RGB difference is
0.026/255, maximum 2/255. This is a quality/cost
comparison between two HRC settings, not error against a ground-truth renderer.
Both new lab scenes default to scale 0.25; their other authored lighting settings
are unchanged. This frozen scene fits the 60 fps budget at 1440p on this host.
The higher-cost rows above do not, and 4K60 remains unproven.

Frozen measurements exclude simulation cost. Raw requests, responses, launch
commands, binary/shader/project hashes and PNGs are in the ignored
`build-release/fog-evidence/` folder. Open `comparison.html` there for registered
before/after captures and the aperture scene.

## Remaining rendering work

The [revised realtime direction](realtime-optics-direction.md) makes bounded
reflection/refraction and caustics the next integrated feature slice. Further
HRC tuning must include their cost and visual behavior. Controlled approximations
are acceptable; full physical convergence is not a prerequisite for realtime use.

Physical Snell/Fresnel reflection and refraction, focused caustics, directional
medium phase functions, body-displaced fog, and an MPM adapter are not part of
this implementation. The existing screenshot reflection and liquid distortion
paths remain as described in the original research.

The next optical pass should trace bounded specular paths separately from HRC's
diffuse field. Reflection/refraction must share a material boundary and IOR
contract, with Fresnel splitting and total internal reflection checked against
an independent reference. Caustics then need forward light paths or photon
beams, including medium extinction, rather than another blur of diffuse light.
See [PBRT's specular transport model](https://www.pbr-book.org/4ed/Reflection_Models/Specular_Reflection_and_Transmission).
