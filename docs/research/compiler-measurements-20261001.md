# Compiler measurements — 2026-10-01

The native loop sweep pins current candidate `a6003cb5dc93654cc96a89389eafe20ac26455de`. The Web smoke runs previously built at `edb8b29a86cbfd6ae202da158582330cf266746f`; the only intervening files are the gas implementation and its test, while the selected smoke and Wasm compiler sources have identical hashes at both commits. In a focused worker smoke, eligible numeric graph regions executed Wasm faster than their bytecode reference, while one-job condition-variable handoff dominated end-to-end latency. The counted-loop sweep also met its 1.5× generated-C target at base lengths 64, 256, and 1024. Neither result establishes a whole-scene gain or compares LuaJIT, V8, or JavaScriptCore; those engine comparisons are deferred.

## Numeric WebAssembly smoke

The runner completed six independent Node processes, three serial followed by three worker, on Node `v24.11.1`. All exited successfully and passed their output and execution-counter assertions. Each process covered eight cases: six eligible graph regions and two bytecode fallbacks. The ordinary flow path reported four Wasm calls per process; six additional calls came from direct graph-module probes, one for each eligible region. These counters describe separate execution paths.

For each worker region, five paired trials measured 4,000 repeated graph-input evaluations per path. The table gives the median per-call times across those trials and then across the three worker processes. “Caller” includes the roundtrip around a 4,000-call batch; it amortizes one worker handoff across the batch.

| Operations / inputs | Case | Worker Wasm / bytecode (ns/call) | Caller roundtrip Wasm / bytecode (ns/call) |
|---:|---|---:|---:|
| 16 / 1 | Sum | 103.5 / 1,096.3 | 121.8 / 1,113.4 |
| 16 / 4 | Sum | 189.1 / 1,214.8 | 198.0 / 1,226.5 |
| 24 / 6 | Sum | 225.6 / 1,673.5 | 233.0 / 1,686.7 |
| 32 / 8 | Sum | 280.5 / 2,143.3 | 285.9 / 2,157.0 |
| 64 / 1 | Sum | 152.3 / 3,672.7 | 160.6 / 3,684.7 |
| 16 / 1 | F32 rounding | 91.7 / 994.0 | 100.2 / 1,004.6 |

The two fallback cases were 12 operations / 1 input, below the 16-operation gate, and 16 / 8, below the four-operations-per-input gate. Their Wasm-request and bytecode-reference measurements both executed bytecode, as asserted by zero Wasm execution deltas. They are fallback-cost observations, not Wasm-versus-bytecode speedups.

Each fresh worker process first compiled and ran a 16-operation / 1-input region. The median worker-side first-use interval was `129,150.4 ns`; caller roundtrip was `698,486.3 ns`. The three raw worker/roundtrip pairs were `129,394.5 / 698,486.3`, `64,697.3 / 564,209.0`, and `129,150.4 / 1,068,603.5 ns`.

A separate one-job test performed 128 individual condition-variable handoffs per path in each of five paired trials. Across the worker processes, median caller roundtrip was `8,901.6 ns` for Wasm and `8,743.3 ns` for bytecode, while median work measured inside the worker was `438.7 ns` and `1,697.5 ns`. The roundtrip-minus-worker estimates were `8,302.7 ns` and `7,185.0 ns`. This shows the handoff dominates the one-job sample; subtraction is an approximate boundary estimate, not a scheduler counter.

All smoke outputs also passed the F32 intermediate-rounding, NaN, cache-retirement, and bytecode fallback checks with assertions enabled. The binaries use Emscripten `6.0.9`, Release `-O3`; the worker build adds pthreads and SIMD. The CMake sanitizer cache says `ON`, but the selected compile and link commands contain no `-fsanitize` flags. The source tree was tracked-clean, and known competing build/test/controller jobs were absent at all 14 observed readiness boundaries. That observation does not rule out ordinary applications or activity between samples.

The JavaScript and Wasm files are retained with hashes in the runner manifest (local evidence: `runtime-next-20261001/runs/behavior-wasm-smoke-20261001T194242-90782/manifest.json`; not included in this repository), and raw trials and full process logs (local evidence: `runtime-next-20261001/runs/behavior-wasm-smoke-20261001T194242-90782/summary.json`; not included in this repository). The backend artifact build was recorded as `edb8b29a86cbfd6ae202da158582330cf266746f`; current source is `a6003cb5dc93654cc96a89389eafe20ac26455de`, with exactly `src/runtime/material_flow.c` and `test/runtime/materials.c` changed between them. The smoke and Wasm compiler source hashes match across both commits. Historical build-log SHA-256: `7cab9250917fc7157269a2e6fa67227a950543d1d526dfe2a62502bee32bc052`. These hashes and timestamps support provenance but do not cryptographically prove the bundle link inputs.

## Counted-loop sweep

The native Release `test_suite` binary (`2b8343f7261a154d37c194fdaf23312e859cf8b097747a96d7dd66dd71c31a8c`) ran five trials of 100,000 invocations at each base length. Each invocation used a trip count cycling from the base length through base length + 7. Trial order rotated among SLJIT, generated C, and bytecode. The generated-C library came from the same lowered loop workload. All results below met the tool’s `SLJIT / generated C <= 1.5` target; its optional enforcement flag was not enabled.

| Loop length | SLJIT median (ns) | Generated C median (ns) | Bytecode median (ns) | SLJIT / generated C |
|---:|---:|---:|---:|---:|
| 64 | 28.10 | 32.14 | 3,767.13 | 0.8743 |
| 256 | 86.64 | 108.52 | 14,375.36 | 0.7984 |
| 1024 | 263.02 | 398.52 | 50,976.45 | 0.6600 |

The first sweep accepted base lengths 64 and 256. Its length-1024 attempt was retained but rejected after the post-run readiness check found a competing build job. A clean-boundary 1024-only retry passed and used the same source commit, test binary, compile database, and generated-C hash. Thus the three accepted sizes combine the first sweep with the separate retry; the rejected raw attempt remains diagnostic only. This current-source sweep refreshes older whole-loop status without changing the historical records.

The accepted 64/256 sweep evidence (local evidence: `runtime-next-20261001/loop-sweep/manifest.json`; not included in this repository), 1024 retry evidence (local evidence: `runtime-next-20261001/loop-sweep-1024-retry/manifest.json`; not included in this repository), and per-length JSON files retain five raw trials and readiness hashes. The compiler and workload hashes are in [compact machine-readable results](compiler-measurements-20261001.json).
