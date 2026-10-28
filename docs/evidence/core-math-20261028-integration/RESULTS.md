# Math and collection reachability integration, 2026-10-28

Selected native reachability controls and the full current-source native bootstrap passed in task-owned snapshots. Clean Math sources passed authored checks on BYTECODE and WASM. A later [focused native Math checkpoint](NATIVE-MATH.md) passed all 29 authored tests on BYTECODE and SLJIT, both compiler import modes, native-call controls and the scoped benchmark. That isolated driver omitted only the unrelated asset-host fixture; it did not repair the original ordinary-build input mismatch. The Math SDK publication hold and final normal Graph, reviewed-artifact and installed-SDK gates remain in force.

This checkpoint records frozen execution boundaries. The [companion JSON](integration-results.json) contains full commit/tree IDs, exact commands and environment, source/artifact/binary identities, receipt paths, and saved log hashes. During assembly, all 45 distinct command-log hashes were rechecked. Times below are outer elapsed seconds, rounded to three decimals; configuration, production and tests remain separate.

| Source boundary | Execution scope |
| --- | --- |
| Pure `2a76e61a8` | Clean source; original normal-build failure and task-local recovery/production-writer proof |
| Pure `3ca0b4a0` | Two-artifact dirty snapshot; native focused controls and full bootstrap/fixed point |
| Pure `60b28e573` | Two-artifact dirty snapshot; only the two repaired fixture controls rerun |
| Math `5672369ff` | Clean source; selected Wasm driver checks, including both green and red results below |
| Math `8b1419414` | Clean source for Wasm snapshot/Math subset; separate two-artifact dirty snapshot for the blocked native build |

The native dirty snapshots replace only `tools/compiler/bootstrap/bootstrap.json` and `compiler.chibip`. All other tracked hashes matched the authoritative source: 3,460 files compared for pure 60b and 3,467 for Math 8b. These are local diagnostic proofs, with honest `60b28e573-dirty` and `8b1419414-dirty` revisions. The authoritative checked artifacts stayed unchanged. The clean Wasm checks used no substituted artifact pair.

The task artifact was generated on clean pure `2a76e61a8bc21ee908f1306d607c8ddd93432b48`, using the existing checked seed, normal recovery and the unmodified production writer. The real failed Generate request was copied byte-for-byte as the seed request. Its 27 source paths, order, complete source texts, `/project` and `/sdk/core` roots, `debug=false`, and full target matched. The writer verified inventory, target and self-reproduction. Later source boundaries retained those 27 compiler texts and target, so the original generation attribution remains `2a76e61a8`.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Unchanged checked #694 closure | 20,780,474 | `bf582baf6aa06496b15e6f715f27cab3d5afbd0fb68feb59483c85d7481cc64a` |
| Task-only production-writer closure | 20,805,242 | `ce56a52ba79e51fe538eac447bd1caf2d233d5250f2a990e56f9d4ca0e9b3356` |
| Task-only production-writer manifest | — | `216d418a988dc0e6eae622b391585b5d10ab030d11d9d4e20492926d673fa2f0` |

These proofs use prepared format 26 and bootstrap version 7/minimum 6. No pre-#694 prepared-25 timings or compiler proofs transfer here. The bootstrap selector’s exported compiler later matched the task closure byte-for-byte; its request retained the same 27 texts, roots, order, debug flag and full target despite different JSON serialization. The export remains task scratch and is not publication.

Native standalone configuration used Clang 22.1.8, CMake Release, compiler SDK ON, desktop/native SDK OFF, and four build jobs. All original guards and warning settings stayed in place.

