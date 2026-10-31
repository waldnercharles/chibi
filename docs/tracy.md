# Profiling explicit products

The separate Studio repository owns its native Tracy and browser User Timing
options and the commands that build with them. The engine's `studio-build`,
`studio-run`, `web-profile` and `web-threads` Make targets were deleted with
Studio and web packaging (7b97ecf7c). Profiling must use the same installed
SDK, project assets, worker budget, and scenario for both measurements.
Instrumented timings are separate from normal performance measurements.

## Native Studio

Configure Studio with `-DCHIBI_STUDIO_TRACY=ON` (RelWithDebInfo) against a
packaged desktop SDK and a Tracy installation discoverable by
`find_package(Tracy CONFIG)`. This instruments Studio's compiled scopes. It
does not enable instrumentation in previously installed SDK archives. Connect
the Tracy profiler to the application to collect those scopes. The former root
`build-tracy` and `run-tracy` products have been removed.

## Browser Studio

Studio's `CHIBI_STUDIO_WEB_TRACE` option records its scopes as browser User
Timing measures. Use the browser Performance panel. The SDK has its own
compilation identity; this option does not recompile SDK archives with
instrumentation.

The former graph behavior Wasm compiler and root browser bundle are retired.
Historical frame-summary and graph-region profiling results do not validate
the canonical source executor or the extracted browser product.

## GPU pass profiling

The current Tracy integration measures CPU scopes and submission. It does not
collect GPU timestamps for HRC. Preview render/present time includes GPU waiting
and must not be reported as the duration of a particular shader.

Use Xcode's Metal debugger and Instruments Metal System Trace on Apple GPUs.
The former profiles passes, shaders and counters; the latter correlates CPU and
GPU scheduling. See [Apple's profiling guide](https://developer.apple.com/documentation/xcode/optimizing-gpu-performance).
Full Xcode is required for those tools. For a `.gputrace` comparison, capture
one warmed frame, preserve its fixture and request, and check deterministic replay.

On Steam Deck, use Radeon GPU Profiler with RADV's capture mechanism, and
RenderDoc for inspecting Vulkan resources and dispatches. RADV capture does not
use the Radeon Developer Panel workflow. See [AMD's RADV notes](https://gpuopen.com/manuals/rdp_manual/)
and [RenderDoc/RGP interoperability](https://gpuopen.com/manuals/rgp_manual/renderdoc_and_rgp_interop/).

HRC now labels prefilter, each quadrant's trace/extend/merge, gather, filtering
and bounce on the GPU command stream. Material tile binning/rasterization, fluid
density, HRC composite and planar reflections also have labels.
These labels identify work in captures; they do not themselves measure it.
The CPU Tracy scope `Gas flow` includes sparse grid construction, velocity
advection, collider boundaries, vorticity and pressure projection.

For timing comparisons keep adaptation disabled, pin scale and trace levels,
and benchmark outside the capture tool as well. Compare live and frozen scenarios
separately. Capture one case at a time and keep instrumented timings separate
from benchmarks.
