# Compiler performance status

Checkpoint: 2 November 2026, local Mac evidence. The active correction is [PR #1337](https://github.com/waldnercharles/chibi/pull/1337), commit `f11df26379b2250ab34d07fda140acfc546de353`, tree `38a468ea8f28fcbfccb783227bd030459d01a11c`. The coordinator imported it as `ad803aea440c485545d03e49bcd8bb385526d8ca`; that later composition needs its own acceptance results.

Latest published integration is `7482af7bef75d950ab6e3d9216f8e7078ce70394`, tree `68312d959ad5c6f232ab0530284dedc2c874b2e9`, with [CI 41574174752](https://github.com/waldnercharles/chibi/actions/runs/41574174752). Constructor Pointers/Body and Declarations are byte-identical to f11df263. Three physical proof images from frozen `95ec7d9` share SHA-256 `340210ff3cfa3bc32472fa3a027a32d65a6caf9691002e7d5995ef8718b33ad0`; shared-load stage times are not controlled measurements. That proof belongs to compiler tree `4d8d962e…`, while7482 uses `abd17fc7…` after a Scalar.chibi diagnostic change. It is not exact-head7482 acceptance. The prior native_cli ReadText blocker is corrected in source with explicit error checks and retained text assertions. One initial new-CI snapshot showed no failed step; no completion claim or repeated polling. See `published-7482-checkpoint/identity.json`.

Newer published integration checkpoint: `6b46f9261e6c0cf6499a528d5b288520ee08bf53`, tree `667037405b9e722bb82c6907d256b1cf290a2b25`, compiler `086a9dbd…`, core `440ceef1…`, with [CI41583508859](https://github.com/waldnercharles/chibi/actions/runs/41583508859) active per the coordinator. PR head was independently rechecked; no jobs were polled or builds duplicated here. The coordinator reports466 compiler tests plus remove_item/source cases passing; earlier16cc fixedpoint/runtime checks remain scoped to that older source. Independent native Driver/Graph full-table comparison data is still being prepared, including reviewed replacements for the old88-row/33-argument recipe. Exact final-head acceptance and controlled timing are pending. See `integration-checkpoint-6b46f926.json`.

## Where the work goes

The latest concrete bottleneck is unnecessary declaration construction. Each address-using package pointed to synthetic optional/collection types created by prior packages, creating another family for the next package. A declaration-only probe excludes body construction and LLVM entirely:

| Imported packages | Source bytes | Type rows before | Type rows after |
| ---: | ---: | ---: | ---: |
| 1 | 155 | 261 | 174 |
| 2 | 266 | 810 | 174 |
| 4 | 488 | 6,588 | 174 |
| 8 | 932 | Unknown at original 300-second deadline | 174 |

The four-package count falls 97.36%. The fix removes the unused families and supplies the exact constructors required by addressed generic value parameters. It changes 16 production lines and removes one; it adds two tests to the existing runner. The eight-package baseline was not retried.

Earlier exclusive phase measurements on frozen `9934af8` placed 23.874 seconds in semantic/IR/facts work, 0.040 in parsing, 0.126 in native lowering, 1.061 in LLVM-text validation/emission and 10.960 in elapsed LLVM pipelines. Those measurements predate several ownership fixes and do not describe the present proportions. Chibi already runs bounded parallel LLVM pipelines. Current complete phase attribution, dynamic lookup row visits and aggregate concurrent memory remain unmeasured.

## Controlled results already established

Each row is its own same-input, same-Mac/toolchain comparison. Absolute times from different revisions must not be added or compared as one series.

| Change/workload | Mean wall before → after | Mean reported CPU before → after | Evidence |
| --- | ---: | ---: | --- |
| StatementStep read-only type-table access | 35.024 → 31.494 s | 63.925 → 60.685 s | Four balanced pairs; identical output |
| CallStep read-only type-table access | 29.347 → 26.067 s | 57.635 → 53.960 s | Four balanced pairs; identical output |
| Primitive record projection, compiler rebuild | 37.823 → 29.740 s | 89.158 → 81.100 s | Four balanced pairs; all faster |
| Same projection, same 429 tests | 12.255 → 11.260 s | 12.955 → 12.265 s | Four balanced pairs; exact names/order/output |

Projection feedback observations were 214–327 ms cold, 66–75 ms unchanged warm and 205–232 ms for a one-function edit. One final arm exceeded the quiet window and was excluded; there is no balanced feedback speedup claim. The separate CtCollect experiment was inconclusive and remains private.

For the constructor correction, 211.51/28.11-second full compiler constructions are exploratory observations, not controlled before/after results. The candidate reproduces itself byte for byte. All 443 compiler tests, native managed-pointer checks, scoped ordinary bytecode/SLJIT controls and both new regressions pass. Negative controls build and then fail at the intended assertions. A stronger optional-array case still fails on both baseline and candidate; it remains outside this correction.

## Complete CI and next experiment

No controlled complete-CI improvement is established. The historical same-inventory hosted comparison was 42m22s → 43m19s wall latency and 64m39s → 68m20s summed runner work. The original #1330 30m18s run had a smaller effective platform inventory and is not equivalent acceptance evidence. The preceding published `d4451f6b` run `41563501452` has Linux core and macOS selector failures; its completed-job costs are recorded below, while the last Windows snapshot remained running. Selector intervals overlap; they must not be summed as critical-path latency.

The completed macOS job took 35m20s: sequential Release and Debug product steps account for 27m14s, with 305 C cache misses and no hits. Its 6m52s selector step failed during native precompilation of a graph-c helper, before launching the selector inventory. The exact diagnostic is an ignored error result at `Process.chibi:26:15`. These are failed-run costs with different cache state, not native-build execution time or a same-input performance comparison. Completed Linux logs now confirm the graph-driver precompilation error at the same Process.chibi site, followed by the literal core watchdog expiry (exit 124). The native phase passes normalized stage-three/four equality and both command parity checks, then fails later at native_cli/Main.chibi:89:16 on an ignored error result. Linux job wall is 85m19s (core step 43m57s; native step 40m08s). Source comparison with root450 confirms Process is fixed and native_cli/Main is still unchanged; its owner is correcting it. Both cross-platform module jobs downloaded only the two runner binaries, lacking modules/program manifests, so their failures precede linking/runtime. Windows self-host is still running as of 03:39:22 UTC and remains separate from those module failures. The workflow checks PR merge `ab045bfea97a338d6fd457e333a8fd1b660d1773`, rather than the head object alone; inspected owning blobs match d445.

Next: run four balanced compiler and complete 443-test pairs plus balanced cold/warm/edit feedback on immutable original `4d23` inputs, normalized A=`21a367ae…` and B=`7de54a35…`, LLVM 22.1.8, explicit optimization and jobs=4. Every rebuilt original compiler must reproduce A's bytes. Helpers are prepared under `pointer-construction-4d23/controlled-performance/`; execution is held for an agreed quiet Mac window. Then validate the exact final composition with fixed-point equality, Driver parity, Graph parity and fresh platform CI. Before a lookup index, count residual requests and row visits by caller.

The prior-art investigation remains [documented separately](compiler-comparison/comparison.md): Zig's canonical identities, Odin/Rust's retained semantic facts and explicit ownership, and Goose's scoped lookup inform the small code changes. Goose uses C/TinyCC. No new backend, cache, test runner or incremental framework is part of this correction.