| Native preparation | Configure s | Build/producer s | Result |
| --- | ---: | ---: | --- |
| Clean pure 2a normal build | 5.012 | 264.786 | RED: source mismatch caused fallback; existing 240,000 ms guard ended asset generation |
| Clean 2a current-source recovery | — | 230.071 | Passed |
| Clean 2a production writer | — | 135.020 | Passed; task-only output above |
| Dirty 2a matching-artifact build | 8.682 | 200.992 | RED: asset generation reused successfully, then four strict `pin_string.owner` initializer errors |
| Dirty c3 partial initializer migration | 7.655 | 14.161 | RED: further strict initializer errors |
| Dirty 3ca complete common migration | 7.244 | 35.706 | Linked native language driver |
| Dirty 60b fixture-control update | — | 5.858 | Incremental build passed |
| Dirty Math 8b native diagnostic | 8.202 | 79.743 | Stopped after confirmed inventory mismatch; no driver linked |

The two native driver warning failures were preserved before applying the independently reviewed common initializer migrations. The initial tools command also returned exit 2 after 30.449 s because the minimal SDK configuration had no `chibi-compile` target; `test_tools` had built. An explicit `test_tools` build later passed in 2.797 s. This configuration did not provide the separate native published-docs CLI control.

For the following table, “cases/asserts” identifies the test result, not elapsed time. Pure controls contain no Math compiler samples. Native imports executed BYTECODE and SLJIT. Both stage-one compiler modes passed, with emitted sample execution in BYTECODE and SLJIT. The bootstrap selector exercised its compiled native compiler stages using SLJIT.

| Native pure test | Source | Passed cases/asserts | Test s |
| --- | --- | ---: | ---: |
| compiler-fixture | 3ca-dirty | 9 / 414 | 1.849 |
| package-tests | 3ca-dirty | 5 / 271 | 0.245 |
| package-core | 3ca-dirty | 6 / 14,762 | 41.243 |
| package-resolution | 3ca-dirty | 8 / 164 | 0.483 |
| docs-markdown | 3ca-dirty | 14 / 1,778 | 1.921 |
| compiler imports, compiler BYTECODE | 3ca-dirty | 2 / 15,281 | 38.282 |
| compiler imports, compiler SLJIT | 3ca-dirty | 2 / 14,888 | 32.110 |
| full compiler bootstrap/fixed point | 3ca-dirty | 1 / 31,262 | 580.831 |
| controlled bootstrap unit fixture | 3ca-dirty producer | 18 / 204 | 29.730 |
| package-imports repaired control | 60b-dirty | 18 / 1,530 | 1.710 |
| package-snapshot-chibi repaired control | 60b-dirty | 16 / 286,093 | 0.661 |

Two original 3ca controls were red: imports had 17 passed/1 failed and 1,473 assertions in 2.158 s; snapshots had 2 passed/14 failed and 48,769 assertions in 0.376 s. The import fixture lacked a caller-owned managed heap. The snapshot fixture rejected canonical empty text represented by NULL. Reviewed fixture migrations retained value/counter assertions and ownership checks for present descriptors, arrays and records. Only those two failed controls were rerun at 60b. The full bootstrap result remains pinned to 3ca and was not repeated for these C-fixture-only changes.

The first full bootstrap invocation failed after 570.316 s because the agent had not created the optional `CHIBI_BOOTSTRAP_SEED` export directory. Compiler byte equality and zero copying-function assertions had passed, but later stage-two samples/literals had not run. Creating the task-owned directory and repeating the same selector and limits produced the 580.831 s, 1/31,262 green result above, including the remaining checks. The setup failure remains recorded. The controlled 18/204 unit fixture verifies reuse guards using controlled sources; current compiler provenance comes from the separate production writer and full bootstrap proof.

The complete common initializer migration allowed the clean Math 567 Wasm build to pass in 22.791 s. Earlier clean Wasm builds remain red: ea596 took 52.063 s and failed at the four original initializer sites; 519c took 29.421 s and failed at further initializer sites. These were build failures before the selected runtime checks.

