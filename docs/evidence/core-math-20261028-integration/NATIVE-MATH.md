# Focused native math diagnostic, 2026-10-28

All five requested selectors passed on fresh native code: 17 correctness cases / 39,367 assertions, plus the opt-in benchmark’s 1 case / 2,021 assertions. This fills the native Math runtime/fixture gap through an authorized focused build. The normal full asset/Graph/installed-SDK gates and Math publication hold remain open. The original 8b 33-versus-27 input failure is preserved separately; this route does not repair it.

Authoritative Math source stayed clean at `6990373667fc211faf828035ab9d4bb051f085bd`, tree `c4381974226adbf5291506e44548f18a3571bfcd`. It differs from 8b only in three documentation/evidence files. The new task-private snapshot reports `699037366-dirty` and changes only the optional asset-host condition in `test/language/CMakeLists.txt` to `if(FALSE AND TARGET Chibi::asset_compiler_host)`. Helper diff SHA-256: `69062a481869920fc8585c1531556f5dda7dee9d422abde4f9d3ee70f108e163`.

All 3,470 tracked files were hashed before and after. Every other byte matched authoritative 699, including selected tests and dispatch registration, compiler/SDK/runtime/authoring code, six Math sources and both checked bootstrap outputs. No artifact was substituted or regenerated, no sibling source was copied, and no production code, guard, warning or assertion setting changed.

The standalone configuration used Clang 22.1.8, CMake Release, compiler SDK ON, native/desktop SDK OFF, four jobs and no sanitizers. Selected test compilation retained `-O3 -DNDEBUG -std=gnu23 -arch arm64 -Wall -Wextra -Werror -UNDEBUG`. Configure took 4.321 s; the fresh `test_language` build took 46.300 s. It began with no driver or objects and produced 486 actual compilations and 486 objects. Neither the dependency graph nor the verbose build invoked the optional asset-host consumer, asset/facade generation or complete host executables. No complete bootstrap/self-build ran.

Driver SHA-256: `5ac1bcfc561ad81646777ef4f6bbcb4ee7745c2af58c40e4209fabe09e9ff840`. Prepared format: 26. The checked closure remained `bf582baf6aa06496b15e6f715f27cab3d5afbd0fb68feb59483c85d7481cc64a`.

| Existing selector | Passed cases/assertions | Outer test s |
| --- | ---: | ---: |
| `--package-math` | 6 / 4,132 | 1.153 |
| `--package-compiler-chibi-imports` | 2 / 17,453 | 31.459 |
| `--package-compiler-chibi-imports-compiled` | 2 / 17,024 | 28.784 |
| `--native-calls` | 7 / 758 | 0.510 |
| `--package-math-benchmark` | 1 / 2,021 | 0.426 |

The Math log reports 29 authored tests on BYTECODE and 29 on SLJIT. Their preparation times were 23.610 ms and 22.517 ms. Imported Math checks require positive matching backend entries, zero bytecode entries on compiled calls and zero inspection fallback. Both compiler modes passed the existing importing-package corpus, including the nine reachability additions and two Math samples. Emitted Math sample calls execute on BYTECODE and SLJIT with the same strict target-entry/no-bytecode/no-inspection-fallback checks. The general compiler fixture permits its existing size-based bytecode fallback exception and does not print numeric fallback counts; this is not a blanket zero-fallback claim for the compiler itself. Native-call controls also include the intentional inspected VM path.

The benchmark opt-in is its explicit selector; there is no enabling environment flag. No environment override was set, so it used 128 loop steps, 16 invocations per trial, three trials per backend/body, and one warm-up invocation per body. Invocation timing includes reusable storage reset and binding. The table gives median timing and counters per trial; allocation/footprint counters were identical across the three trials in each row.

| Backend/body | Median ns/step | xalloc calls | Requested bytes | Arena blocks | Activation bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| BYTECODE ImportedLerp | 599.121 | 0 | 0 | 0 | 7,808 |
| BYTECODE InlineLerp | 254.883 | 0 | 0 | 0 | 2,256 |
| BYTECODE SpringValue | 13,597.168 | 23,136 | 5,474,048 | 112 | 7,480 |
| BYTECODE SpringFields | 13,954.590 | 21,088 | 5,179,136 | 112 | 6,528 |
| SLJIT ImportedLerp | 103.027 | 0 | 0 | 0 | 5,088 |
| SLJIT InlineLerp | 5.371 | 0 | 0 | 0 | 320 |
| SLJIT SpringValue | 3,454.101 | 20,624 | 3,024,128 | 16 | 63,768 |
| SLJIT SpringFields | 3,421.387 | 18,576 | 2,729,216 | 16 | 63,632 |

Benchmark preparation was 24.167 ms for BYTECODE and 24.493 ms for SLJIT. Both spring forms passed the independent numerical oracle and matched one another. The spring forms measured nonzero allocations. This run establishes neither a general allocation budget nor their cause. The counters cover xalloc wrappers and arena blocks, not every process allocation. Arena, activation, host and managed footprints are separate sampled measures. Full per-trial data, host/managed/arena footprints and exact timings are in [benchmark-counters.json](benchmark-counters.json). Benchmark measurement does not print execution-entry counters; the strict runtime backend evidence comes from the correctness/import checks above.

There were no red results in this focused route. The earlier normal 8b build remains recorded in the [integration checkpoint](RESULTS.md) and its companion JSON: exact reuse status 3 for 33 captured inputs versus the task artifact’s 27, followed by directed cancellation at 79.743 s. The compiled-Wasm compiler limit remains a separate red result from the earlier 567 boundary. Nothing here clears #791/Math SDK publication after #795, or substitutes for the coordinator’s final clean normal Graph, reviewed matching artifact, complete installed-SDK and applicable platform gates. All scratch, binaries and prior failures remain preserved.

[native-math-focused.json](native-math-focused.json) records exact commands, source/object/binary hashes, build/test times and log hashes. `preflight.json`, `tracked-files-sha256.json`, `dependency-and-flags.json`, `build-identity.json`, `objects-sha256.json`, the helper patch and seven command receipts retain supporting evidence in the task validation directory.
