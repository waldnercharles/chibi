# Tracy profiling

Native builds can include optional Tracy instrumentation for investigating
frame timing, worker overlap, and presentation waits. Normal, test, and web
builds leave it disabled. The instrumentation is on demand, so a profiling
binary does not collect trace data until a Tracy client connects.

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
render submission, presentation, and native task pool workers. Behavior scopes
also separate schedule repair, validation of parallel rows and its worker
chunks, query preparation and worker execution. These zones describe timing
and overlap; they do not replace deterministic scenario results or benchmark
thresholds.

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
