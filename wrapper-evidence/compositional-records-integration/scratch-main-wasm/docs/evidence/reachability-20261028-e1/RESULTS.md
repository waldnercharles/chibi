# Collection reachability on the E1 runtime, 2026-10-28

The reviewed reachability implementation and its matching checked compiler are integrated with accepted main `498f1b9a`. Focused native, Wasm, bootstrap, documentation and Graph checks pass at the source boundaries below. The original deep-path Graph build still fails the existing argument-byte guard; the short-path build passes. Fresh CI for the final combination and coordinator review remain required before merge. Math #791 remains separate.

Every project package is a checked root. Collection packages enter through direct, transitive or blank imports; a collection-only snapshot selects its first input as the root. All supplied syntax and import graphs are still validated. Internal graph indices stay intact during construction; only public native enumeration and emitted closure rows are compacted.

## Frozen boundaries

| Boundary | Commit | Tree |
| --- | --- | --- |
| Reviewed source over accepted Body/E1 main | `d1315047c0a533a3848569a39af83a33c98a8799` | `e354bc3263c912bb928ade409614ae9663ee30d2` |
| Matching checked pair and main execution checkpoint | `bad6bd81d120bbdd2cd2c5cbcb35ec7e47fd57b0` | `09eba7d216264b9ed3461839369a750961127d4b` |
| Accepted scalar fixtures and SDK version authority | `498f1b9aebca18597f1ce93fd292fd0739b2e39a` | `17736bb7c33ee2c5ad649322c044ba96f24924e3` |
| Final source integration | `4c9bf0a8c6ab576495c581fe14243dfc44ae116a` | `3b91f2176e2aa3882e1fcb3538a2b2ee51696661` |

The final integration preserves all 11 accepted incoming paths exactly. Its eight feature paths and two compiler artifacts are byte-identical to the reviewed `bad6` versions. All 27 compiler source paths, bytes, lengths, hashes, ordering and package membership still match the captured request and generated manifest. No compiler regeneration followed the fixture/metadata merge. The full feature/pair diff against accepted main has SHA-256 `259d7ce7669f0444ed2cbfaca1bc4cf01217ddb70075f26d9ce9f55fc5ed1059`.

