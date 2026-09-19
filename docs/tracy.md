# Tracy profiling

Native builds can include optional Tracy instrumentation for investigating
frame timing, worker overlap, and presentation waits. Normal, test, and web
builds leave it disabled. The instrumentation is on demand, so a profiling
binary does not collect trace data until a Tracy client connects.

## GPU pass profiling

The current Tracy integration measures CPU scopes and submission. It does not
collect GPU timestamps for HRC. Preview render/present time includes GPU waiting
and must not be reported as the duration of a particular shader.

Use Xcode's Metal debugger and Instruments Metal System Trace on Apple GPUs.
The former profiles passes, shaders and counters; the latter correlates CPU and
GPU scheduling. See [Apple's profiling guide](https://developer.apple.com/documentation/xcode/optimizing-gpu-performance).
Full Xcode is required for those tools. The September 19 local environment has
Command Line Tools selected and no Xcode installation was found in the usual
application directories.
The Metal runtime can still write `.gputrace` files programmatically here. The
[NeonSwarm capture workflow](neon-gpu-benchmarks.md) records one warmed frame,
preserves its fixture and request, and checks deterministic replay.

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
and benchmark outside the capture tool as well. The deterministic helper accepts
`--scale .5 --trace-levels 1`; each report records both settings.
Use `--mode live` to retain the original moving scenario, or `--mode frozen` for
a stable endpoint. The default `both` mode compares both workloads. Capture only
one case at a time and keep instrumented timings separate from benchmarks.

## Native profile

Build the profiling targets with symbols and sanitizers disabled:

```sh
make build-tracy
```

Run the editor or another authored project:

```sh
make run-tracy PROJECT=. SCENE=scenes/Arena3D.json
```

Use the [Tracy profiler](https://github.com/wolfpld/tracy/releases) and
connect to the application on its local Tracy port. The first capture should
use the same fixed assets, seed, worker budget, and scenario as the existing
performance check. Capture names should include the build or commit and the
scenario so before and after traces remain comparable.

The first instrumentation slice covers application and scene preview frames,
engine frames and fixed steps, behavior execution, physics, ECS systems,
render submission, presentation, and native task pool workers. These zones
describe timing and overlap; they do not replace deterministic scenario
results or benchmark thresholds.

## Tracy MCP

Tracy's upstream source includes an optional MCP server. It loads `.tracy`
captures, connects to a live instrumented process, and exposes the Worker
analysis surface for CPU zones, GPU zones, frames, threads, memory, locks,
plots, and callstacks. The server is separate from the project's authoring
MCP because it needs Python bindings and runs as a local HTTP MCP service.

Build the bindings when MCP analysis is needed:

```sh
make build-tracy-mcp
python3 -m pip install -r build-tracy-mcp/_deps/tracy-src/extra/mcp/requirements.txt
make tracy-mcp
```

The launcher finds the generated `TracyServerBindings` module and prints the
MCP URL. Set `TRACY_MCP_PYTHON` before both build and run to use a virtual
environment for the bindings and server. Set `TRACY_MCP_BUILD_DIR` to use
another build directory. Call `unload_capture` after analysis so large traces
do not remain resident in the MCP process.

The useful workflow is:

1. Run the deterministic scenario and record its world hash and timing result.
2. Capture the same run with Tracy attached.
3. Ask the MCP for the hottest self time zones and frame percentiles.
4. Compare captures after a code change, then rerun the deterministic check.

GPU pass timing depends on the renderer backend exposing GPU timestamps. The
existing preview measurement records GPU complete host time and the Tracy
CPU zones identify the submission and presentation boundaries; a capture
without GPU zones remains a CPU and wait diagnosis.

For fixed-quality NeonSwarm comparisons, use
`tools/benchmark_neon_materials.py` as described in
[the material performance report](neon-material-performance.md). Fluid reaction,
density, relaxation, collision, raster preparation and HRC submission zones are
available in profiling builds. These renderer zones measure CPU submission;
they are not GPU pass timestamps.
