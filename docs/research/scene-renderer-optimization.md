# HRC scene renderer optimization

September 21, 2026. Worktree `.worktrees/gi-research`, starting at `2fbf282e`.

## Direction

Continue improving the integrated HRC scene renderer. HRC carries diffuse and
volume transport; bounded optical paths supply sharp reflected/refracted light,
caustics and sources for subsequent diffuse bounce. A unified directional solver
remains an experiment, not a dependency for completing this renderer. Its current
results do not justify replacing HRC. MPM remains a future density/material input.

The uncommitted lab footprint/volume-source experiment was preserved as
`build-release/scattering-evidence/parked-volume-experiment.patch`, checked with
`git apply --check`, and removed from the active source tree. Its original optics
regression passed, but the new scattering iteration had not passed an independent
analytic comparison. It must not be treated as production functionality.

## Changes

The optical direct and surface textures now use RGBA16F. A per-frame multiplier
keeps their conservative power bound at or below 32768 before storage and restores
world-space radiance in composition. This prevents half-float overflow at large
light powers or small world cells. The isotropic source consumed by HRC remains
RGBA32F. At 480 by 271, the three output fields fall from 5.95 MiB to 3.97 MiB.
The isolated frame-time change was too small to establish a speedup.

For capped HRC grids whose history and work dimensions match, prefiltering now
combines repeated mip reads. A bilinear sample is linear within each interpolation
region. Splitting a block at the region boundary allows its Gaussian/uniform
weighted average to use the weighted mean coordinate in each region. Separable
weights reduce each field to four texture reads instead of a full two-dimensional
loop. Other grid ratios retain their existing fractional history-LOD behavior.

The prism fixture's mirror now intercepts the refracted beam and reflects it onto
the receiving wall. The previous placement missed the beam. Both optical fixtures
retain their authored persistent medium and HRC diffuse feedback.

`benchmark_hrc_lab.py --animate` advances simulation during the measured frames,
checks the actual frame count and records timings that include simulation. Frozen
runs remain the default for image comparisons. All runs use hidden windows.

## Accuracy

The new GPU prefilter test compares the production reduction with the original
full quadrature across six fractional/anisotropic grid configurations, colored
noise and an HDR impulse. With explicit float interpolation, five configurations
agree exactly after half-float storage; the sixth has a maximum relative difference
of 0.068%, consistent with one rounding unit. Native sampler interpolation changes
rounding near the impulse: maximum local difference 1.14%, worst field relative L1
0.178%. These are separate checks with separate bounds; algebraic equivalence does
not imply bitwise equivalence of native filtered texture reads.

Optical range tests cover source powers 0.001, 1 and 1,000,000, including a return
from the bright case to the dim case. Direct and surface fields remain finite;
relative L1 error against power-scaled unit output stays below 0.03%. Existing
Beer attenuation, scattering, lens focus, reflected flux, BVH invalidation,
diffuse-source power and light-removal checks still apply. A composite check also
reconstructs incident light with a display multiplier of 100,000.

Matched 4K lens captures before and after both changes differ by at most 1/255 per
channel, with mean absolute RGB difference 0.00454/255 and zero p99 difference.
The prefilter-only comparison also has a maximum difference of 1/255.

## Measurements and limits

Artifacts and exact launch requests are under
`build-release/composite-evidence`. Each run discards 240 frozen warmup frames and
the first 16 measured frames. Timings include GPU completion and exclude visible
presentation and readback. They are specific to this development host and these
small authored fixtures; they do not establish a minimum GPU or game-wide 4K60.
The host is an Apple M4 Pro with 20 GPU cores and 48 GB of unified memory.

In a fresh sequential prefilter comparison, with scaled optical textures in both:

| 4K lens, frozen | Repeat medians | Repeat p95 |
| --- | --- | --- |
| Original prefilter | 14.42 / 14.33 ms | 14.93 / 14.87 ms |
| Combined reads | 14.09 / 14.13 ms | 14.58 / 14.54 ms |

This repeat supports a modest roughly 2% scene-time improvement. An earlier
comparison showed a larger difference; it is not the basis for the speedup claim.
Moving an absorption debug read inside its branch produced identical images but
no speedup and was reverted.

The 4K prism with advancing fog measured 13.95 / 13.89 ms median and 14.27 / 14.31
ms p95. Both runs advanced 120 frames and produced the same final medium hash.
Density remained between 0.8525 and 1.1474, with relative mass drift below 0.0001%.
The mirror and prism were stationary in this measurement.

The full renderer goal remains open. Diffuse-to-specular paths, broader moving
optics coverage, representative gameplay loads and visual quality beyond these
controlled optical fixtures still need work. The persistent medium currently uses
prescribed wind with periodic advection; it is not an obstacle-coupled fluid solve.

The advancing 1440p lens measured 8.11 / 8.31 ms median and 8.59 / 8.70 ms p95.
The advancing 4K persistent-fog fixture without optical emitters measured
12.85 / 12.98 ms median and 13.46 / 13.47 ms p95. Those runs also preserved
positive density and mass over 120 simulation frames.

## Validation and reproduction

Release `make build`, `make test`, and `build-release/test_suite --radiance`
passed. The authored prism passed worktree-local MCP `validateAsset`. A deliberate
single-region filter mutation failed the new test with 27–63% relative L1 error;
the production shader was restored before the passing checks. Logs are in the
artifact directory, including `prefilter-negative-control.log` and
`radiance-final.log`. Source and shader fingerprints accompany every benchmark.

```sh
CCACHE_DISABLE=1 make test BUILD_DIR=build-release
build-release/test_suite --radiance
python3 tools/benchmark_hrc_lab.py --project projects/hrc_lab \
  --hrc-prototype HRCLabHRC --scene HRCLabOpticalPrism \
  --width 3840 --height 2160 --scale .25 --max-light-size 480 \
  --render-frames 120 --repeat 2 --animate --output build-release/prism-motion
```

The saved [comparison report](../../build-release/composite-evidence/comparison.html)
links matched frames, timings, hardware and logs. The captured renderer images were
visually inspected. The app browser's file-URL policy blocked a hidden HTML preview;
the report's markup and local links were checked without a browser preview.
