# Four focused Graph regressions

The unchanged `graph-engine`, `graph-evidence`, `graph-wasm` and `graph-os` entries passed on Linux at code `083e82408a337787cfd9f08c1fff1113a1884831`. Their 778 authored lines and every assertion remained unchanged. Independent source/control review preceded compilation; separate actual-artifact review preceded execution; final review checked the results and preserved inputs. These are four focused selector results, not full-suite, browser/Wasm runtime or PR acceptance. PR #1331 remains held.

## Coverage

- Engine: profile/target validation, current-compiler companion selection, native identity ownership, source add/edit/delete cache behavior, malformed receipt rejection and mirrored-file cleanup. Its real shell/find/cat step executed eleven times, with the original `.calls` assertions checking that intervening cache hits started no child.
- Evidence: compiler/link/native command provenance, live file stamps, missing/stale/source-inventory rejection, sanitizer-flag and symbol-table validation. Compiler/object bytes in these cases are test data; no C compiler was invoked.
- Wasm: explicit target selection and command planning, including two real `uname -m` calls. No Emscripten build or Wasm program ran.
- OS: exact file bytes including NUL and invalid UTF-8, empty-file round trips, distinct private directories, publication/rename, cleanup and expected missing-path failures.

## Actual results

Four explicit-output O2 compilations used the admitted current compiler and pinned LLVM, with two workers and CPU affinity 0,1. They took 5.610, 4.437, 5.282 and 1.024 seconds respectively, all exit 0 with empty compiler logs. The complete owned compilation phase took 18.114 seconds within its original 180-second total limit. No output executed during this phase.

The separately accepted run phase executed the four ELF files serially from the isolated source directory. Engine took 0.0536 seconds, Evidence 0.0173, Wasm planning 0.00259 and OS 0.00133. All returned 0. The complete phase, including full input/output checks, took 1.880 seconds within its original 60-second total limit. Both limits included five seconds for owned cleanup and were never reset or extended. These are correctness-run timings, not benchmarks.

Engine's 2,016-byte combined log contains exactly eight intended invalid project-version diagnostics followed by four invalid prepared-writer diagnostics. The other three logs are empty. Final independent review checked the exact text/order rather than treating all diagnostics as failure or filtering them out.

Both owners reached authoritative ECHILD with no errors, signals or surviving children. Final evidence writes were also checked against the original deadlines, separately from the controller's status field. Runtime evidence completed with 58.120 seconds remaining.

All 4,072 authored source entries, the four executable byte/stat identities, fourteen compilation evidence files and tool/loader/Python inputs remained unchanged. Only the four intended binaries remained from compilation; runtime fixtures and private scratch were empty. No cached test output, broader fixture suite or replacement assertion was used.

## Evidence and remaining gates

Independent final admission SHA-256: `065f7222c64e8fcd41470f492010da613914302daf5814081743846a21faff43`. Exact executable and log hashes are recorded in `graph-regression-results.json`.

Verified archive `chibi-graph-regressions-validated-20261104.tar.gz`: `f86d3f2dbe31b71f23d3283c1b7829df1311c507168ccccc0e7d3e2d1254a093`, 17,510,843 bytes, 4,137 regular files and one preserved source symlink. All member bytes, modes and link targets were reread. The archive contains copied source, controls, binaries, receipts and independent reviews; external tools remain separately pinned.

Remaining gates include graph-c/build/discovery, ordinary VM/native OwnedRecords parity, current C-driver integration, actual Windows execution, the separate pointer-view WIP and exact-head integration/merge review. The old terminal-unknown bootstrap and excluded union leak investigation were not rerun.
