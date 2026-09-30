# Tracy profiling

Native builds can include optional Tracy instrumentation for investigating
frame timing, worker overlap, and presentation waits. Normal, test, and web
builds leave it disabled. The instrumentation is on demand, so a profiling
binary does not collect trace data until a Tracy client connects.

## Web profile

Browsers cannot run Tracy, so the web has its own profile. `make web-profile`
serves a build (`CHIBI_WEB_PROFILING`) that names WASM functions for the
browser's profiler and records each Tracy zone as a User Timing measure. In
Chrome, record in the Performance panel: the Timings track shows the same zones
as Tracy, such as Fixed step, Physics and Terrain required tiles, above the
named WASM call stacks.

Every web build also logs the last two minutes of frame timing: the interval
since the previous frame, fixed steps run, simulation time dropped, and
behavior, physics, systems and rendering milliseconds. In the page's console,
`Module.chibiFrameSummary()` reports p50, p95, p99 and max for each timing,
plus a fixed-step histogram, catch-up-frame count and dropped-time totals.
`Module.chibiFrames()` returns correlated per-frame samples, and
`Module.chibiDownloadFrames()` saves them as CSV. Add
`?play&scenario=Firefight` to the URL to replay the same gameplay each time, so
a summary before a change and one after it compare the same play on real
hardware. `make profile-compare PROFILE_SCENARIO=Firefight` compares that play
headless, natively and as WASM. A catch-up frame ran more than one fixed step;
a dropped frame reports nonzero `dropped_ms`. `interval_ms` is the spacing
between app log samples, which are recorded before presentation; it is not a
GPU-completion time. Use the `Present / GPU wait` trace zone to investigate
presentation stalls.

## Threaded web build

`make web-threads` builds a separate application in `build-web-threads`, with
four total task workers by default (`WEB_WORKERS=2` through `8`). Emscripten
prestarts the pthread pool, and the runtime shares one worker budget between
worlds. `make web-threads-serve` sends COOP and COEP headers for local profiling.
The serial `make web` build remains available for browsers without isolation.

GitHub Pages does not send those headers. The optional bundle includes
`chibi-isolation.js`, a service worker that adds them to same-origin responses
within the bundle's directory. Its bootstrap registers the worker and reloads
once before loading `chibi.js`. It requires HTTPS or localhost and service-worker
support. It stores no assets in a cache. This follows the
[static-host isolation approach](https://github.com/gzuidhof/coi-serviceworker);
see Emscripten's [pthread requirements](https://emscripten.org/docs/porting/pthreads.html).

Package the serial app at the site root and the optional bundle under `/threads/`:

```sh
python3 tools/package_web.py --serial build-web --threads build-web-threads --output build-web-site
```

The Pages workflow's manual `web_threads` input includes that directory for
that artifact. A later deployment must opt in again to retain `/threads/`.
Test first-load isolation on a server without headers as well as the header-enabled
development server; `crossOriginIsolated` must be true before pthread startup.

## Behavior Wasm compilation

Web builds asynchronously compile bounded numeric behavior regions to small
Wasm modules. Eligible regions contain 4 to 256 float or double arithmetic
operations, use at most eight scalar inputs, and have no helpers or side effects.
Execution uses bytecode while compilation is pending, when tracing is enabled,
or if a region is ineligible or compilation fails. Graph replacement releases
the old modules, including compilations that have not finished.

Compilation currently runs on the browser main thread. Behavior jobs on pthread
workers use bytecode: their task loop does not yield to the JavaScript microtasks
that complete asynchronous compilation. Worker module handoff needs a separate
implementation and measurement. The backend smoke test covers rounding,
intermediate values, loop re-entry, tracing and graph replacement; its timings
measure eligible arithmetic regions rather than a complete game frame.

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
make run-tracy PROJECT=. SCENE=scenes/Arena3D.yaml
```

Use the [Tracy profiler](https://github.com/wolfpld/tracy/releases) and
connect to the application on its local Tracy port. The first capture should
use the same fixed assets, seed, worker budget, and scenario as the existing
performance check. Capture names should include the build or commit and the
scenario so before and after traces remain comparable.

The first instrumentation slice covers application and scene preview frames,
engine frames and fixed steps, behavior execution, physics, ECS systems,
render submission, presentation, and native task pool workers. Behavior scopes
also separate schedule repair, validation of parallel rows and its worker
chunks, query preparation and worker execution. These zones describe timing
and overlap; they do not replace deterministic scenario results or benchmark
thresholds.

Physics scopes separate ECS synchronization, the Box2D solve, swept-target
maintenance, pair collection, transform writeback, and pair sorting/event
queueing. `Contact events` measures the later event pump, including lifecycle
flushes between batches. Correlate these scopes with their containing engine
frame to distinguish steady costs from work concentrated in slow frames.
Nested zones split pair sorting from contact comparison, and contact-batch
dispatch from lifecycle processing. `Detach behavior attachments` isolates
behavior removal within bulk entity destruction.

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