The [structured receipt](execution-results.json) records exact commands, source states, environment, individual timings and all 52 completed command/log hashes. Results from `bad6` remain attributed to `bad6`; the final version/resource controls have their own `4c9` receipts. This forward integration replaces the historical source of [#790](https://github.com/waldnercharles/chibi/pull/790), whose frozen head and earlier receipts remain intact.

## Compiler production and identity

The accepted E1 runtime executable was verified against `tools/compiler/runtime.json`: macOS ARM64, 1,367,136 bytes, SHA-256 `7526a0a611e4e4be562e7eb7b91633b61238c6d7fe762429c0139f2fa637951c`. A fresh source-built host reports the same six target-fact values. The runtime pin, prepared format 26, bootstrap format 1, version 7, minimum 6 and entry `0x140a9546d9e8c296` are unchanged.

Ordinary changed-source recovery passed in 216.352 seconds. It compiled the current source and verified that the resulting compiler recompiles those sources to identical bytes. The initial unmodified standalone SDK/language build then failed in 339.110 seconds at the existing 240,000 ms asset-generator deadline, before writing its request. That failure remains failed.

To obtain the documented seed/request export without that build dependency, the existing full bootstrap fixture ran in an exact task-private clone of `d131`. Its sole difference is the [recorded optional asset-host CMake condition](bootstrap-driver-isolation.patch). Independent review compared all 3,462 tracked paths, all 485 fresh objects and the selected header dependencies. Compiler/runtime/test bytes, strict Clang 22 Release flags and assertions were preserved. The private driver omits the optional asset-host facade; this route does not establish ordinary full-build acceptance.

Private configuration took 6.416 seconds and compilation 33.734 seconds. The full fixture passed **1 case / 31,262 assertions in 398.359 seconds**, including fixed-point equality, the unchanged zero-copying-functions guard, stage-two samples and literals. Its actual captured request is 1,351,502 bytes, SHA-256 `f83e51dbe6e066e4cdc13b4f2d47bafbfd073b944df22e7b3dacefccd3c89ea4`; it retains `/project`, `[core]`, `[/sdk/core]`, all 27 ordered source texts, `debug=false` and all six target facts. No request was reconstructed or borrowed from Body's older proof.

The unmodified production writer ran in the authoritative clean `d131` worktree against that actual export and passed in 90.672 seconds. Its reviewed output is:

- Compiler: **20,792,064 bytes**, SHA-256 `27d8f3d0568e442e1a70b167c04ba97c671ea0d0b1e85e3a7493d88fc59bb912`.
- Manifest: SHA-256 `2204ba9cce1e68ab95d8a5b7ed6158ba4d1bbae864f0dee624aaa550672c80f1`; producer revision remains clean `d1315047c0a533a3848569a39af83a33c98a8799`.
- All 12 modules retain prepared 26 and the accepted ABI. Eleven non-root payloads are byte-identical to accepted `e93318a3`; only `project:package_compiler` changes. JSON target-key ordering changes, but every target value is identical.

Normal recovery of the written artifact passed in 75.805 seconds. Exact captured-request reuse passed with exit 0 in 0.955 seconds, without compilation. Independent recovery, fixture export, writer output, written-artifact recovery and exact-reuse output are byte-identical. Independent artifact review cleared the pair before it was installed and committed at `bad6`.

## Ordinary builds and execution at bad6

With the reviewed matching pair, the unmodified standalone native SDK/language build passed in **116.315 seconds**, including the optional asset-host facade and ordinary generator. Its trace reports compiler loading in 0.809 seconds, then preparation and successful generation; bootstrap reuse reports status 0. Configuration was measured separately.

The full ordinary Graph Release build first failed in **194.516 seconds** at the deep output path under `.chibi/integration/pure-807/release`. Its `test_tools` link has 502 arguments totaling **71,540 bytes including NULs**, exceeding the unchanged 65,536-byte host guard. The host rejects zero-based argument index 461 (the 462nd argv entry) before launch. All 1,206 launched steps succeeded; this is not a compiler or linker diagnostic. The original failure remains recorded.

Changing only the output directory to **`/tmp/p790-hqmks0bc`** produced a passing full Graph Release build in **192.335 seconds**, with the same source, profile, targets, host and limits. The actual successful per-binary record retains the same 502 arguments at **29,006 bytes**; its archived SHA-256 is `9b58c09dc88ab0ae436dbf92a98ea1adfc888bc1bda90776f625b8b62d1b9ec5`. This establishes the short-path build, not original-path parity. The guard was not widened. The Graph host's compiler-load trace is 0.759 seconds.

| Completed execution at bad6 | Passed cases | Assertions |
| --- | ---: | ---: |
| Native imports, snapshots, package tests/resolution/files/core, compiler fixture, Markdown and record publication | 91 | 305,391 |
| Native compiler imports in both modes, compiled compiler and sharing | 6 | 49,411 |
| Fresh Wasm compiler imports in both modes, compiled compiler and sharing | 6 | 49,411 |
| Wasm snapshots, compiler fixture and core strings | 31 | 301,269 |
| Controlled bootstrap reuse unit fixture | 18 | 204 |
| Published documentation, after normal resource staging | 3 | 58 |
| Graph command comparison, debug options and batch controls, with canonical reference | 5 | 4,002,151 |

Native totals are **13 selectors / 97 cases / 354,802 assertions**. Wasm totals are **7 selectors / 37 cases / 350,680 assertions**, including the previously blocked compiled-Wasm compiler lane. Core strings run 190 authored tests per selected backend. The general compiler fixture retains its existing size-based bytecode fallback exception; these results do not establish blanket zero-fallback compiler execution. Record publication exercises preparation/publication, not authored execution. The controlled reuse unit deliberately uses a tiny source fixture and is separate from current-source fixed-point proof. No allocation or general throughput claim is made.

Three setup failures remain preserved with their corrections: an unregistered Wasm policy name exited 2 without running a test, then the existing Node language dispatcher passed; published docs first failed because its resources had not been staged, then the owning normal `chibi` target staged them and all three cases passed; Graph command comparison first used a `/tmp` CMake reference that canonicalized to `/private/tmp`, then a fresh canonical reference passed all five cases. Assertions, guards and test selections were not weakened.

## Final integration controls and remaining gates

On clean `4c9`, the tooling refresh passed in 100.311 seconds. It also reran normal asset generation, with reuse 0 and no bootstrap fallback; it was not solely a C recompilation. The checked compiler pair remained unchanged. That refresh is distinct from the earlier normal `bad6` proof.

The incoming shared-version/configuration-invalidation, resource-installation and language-corpus controls pass **3 cases / 20 assertions in 47.163 seconds**. The incoming runner/scalar-transport controls pass **64 / 900 in 94.206 seconds**. PowerShell transport explicitly reports unrun because `pwsh` is unavailable locally; its enclosing case's success is not PowerShell execution coverage.

Fresh exact-candidate normal CI and three-platform compiler self-build remain required on the replacement PR, followed by coordinator review. Full local native/material/project suites, full Wasm, GPU execution, Windows execution and PowerShell transport were not run here. Historical recovery-dispatcher deadline failures remain in their original Body/earlier receipts; no new success for that dispatcher is claimed. No unpublished feature, new runtime seed or limit increase entered this candidate.

Detailed scratch, original logs, private source/driver, seeds, target facts and object evidence remain under `.chibi/integration/pure-807` in the pure integration worktree. The short build and both original/canonical CMake references remain in `/tmp`/`/private/tmp` for review. Math's separate worktrees and queued compiler/SDK inputs were not changed by this integration.
