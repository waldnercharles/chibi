# HRC quadrant, Gather, and bounce diagnostics

These nine checked-in stills come from `chibi_ctl`'s native `preview_scene`
renderer. The first seven are opt-in debug views of the authored flat 2D Cornell
fixture and matched camera-relative Blur/Gather scenes. The final pair uses the
flat fixture's `Fluence` view with only `bounce_intensity` changed from 0 to
0.35. They were captured before the current diagnostic band remap and Depth
unsupported marker were added, so they do not show those latest shader changes.
The capture script can regenerate them and now also offers a Depth-mode case.
Project copies are made under `/tmp`; the capture script changes no authored
project scene. Each fresh run records source and temporary-scene hashes,
request/response data, stderr, and image hash beside the PNG.

## Reproduce

From the repository root, with the Release controller built:

```sh
source /workspace/.cloud-tools/activate.sh
xvfb-run -a python3 tools/hrc_cornell_diagnostic.py \
  --build build-release --width 320 --height 180 --render-frames 256
```

Use repeated `--case <name>` options for a bounded subset; case names are the
PNG filename stems. Each renderer process has a 180-second timeout by default
and can be adjusted with `--timeout`.

The request asks for one final image after advancing simulation. The native
response reports frame 257, 240 performance samples, and 240 warmup frames.
Captures run one at a time at 320×180, HRC resolution 0.5, and half-resolution
Gather; there is no frame sequence or video in this diagnostic set. The
response backend is `CF_BACKEND_TYPE_VULKAN`. This run used the workspace
software Vulkan ICD `lvp_icd.json` / `libvulkan_lvp.so` (Mesa lavapipe
25.0.7-2+deb13u1, LLVM 19.1; Vulkan API 1.4.305) with Xvfb. Per-case timings
are in the JSON responses; they are software-renderer diagnostics, not GPU
performance claims.

The current shader and a fresh run's per-case `display_transfer` record the
following mapping. The checked-in manifest and images are still the earlier
nine-case run. Views 10 and 11 use
`log1p(max(energy, 0) * 65535) / log1p(65535)` for RGB; this display transform
is not part of HRC lighting. Their top 82% band maps the complete field height
to RGB; the bottom 18% band independently maps the complete field height to
alpha in grayscale. In view 10 raw HRC quadrants have alpha 1 (the gather pass
writes it as a sentinel), so that strip is constant and does not represent
opacity. In view 11 Blur/Gather alpha is the processed openness field. Depth
thickness packs diffuse/depth, specular/gloss, normal/validity, and
direction/shadow rather than four light quadrants; view 11 returns a magenta
unsupported marker for that mode.

For view 12, each 2×2 tile is one HRC direction in shader order q0, q1, q2,
q3: +X, +Y, −X, −Y in texture coordinates. The top 68% band maps the
complete probe field to RGB; the bottom 32% maps the complete field to
openness in grayscale. Each rendered pixel selects its nearest low-resolution
cell with `texelFetch`; discrete fractions are not bilinearly interpolated.
RGB is actual probe data:

- Red: in-bounds, open candidates divided by three possible radial samples.
- Green: candidates tested for height visibility divided by three. Empty
  receiver pixels have no height query.
- Blue: visible candidates divided by the number of visibility tests, or zero
  when there were no tests.
- Alpha in the lower grayscale strip: open weight divided by three.

Candidates outside the image or behind closed source pixels are skipped.
Gather takes three radial samples for each of four directions and checks an
occupied heightfield along at most four segments per accepted candidate. Its
reported `surface_work` counters are upper bounds; this view is the per-pixel
candidate/test result. The shader reuses HRC quadrants and does not trace the
SDF or loop over emitters.

## Captures and interpretation

| Capture | Scene / view | Result |
| --- | --- | --- |
| [flat-blur-transport.png](flat-blur-transport.png) | `HRCLabCornell2D`, raw HRC quadrants | Broad nonzero 2D transport input. |
| [flat-blur-surface.png](flat-blur-surface.png) | `HRCLabCornell2D`, processed Blur quadrants | Spatially filtered field; alpha is open-space coverage. |
| [camera-blur-transport.png](camera-blur-transport.png) | `HrcCornellModesBlur`, raw HRC quadrants | Sparse camera-relative input with feedback disabled. |
| [camera-blur-surface.png](camera-blur-surface.png) | `HrcCornellModesBlur`, processed Blur quadrants | RGB is black in this 8-bit log preview; openness remains visible. This is consistent with sparse input and zero bounce in the matched mode scene, and is not by itself proof of a Blur regression. |
| [camera-gather-transport.png](camera-gather-transport.png) | `HrcCornellModesGather`, raw HRC quadrants | Same source field as the matched Blur scene. |
| [camera-gather-surface.png](camera-gather-surface.png) | `HrcCornellModesGather`, processed Gather quadrants | Sparse received samples; directionality remains visible. |
| [camera-gather-probes.png](camera-gather-probes.png) | `HrcCornellModesGather`, Gather probes | Actual candidate, query, visibility, and openness fractions. |
| [flat-fluence-bounce-off.png](flat-fluence-bounce-off.png) | `HRCLabCornell2D`, Fluence, `bounce_intensity=0` | Matched control. |
| [flat-fluence-bounce-on.png](flat-fluence-bounce-on.png) | `HRCLabCornell2D`, Fluence, `bounce_intensity=0.35` | Same authored scene and feedback setting; only intensity differs. |

To render the additional Depth marker with the current shader, run the script
with `--case camera-depth-surface`. That PNG is not part of the checked-in
nine-image set.

The isolated bounce pair's 8-bit PNGs differ by 0.666/255 mean absolute RGB
channel value; 3.90% of channel values differ by more than 2/255, and 7.47%
of pixels have at least one channel beyond that threshold. This measures the
encoded `Fluence` debug previews at the captured frame, not calibrated light
energy or a physically based reference. The camera-relative mode fixtures
keep feedback off so the surface-mode comparison is separate from this bounce
comparison.

## Scope

Blur applies a Gaussian filter to the four screen-plane HRC quadrant fields;
it has no local height visibility test. Gather samples the same fields with
bounded radial candidates and local height visibility. Depth thickness uses
camera-relative emitter reconstruction and a finite assumed slab; it does not
make the transport fully 3D. In the camera scenes, only the nearest visible
surface height is available at a pixel. The open-front Cornell cutaway is a
2.5D lighting diagnostic, not a full path-traced Cornell box. The renderer has
no volumetric scattering phase function here; fog/glow and emissive pixels are
not evidence of volumetrics.

The machine-readable data, requests, responses, and renderer stderr logs for
the checked-in nine captures are in this directory. A fresh script run writes
the controller hash, requests, per-case image hashes, exact display transfer,
timeout, and performance responses to `manifest.json`. The confirmed
persistent Library IDs for the checked-in PNGs are in `library_ids.json`.
