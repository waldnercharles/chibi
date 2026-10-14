# HRC path transmission correction

Serial ray segments add optical depths. Alternative paths must average transmission, then convert the result back to optical depth. Averaging their depths instead produced excessive attenuation where one path was clear and another blocked.

`src/render/shaders/hrc/extend.shd` now evaluates the transmission mixture with a stable exponential/logarithm expression. Equal depths return directly, including two half-float infinities. Storage remains eight bytes per interval; no passes, textures or buffers were added.

`extend_joins_serial_intervals` in `test/render/radiance.c` (the `radiance` selector) dispatches the production shader and compares GPU readback with an independent CPU calculation in physical transmission space. It covers transparent, uniform, mixed clear/opaque, unequal serial, thin-fog and saturated-depth paths. The old shader returned 0.006737947 instead of 0.5000227 for the mixed clear/opaque case, and zero instead of 0.5 for a clear/saturated pair.

## Recorded performance evidence

These are the original matched measurements recorded with commit `3eac44469ed283a0377a3a2d3db14125b9cfb09f` on an Apple M4 Pro using Metal. They were not rerun as part of integration.

| Dense-fog output | Transport grid | Before median | After median | Change |
| --- | --- | ---: | ---: | ---: |
| 2560 × 1440 | 1280 × 720 | 29.82 ms | 30.22 ms | +0.40 ms / +1.3% |
| 3840 × 2160 | 960 × 540 | 22.40 ms | 22.45 ms | +0.05 ms / +0.2% |

Each configuration used three matched pairs, 120 measured frames, frozen simulation and hidden presentation. Times include scene submission and GPU completion, not GPU phase timestamps. The rows use different sampling scales and do not provide equal-quality resolution comparisons.

The before/after run ranges overlap: 29.74–30.39 / 28.90–31.55 ms at 1440p, and 22.20–22.87 / 22.26–22.54 ms at 4K. Another editor was active on the desktop. These provisional measurements do not establish a performance regression, speedup or portable frame-rate target. The correction adds arithmetic for unequal depths; it is not a claim of zero cost.

## Integration scope

This integration includes the shader correction and its GPU regression test. The broader GI branch, optical transport, persistent medium state and benchmark/background-window changes remain separate.