| Clean Math 567 Wasm check | Result, cases/asserts | Test s |
| --- | --- | ---: |
| package-math | Passed 6 / 4,132; 29 authored tests each on BYTECODE and WASM | 0.788 |
| compiler-fixture | Passed 9 / 414 | 2.503 |
| docs-markdown | Passed 14 / 1,778 | 4.053 |
| compiler imports, compiler BYTECODE | Passed 2 / 17,453 | 60.268 |
| row-array | Passed 20 / 17,853 | 1.285 |
| manual package-imports | RED: 4 passed / 14 failed / 925 assertions | 0.461 |
| package-core | RED: 5 passed / 1 failed / 12,770 assertions | 47.104 |
| compiler imports, compiler WASM | RED: 0 passed / 2 failed / 15 assertions | 22.859 |
| `--packed` | RED: unsupported flag; no suite executed | 0.665 |

The passing BYTECODE compiler lane includes Math’s imported-values and rejected-f64-narrowing samples. Its Math output executed on BYTECODE and WASM; the compiled Math path asserts positive WASM entries and no bytecode or inspection fallback. The compiled-WASM compiler lane failed during compiler preparation with `Wasm function exceeds compiler limits`, as did package-core’s stage-one compiled compiler control. They do not provide successful compiled-WASM compiler evidence. The manual package-imports selector hardcodes SLJIT and is not Wasm-admitted; it failed with backend-unavailable errors. The supported `--row-array` check ran separately from the mistaken `--packed` invocation and reported BYTECODE/WASM entries, zero SLJIT entries and zero fallback for its reported calls.

At clean Math 8b, the incremental Wasm build passed in 13.159 s. The focused rechecks passed: snapshot 16/286,093 in 0.960 s and Math 6/4,132 in 0.715 s, with 29 authored tests on each of BYTECODE and WASM. The Wasm driver SHA is `769cbecd37804e216e424a7d859f729f1b20b1bf7154866a015f44dd1dbac41c`. This is a passing subset, not a full Wasm-suite result.

Native Math’s recorded 27 compiler texts and full fresh native target matched the task artifact. Its actual ordinary Generate request nevertheless contained 33 sources: the original 27 plus all six Math/ease source and test files. Exact reuse returned status 3, confirmed separately in 2.009 s without writing an output. As instructed, the build was cancelled after 79.743 s following that miss; its underlying exit was -15. This was neither a production timeout nor a Math runtime failure. `test_language` never linked, so native package-math, both Math compiler-sample lanes, native-calls and the opt-in Math benchmark were unrun in that attempt. Benchmark counters were unavailable there, not zero. The later focused checkpoint above supplies separate native evidence. The diagnostic did not copy #795 generator changes or alter guards, limits, source contracts or authoritative artifacts.

The original evidence heads precede accepted #796 `3e2b28dc19fb21961ae06fe99aeb5a8dede35bf0` (protocol 13, prepared format 26). The canonical 18-file fixture packet [#800](https://github.com/waldnercharles/chibi/pull/800) subsequently merged as `afcd777ba1741dfa3f1d890c90d5310a9415e331`, after normal CI, all three compiler self-build platforms and separate focused native controls passed. Its own source and runtime evidence remains separate from the frozen Math runs.

A source-only forward merge consumed that accepted checkpoint: pure `f9e8b637736e8a3139553912865c087e4c07edcd` contains the reviewed eight-file reachability change over accepted main, and Math `2726b662af07b123122ebf8eb288fe0853ba6a21` merges that prerequisite. Independent review verified all 17 owned Math implementation paths and five unaffected registration paths match the executed `699037366` source. The package-compiler fixture changes only to the canonical accepted host-layout assertions and explicit null/zero initializers. No new runtime or broad-build result is claimed for these merge commits.

The coordinator orders pure #790 after the accepted EmitBody/common #800 boundary and before #795. Pure reachability retains the complete private 27-source compiler input and has no current Generate/Driver source-role dependency. #795 retains library/Generate/SDK ownership. Math #791 and SDK publication remain held after #795; final matching-artifact, clean normal Graph, complete installed-SDK and applicable platform checks remain ordered gates. The Spring benchmark records nonzero allocations in its measured loop bodies without assigning a cause or accepting a budget.

Both dirty snapshots, their task-only artifact/seed pairs, original failures and logs remain preserved at the coordinator’s request. The report preserves these measured boundaries; authoritative artifacts, guards and implementation sources were unchanged during its assembly.
