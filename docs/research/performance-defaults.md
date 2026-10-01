# Performance and automatic parallel execution

Implemented September 30, 2026, on `codex/performance-defaults`, starting from
`5a74676f2a8115b089cdeba54e20f98e735f041a`. The implementation and measurements
use an isolated worktree. The primary checkout's existing changes were preserved.

## Compiler and storage changes

- Native floating-point allocation uses the target ABI's available registers.
  Conversions write directly to their destination; min/max and clamps avoid
  unnecessary moves while preserving NaN, infinity, and signed-zero behavior.
- Guarded optimization removes repeated exact typed expressions and immutable
  argument, delta, and nominal-name reads. Constant branches are followed without
  reordering effects or floating-point arithmetic. Original checked instructions
  remain available for inspection and insufficient budgets.
- Proven counted loops preflight their entire step cost. The successful native
  path removes per-iteration budget checks and promotes up to two persistent
  scalar variables to registers, flushing at successful return. Endpoint bounds,
  arithmetic widths, result aliasing, and eligibility are checked. The ordinary
  path retains eager stores, exact partial effects, and source diagnostics.
- Nested managed records copy their outer storage once and retain nested fields
  directly. Add/Append/Insert construct final owned arrays without copying an
  intermediate container. Set/Remove avoid copying discarded payloads while
  preserving layout validation and failure behavior.
- Exact self-assignment of already owned managed storage avoids another retention
  copy. Read-only ForEach loops borrow persistent arrays when a whole-function
  proof excludes mutation, raw pointer writes, host calls, commands, and suspension.
  Inspection keeps independent snapshots. These are scoped ownership proofs;
  the runtime still retains values that escape or cross observable boundaries.
- Fused query reductions share validated component columns, snapshot metadata,
  and dominating entity-row lookups. Presence checks, predicate short circuits,
  effect barriers, and the original diagnostic fallback remain intact.

## Parallel execution and WebAssembly

Native Auto worker selection was already enabled. The gas pressure solver now
builds compact red/black rows and a dense pressure buffer, reuses them, and
processes four independent interior rows with SIMD. It retains 32 iterations,
neighbor addition order, missing-cell and solid-boundary rules. Colors join
before the next color starts. A color uses the shared task pool at 16,384 rows;
smaller work stays inline after measurements showed dispatch overhead at 8,192.

`make web` now builds and packages both browser variants. Supported browsers
automatically choose the worker/SIMD bundle, with a default budget of four threads
including the caller. The bootstrap verifies shared-memory/SIMD support and worker
policy, obtains isolation on static hosts, and recovers to serial on script,
module, service-worker, or worker failures. Query strings, fragments, and workspace
storage survive the switch; `?serial` selects the compatibility variant explicitly.
Upstream physics libraries' unconditional SIMD options were removed from that
variant. Disassembly confirms zero SIMD instructions in its app and controller.

Eligible numeric graph regions run as Wasm on persistent workers through a
synchronous, bounded 16-entry cache. Unique numeric handles, retirement epochs,
negative caching, and JS-only retained exports preserve module lifetime. Main
compilation remains asynchronous. Cached fixed-arity calls avoid string-key
construction, array views, and argument spreading. A profitability gate retains
bytecode for short regions: at least 16 operations and four operations per input.
Emscripten's generated worker code refreshes heap views at each numeric read/write
after memory growth. Remaining graph control, host operations, and managed work
continue through their existing backend; this does not compile complete graph
events to Wasm.

Execution reports now describe actual published/live behavior backends and native
event coverage. Custom System coverage is marked unverified. Empty scenes no
longer report interpreter work that did not happen. The scheduler's sanitizer
configuration now follows the selected sanitizer, allowing a fully instrumented
ThreadSanitizer build instead of mixing TSan with upstream ASan/UBSan.

## Native measurements

Apple M4 Pro, 14 CPU cores, 48 GiB RAM, macOS 26.7, LLVM 23.1.1. Release builds
disable sanitizers and preserve strict floating-point order. The installed SDK 27
requires newer compiler annotations, so both native builds use SDK 26.5. Five
trials per accepted comparison; builds and owned browser sessions were stopped
for timings. Independent operating-system activity remains a source of variance.

| Workload | Baseline median | Candidate median | Result |
| --- | ---: | ---: | --- |
| Spring numeric kernel | 18.567 ns | 8.554 ns | 2.17x faster; 1.17x generated C |
| Counted range, length 64 | 75.152 ns | 25.442 ns | 2.95x faster; 0.84x generated C |
| Nested managed records | 273.460 ns | 244.080 ns | 11% less time; 1.04x generated C |
| Managed component kernel | 297.640 ns | 263.880 ns | 11% less time; 1.04x generated C |
| Struct-array Tick, 512 entities | 1,926.392 ns/entity | 1,369.934 ns/entity | 29% less time |
| Encounter activation, 20,000 enemies | 0.208950 ms | 0.178210 ms | 15% less time |
| Gas flow, 256x256 parcels, eight workers | 13.797000 ms | 9.992250 ms | 28% less time |
| Whole fluid tick, same workload/workers | 16.906267 ms | 12.981367 ms | 23% less time |

