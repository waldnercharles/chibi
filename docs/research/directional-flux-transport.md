# Conservative directional optics

Follow-up to [the multigrid experiment](unified-directional-transport.md),
September 21, 2026. This is a hidden GPU lab candidate, not a production renderer.

## Diagnosed error

The earlier directional solver exaggerated the light transmitted by a flat
glass slab by about 67%. Doubling angular resolution from 32 to 64 directions,
doubling spatial resolution, changing grid alignment, and rejecting samples
across material boundaries did not remove it. The solver had settled; multigrid
convergence was not the cause.

Point sampling the refracted direction and multiplying by an IOR radiance ratio
does not conserve a narrow beam on fixed angular bins. Refraction compresses or
expands the angular support of radiance. [PBRT's derivation](https://pbr-book.org/4ed/Reflection_Models/Dielectric_BSDF)
explains the continuous 3D measure change. Its squared IOR ratio must not be
copied into this intrinsic 2D angular discretization.

The new boundary operator transfers each input bin's normal flux,
`L_i * abs(dot(direction_i, normal)) * delta_angle`. Fresnel reflection and
transmission split that flux. Snell's law maps it to outgoing bins; interpolation
weights sum to one over the valid outgoing hemisphere. Dividing by each output
bin's projected angular measure recovers radiance. No extra IOR multiplier is
applied after this discrete flux transfer.

`test/directional_transport_test.py` checks an absorbing slab against an analytic
infinite series of internal reflections, with the known sampled source aperture.
The regression failed at 1.67 times expected transmitted power before the fix.
The conservative implementation agrees within 0.022% for 32/64 directions and
two grid alignments. The bounded long-ray version agrees within 0.001% on those
cases. These checks exercise the real GPU operator, not a duplicate CPU formula.

## Transport and optimizations

The local one-cell solver and multigrid remain available as controls. Long-ray
iteration instead follows a straight characteristic to its first optical
interface, redirects through the same directional field, and repeats. It
integrates the persistent 32x18 density grid along long segments; glass uses RGB
Beer absorption. It starts from zero every frame, so the result does not depend
on settled history. A sweep is not a guaranteed fixed path-event budget because
spatial/angular interpolation combines neighboring states.

Three measured optimizations preserve the corresponding field:

- **Bounded angular search.** Reflection has unit angular slope. For transmission,
  invert the output bin's angular support through Snell's law and clip at the
  critical angle. Only incident bins in those ranges can contribute. The range
  expands near critical angles rather than truncating total internal reflection.
  The exhaustive mode remains a numerical control. Differences on the four
  fixtures are below 0.000004% relative L1.
- **Material identity in alpha.** Every directional field stores glass membership
  in its unused alpha channel, including clears, residuals and coarse transfers.
  Interpolation checks this value instead of reevaluating the geometry for each
  tap. Mirror visibility remains explicit. The four tested fields are bit-identical.
- **Surface-band iteration.** An interface lookup samples one cell upstream of
  the boundary, followed by bilinear reconstruction. An exact distance bound
  therefore identifies all cells that can feed subsequent optical iterations.
  Update that band and evaluate the complete field on the final sweep. The band
  includes the entire reconstruction footprint and preserves material metadata.
  The four tested fields are bit-identical at eight sweeps. This is restricted to
  long-ray optical transport; distributed volume scattering would need a coupled
  source update and cannot simply be omitted outside the band.

Fine-grid-only solves allocate three directional textures. They no longer
allocate unused coarse levels or residual textures. All current fields are
RGBA32F. The lab accepts up to 256 directions with explicit atlas dimension bounds.

The new distance bound differs from the rejected earlier clearance shortcut:
it removes entire unnecessary transport evaluations once per output location,
rather than adding a distance calculation to every interpolation sample.

## Reproduction

```sh
make test-directional-gpu BUILD_DIR=build-release
python3 test/directional_transport_test.py --angular-transfer 2 --long-rays 1 \
  --surface-band 1 --output build-release/directional-band-check
python3 tools/benchmark_optics.py --directional --repeats 2 \
  --output build-release/directional-final
```

`--angular-transfer 0` retains the historical nonconservative point-sampling
control; `1` evaluates conservative contributions exhaustively; `2` bounds the
search and is the lab executable's default. `--long-rays 1 --cycles 0` selects long characteristics, and
`--surface-band 1` limits intermediate iterations. The iteration count includes
the final full-field evaluation. Local-step multigrid does not use surface bands.

The benchmark records commands, source hashes, HDR fields, fixed-exposure images,
independent ray comparisons, light removal and moving-glass captures. Timings
include CPU submission, GPU completion and one output reconstruction, excluding
authored scene rendering, diffuse/volume scattering, simulation and presentation.
Reference images integrate fluence over pixels, whereas the directional field
uses spatial probes; both angular and spatial discretization affect disagreement.

## Measured candidate

`build-release/directional-final` contains two repeats with 16 warmup and 32
measured frames each. All candidates below use eight long-ray iterations,
material-aware interpolation and 32-bit fields. Times cover optics and one
output reconstruction on the shared development host.

| Lens, 120x68, 32 directions | Median ms | Worst repeat p95 ms | Reference L1 |
| --- | ---: | ---: | ---: |
| Exhaustive conservative transfer | 4.04 | 4.24 | 27.28% |
| Bounded angular search | 3.00 | 3.20 | 27.28% |
| Bounded search and surface band | 1.09 | 2.46 | 27.28% |

The first comparison changes cost without changing the image. More directions
improve the angular bands at an additional cost:

| Surface-band candidate | Grid | Directions | Output | Median ms | Worst p95 ms | Reference L1 | Field MiB |
| --- | --- | ---: | --- | ---: | ---: | ---: | ---: |
| Lens | 120x68 | 64 | 1440p | 2.28 | 3.10 | 22.23% | 38.1 |
| Lens | 120x68 | 128 | 1440p | 4.16 | 4.55 | 16.53% | 62.0 |
| Lens | 120x68 | 256 | 1440p | 7.41 | 9.56 | 12.87% | 109.8 |
| Lens | 240x135 | 128 | 4K | 9.82 | 11.66 | 21.11% | 222.0 |
| Prism | 120x68 | 64 | 1440p | 4.36 | 5.83 | 9.25% | 38.1 |
| Prism | 120x68 | 128 | 1440p | 7.12 | 8.56 | 5.11% | 62.0 |
| Prism | 120x68 | 256 | 1440p | 13.75 | 16.04 | 3.86% | 109.8 |
| Prism | 240x135 | 128 | 4K | 17.53 | 19.71 | 5.96% | 222.0 |

Spatial refinement is not monotonically better at fixed angular resolution.
The lens result and its memory cost still prevent promoting this candidate.
Visual inspection also shows curved alias patterns around the lens focus even
at 256 directions. The prism agrees more closely but softens some beam edges.
Those are unresolved reconstruction artifacts, not simulated wave optics.
These component timings do not establish complete-scene 1440p60 or 4K60.

Light removal produces exactly zero light on the first changed frame. At every
captured frame after moving the lens, the result is bit-identical to a fresh
solve with that shifted geometry. `make test-optics-gpu`, the normal slab
regression, and the bounded surface-band slab regression pass. The existing
production renderer was not changed by this follow-up.

## Remaining scope

The candidate demonstrates coupled reflection/refraction in one directional
field and corrects an energy-transfer bug. It still needs better angular/spatial
reconstruction, scene geometry and emitter integration, diffuse surface response,
and coupled multiple scattering before it can replace the scene renderer.
The density interface remains independent of simulation; no MPM solver is added.
The integrated forward tracer and HRC remain the working scene reference.
