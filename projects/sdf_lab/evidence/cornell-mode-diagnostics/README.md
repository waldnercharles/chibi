# HRC quadrant, Gather, and bounce diagnostics

These nine stills come from `chibi_ctl`'s native `preview_scene` renderer.
The first seven are opt-in debug views of the authored flat 2D Cornell fixture
and the matched camera-relative Blur/Gather scenes. The final pair uses the
flat fixture's `Fluence` view with only `bounce_intensity` changed from 0 to
0.35. Project copies are made under `/tmp`; the capture script changes no
authored project scene. Each run records source and temporary-scene hashes,
request/response data, stderr, and image hash beside the PNG.

## Reproduce

From the repository root, with the Release controller built:

```sh
source /workspace/.cloud-tools/activate.sh
xvfb-run -a python3 tools/hrc_cornell_diagnostic.py \
  --build build-release --width 320 --height 180 --render-frames 256
```

The request asks for one final image after advancing simulation. The native
response reports frame 257, 240 performance samples, and 240 warmup frames.
Captures run one at a time at 320×180, HRC resolution 0.5, and half-resolution
Gather; there is no frame sequence or video in this diagnostic set. The
response backend is `CF_BACKEND_TYPE_VULKAN`. This run used the workspace
software Vulkan ICD `lvp_icd.json` / `libvulkan_lvp.so` (Mesa lavapipe
25.0.7-2+deb13u1, LLVM 19.1; Vulkan API 1.4.305) with Xvfb. Per-case timings
are in the JSON responses; they are software-renderer diagnostics, not GPU
performance claims.

The RGB of views 10 and 11 is displayed with
`log1p(max(energy, 0) * 65535) / log1p(65535)` to preserve weak field
structure in the 8-bit preview. This display transform is not part of HRC
lighting. The bottom strip shows alpha in grayscale. In view 10 raw HRC
quadrants have alpha 1 (the gather pass writes it as a sentinel); that strip
does not represent opacity. In view 11 alpha is the processed openness field
seeded from open camera-plane pixels. In view 12 it is per-direction openness
divided by the three possible probes.

For view 12, each 2×2 tile is one HRC direction in shader order q0, q1, q2,
q3: +X, +Y, −X, −Y in texture coordinates. RGB is actual probe data:

- Red: in-bounds, open candidates divided by three possible radial samples.
- Green: candidates tested for height visibility divided by three. Empty
  receiver pixels have no height query.
- Blue: visible candidates divided by the number of visibility tests, or zero
  when there were no tests.
- The lower grayscale strip: open weight divided by three.

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

The machine-readable run data, requests, responses, and renderer stderr logs
are in this directory. `manifest.json` lists the controller hash, requests,
per-case image hashes, and performance responses. The confirmed persistent
Library IDs for all nine PNGs are in `library_ids.json`.