Generated C uses Clang `-O3 -ffp-contract=off` on the same checked lowered program.
These controls test code quality and runtime overhead; they are not handcrafted
optimal C algorithms or cross-engine benchmarks. The loop native path additionally
uses its successful-budget/alias proof. All seven motion kernels, managed kernels,
and tested Tick updates meet their existing 1.5x generated-C target. The large
encounter body remains about 1.62x generated C despite the activation improvement.

Tick runs cover six workloads, eleven backend/batch modes, and 64/512/4,096 entities.
Struct-array updates improve 24–29% across the ordinary run and the longer large
run. Counter and scalar component work is effectively unchanged. An apparent
integer-array slowdown in the short run did not persist at 128 frames: candidate
54.348 versus baseline 56.076 ns/entity, with overlapping ranges. No sustained
gain is claimed for that workload. Backend state checksums match, and warmed
batch/update paths have zero tracked VM scratch allocations, runtime stores, and
arena backing-block growth. That metric does not count every allocation.

Gas measurements cover 64x64, 128x128, and 256x256 parcels, moving/rotated boundaries,
one/eight workers, and a candidate with explicit pressure SIMD disabled. Identical
flow hashes are required. Native compact layout plus SIMD reduces gas time roughly
25–28%; explicit SIMD adds about 4–7% in the one-worker comparison. Pressure task
dispatch adds a modest gain at the largest size. A smaller hash-capacity experiment
increased probing and regressed timing substantially; it was removed.

## Web measurements and scene scope

Emscripten 6.0.9 and Node 24.11.1 run the same gas harness: five trials, five warmup
steps, and 60 measured steps. These compare the current compatibility bundle with
the current worker/SIMD bundle, rather than an old web build. Both requested worker
settings in the compatibility bundle actually run serially, so its ten identical
workload samples are pooled below.

| Parcel grid | Serial whole fluid tick | Worker/SIMD whole fluid tick | Speedup |
| --- | ---: | ---: | ---: |
| 64x64 | 1.178766 ms | 0.987522 ms | 1.19x |
| 128x128 | 5.275175 ms | 3.885170 ms | 1.36x |
| 256x256 | 23.854882 ms | 16.204576 ms | 1.47x |

Explicit pressure SIMD adds about 2% versus the worker bundle with that kernel
disabled. Layout and parallel work account for larger gains. These are Node
simulation measurements, not browser presentation or GPU measurements.

The authored Firefight replay runs 1,800 measured frames after 60 warmup frames.
Five paired native trials reduce average headless frame time from 1.736286 to
1.706996 ms, about 1.7%. P95 is 2.251 versus 2.260 ms; P99 is 2.414 versus 2.448 ms.
Their ranges overlap, so no tail-latency improvement is claimed. The independent
HRCLabPinhole fixture is effectively unchanged at about 0.0022 ms/frame. This limits
how broadly the kernel improvements can be interpreted. GPU completion, rendered
frame distributions, cold publication, and LuaJIT/V8/JavaScriptCore comparisons
were not benchmarked here.

## Validation and reproduction

- Full `make test` with ASan/UBSan passes, including generation, reflection,
  reload, development, MCP authoring, assets, backend checks, and the full suite.
  The whole-function audit covers 40/40 entries across 23 assets, 36/36 effectful,
  99/99 pure, and 4/4 library bodies.
- TSan-instrumented scheduler and runtime pass `--materials` and
  `--runtime-execution`. Native, serial-Wasm, and worker-Wasm backend smoke tests
  pass. Regressions exercise bounds/budgets, overlapping buffers, NaN/signed zero,
  nested ownership/source destruction, suspension, query barriers, and bitwise
  serial/parallel pressure parity.
- Final native/serial/worker hashes agree: Firefight `69c6f22b974a4f7a` and
  HRCLabPinhole `16d38da6700d498b`.
- Real Chrome startup renders with shared memory and three active pthreads.
  Explicit serial, blocked script, rejected module, and CSP worker rejection
  recover to the scalar bundle. JS/Python startup/packaging tests, formatting,
  and diff checks pass. All owned servers/browser sessions were closed.

Use `tools/bench_motion.py`, `bench_loops.py`, `bench_managed.py`, `bench_ticks.py`,
`bench_encounter_query.py`, `bench_gas_flow.py`, and `bench_scenes.py` with optimized
builds. The gas runner's `--compare-scalar` controls its explicit SIMD kernel;
`--web --workers 4` compares the paired web libraries under Node. The scene runner
alternates native versions on identical project assets and enforces fixed-step
state parity. Timing JSON records medians/ranges, trial logs, binary/generated
source/library hashes as applicable, and validation scope.

The companion `outputs/performance-defaults.md`, JSON/log evidence, and
`implementation-state.json` in this Codex task record the exact local source,
build, and delivery state. Implementation is local; no deployment was performed.
