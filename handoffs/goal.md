# The goal (owner, 2026-10-30)

Set by the owner with `/goal` in the parent session. This replaces the "not now" list in
`focus.md`: porting the engine away from C, or archiving it, is now in scope.

> Chibi builds and compiles itself with no first-party C, C++ or CMake, Zig/Odin style, with Edit and
> Continue. LLVM is the backend: its libraries and lld are allowed, as in Odin. Third-party libraries
> are linked as prebuilt vendored archives, as in Odin's vendor/.

Every item must hold on one origin/main commit, shown with command output or CI run URLs:

| # | Item | Owner(s) |
|---|---|---|
| 1 | Native self-hosting: the native compiler (no C VM, no C front end) builds stage 2 and a checking stage 3; byte-identical | sdk-cmake: W7 arrays, which block 491 functions. core-math: W12, then W6 text, W10 pointers, census reruns, then leads W22 (native stage one and two). builtin-math: W14 as `foreign import` |
| 2 | A CI job with no cmake, gcc, cc or clang (LLVM and lld allowed) goes from a clean checkout through stage 1 and 2, then links and runs a Chibi program against vendored libraries | CI keeper (W23), after item 1 |
| 3 | `chibi build` builds everything; no CMakeLists.txt, *.cmake, Makefile or Python build/test tooling outside archive/ | lang-ux: Makefile and Python (`.github` Python removed in 702f8b4aa). strings-ints: all CMake |
| 4 | No first-party .c/.h/.cpp/.m/.mm outside archive/ and third_party/, except pass-through C++ shims; vendored libraries bound with `foreign import`, never compiled by us | portable-libs: archive the engine C the toolchain doesn't link (render, physics, audio, UI, scenes, player, Studio services). coordinator + host-clock: src/language, src/authoring. old-formats: src/assets and ckdl done (#919, #921, #922); now ports the compiler's C test drivers (test/language/*_chibi.c) to Chibi `@test`. Toolchain C (src/execution, src/host, src/toolchain, src/types, src/support) is archived once item 1 lands |
| 5 | Odin-style parametric types, full compile-time execution, `foreign import`: documented in docs/code-language.md, tested in CI | generics (types, compile-time); builtin-math (`foreign import`) |
| 6 | Edit and Continue: a long-lived loop picks up edited code at a safe point with its state intact, without rerunning Main; a test proves it | errors |
| 7 | Seed compiler as a Release asset pinned by SHA-256, not committed; recovery from it passes | CI keeper |
| 8 | Full CI green on Linux, macOS and Windows | CI keeper; whoever breaks a platform fixes it |
| 9 | Clean native self-build faster than the VM-hosted one | parent coordinator measures |

## Pieces with owners assigned here

- **Native build runner** (items 1, 3, 4): `chibi build` compiles the project's Build.chibi
  natively and runs it, as `zig build` compiles build.zig. That replaces chibi-build-host (C),
  which runs build graphs on the VM. Owner: records, after #911 and the linear record validation.
- **`core:os`** (items 1, 3, 4): files, directories, processes, environment, time and hashing as a
  Chibi package over libc through `foreign import`, in the style of Odin's `core:os`. The native
  compiler driver and the build runner use it instead of the C `build.*` natives. Owner:
  portable-libs, after the engine archive and once builtin-math's `foreign import` lands.

- **Native prerequisites for items 2, 4 and 6** (coordinator, 2026-10-30): function-pointer types (builtin-math
  slice 5), and native programs linking libc dynamically by default as Odin does, with static no-CRT as an option
  (builtin-math). Then `core:os` over `foreign import` (portable-libs). The native Edit and Continue host
  (errors) and the native build runner (records) both wait on these. No temporary C natives in the meantime.

## Baselines (2026-10-30, main 1a2703a7c)

- VM-hosted compiler self-build, CI job time: ubuntu-24.04 20m16s, macos-15 22m53s,
  windows-2022 26m13s (https://github.com/waldnercharles/chibi/actions/runs/41007870768).
- On main 4c51665e9, outside archive/ and third_party/: 1,084 C/C++/ObjC files (457,364 lines),
  38 CMake files, 1 Makefile, 23 Python files. third_party/ has 25 C files.
- Native census: 131 of 700 compiler functions pass. W7 blocks 491, W9 73 (since landed, #871),
  other forms 5.

## Rules

Merge first and fix main forward. Archive removed features under `archive/<path>` and don't restore
them. Breaking Studio, games and old formats is fine. No paid CI runners. Never touch
/Users/cwaldner/Personal/chibi/chibi. Ask the owner only for decisions that are theirs. Coordinate
peer-to-peer through `sessions.md`.

## Evidence so far (re-check on the final commit)

- **CI workflow fully green on 18b304f73** (run 41204042545): Linux and macOS engine jobs, Native scalar modules on
  macOS and Windows, Graph SDK, clang-format. Compiler self-build (run 41204042606) green on Linux and macOS, red
  on Windows (frozen runtime, known).
- Assignments (coordinator, 2026-10-31 after the owner's "continue"): core-math, the native driver now; sdk-cmake, W10
  lead; builtin-math, a native foreign CI test, the C-callback adapter and native-build parallelism; old-formats,
  the macOS native target; strings-ints, the Windows native target; errors, a native edit-while-running EnC test;
  host-clock, port Chibi-subject C tests from test/tools and test/language; lang-ux, the PTY flake and an item 3
  `chibi build` proof; portable-libs, the shared asset cache and the core:os seam; CI keeper, the bootstrap-recovery
  split and a seed refresh for records.
- Native targets (coordinator, 2026-10-31): `chibi build --native` runs only on Linux today ("native-aot: Linux and
  absolute input/output paths are required", tried on macOS at 7cbab2822+). Items 1 and 8 need native compiles on
  every CI platform, so beside Windows (COFF, Win64 ABI, lld-link) macOS needs a native target too (Mach-O, AArch64
  ABI, ld64.lld). Both are unowned; schedule them with the native driver.
- Item 5, compile-time execution (coordinator, 2026-10-31): CI run 41186715558 on c018bb631, both engine jobs, PASS
  chibi-test-samples-comptime: comptime_records_are_built_read_and_updated, comptime_records_hold_numbers_and_bools_only,
  comptime_arrays_are_built_summed_and_indexed, comptime_for_loops_cover_ranges_arrays_and_exits,
  comptime_array_index_is_checked_against_the_length. Parametric types passed in run 41177771671. Native
  `foreign import` still has no CI run (samples SKIP on the VM).
- Item 8, Windows (coordinator, 2026-10-31): the Compiler self-build's Windows job is red on its frozen C runtime
  (dddc4c491; the record-array GC bug #1123 now triggers; Linux and macOS re-pinned in c0b176cc9, green in run
  41189821528). Decision: no Windows build route for the C runtime. Item 8 needs a Windows native target (COFF,
  Win64 ABI in llvm_native, lld-link, Windows seed, and core:os's Windows files); unowned until W22 lands.
- Item 1 census (coordinator, 2026-10-31, parallel census 030152602): 1,051 of 1,296 compiler functions pass the native
  producer (973 of 1,238 at #1037); remaining W10 pointers 197, W14 native calls 35. Compile-time records landed (#1123).
- Item 7 output (coordinator, 2026-10-31): Compiler self-build run 41177771657 on 2f3512802 (Linux job 124469260542),
  recovery from the pinned release-asset seed (then c6a9ddfcd): "16 compiler sources changed since revision
  c6a9ddfcd...; the artifact compiled a compiler of 22643970 bytes, sha256 587c60ee6a28b382cfd54c59a6a63cc0157e3b2ce7fbe85abf63246801cc5259,
  that compiles them to [itself]"; the step then checks that stage two reproduces stage one byte for byte. Green on
  macOS and Windows too. Locally: `sh tools/build.sh bootstrap-recover`.
- Item 6 progress (coordinator, 2026-10-31): on 2ae63cd3e, CI run 41181505601 (Linux engine job 124482974502) passed
  program_watch_patches_a_running_frame_and_keeps_state and program_watch_tracks_siblings_and_keeps_imported_state
  (`chibi run --watch` edits the source of a running loop, which keeps its state, and Main runs once), and the native
  package_compiler_native_edit_and_continue_keeps_state (a library swap keeps retained state; Main never runs). The
  native test builds both versions up front instead of editing while running; on the final commit the watch test must
  run on the native host, since the VM goes away.
- Census on main 2ae63cd3e (coordinator, 2026-10-31 08:30 UTC): **403** first-party .c/.h/.cpp/.m/.mm files outside
  archive/ and third_party/ (portable-libs' "about 197" was low); no .cpp or .mm left, so no C++ shims remain. CMake 0,
  Makefile 0, Python 0 (item 3 holds again). Tree-sitter archived (#1119).
- Item 5 state: parametric types documented (docs/code-language.md "Generics and compile-time values", generic
  records) and passing in CI (run 41177771671: exported_generic_functions, generic_records_specialize_per_application,
  ...). Compile-time execution covers integers, bools, arrays, calls and loops (#1118, samples/comptime), but not
  records yet (generics' next slice). `foreign import` is documented; its samples are SKIP "native only" on the VM, so
  no native foreign run in CI yet (builtin-math).
- Item 7 progress: Compiler self-build green on Linux, macOS and Windows on 2f3512802 (run 41177771657).
- Item 4 remainder (coordinator session, 2026-10-31 05:30 UTC): about 197 files, all VM, toolchain hosts, CLI natives
  and C tests. Each is replaced by the native compiler driver (item 1), `chibi build` running Build.chibi natively over
  core:os (records, portable-libs), or Chibi @test ports (old-formats, host-clock). Nothing else is archivable now.
- Items 1 and 9 harness: the coordinator session writes a `native-self-host` verb (stage 1 -> 2 -> 3, SHA-256s,
  per-stage times plus the VM-hosted time) around core-math's native stage-one driver.
- Item 7 progress: Compiler self-build green on Linux, macOS and Windows on 73a689581
  (https://github.com/waldnercharles/chibi/actions/runs/41167986622).
- CI time (coordinator, 2026-10-31): 544796abc runs the test selectors in parallel; dispatched run 41167203139
  (95fb3cc05) finished the engine jobs in 12 min (Linux) and 22.5 min (macOS), down from 45-minute timeouts.
- Item 4 count (coordinator, 2026-10-31 04:40 UTC, after #1080 deleted the stage-zero C front end): 491 first-party
  .c/.h/.cpp/.m/.mm files outside archive/ and third_party/ (588 at 03:15 UTC, 1,084 on 10-30).
- Item 7: the seed is a release asset (823473dbf, #940 fea7d308e). It's pinned by SHA-256 in
  bootstrap.json and fetched by tools/bootstrap/seed.sh. The Compiler self-build on fea7d308e fetched
  it and recovered both stages on Linux, macOS and Windows:
  https://github.com/waldnercharles/chibi/actions/runs/41025093174. Re-check on the final commit,
  after the seed refresh the CI keeper is running.
- Item 1: W12 and W13 merged (#950, imported packages and constants). W7 slice 1 merged (#964, 85991540):
  native arrays of numbers, bool and string. Then arrays of records (#970) and
  extend/slice/resize/contains/remove_index (#971). Census #968: 272 of 1,065 compiler functions passed; after the W7 slices #1005 and #1021,
  census #1037 shows **973 of 1,238** (it was 131 of 700 at 10-30 morning). Remaining walls (sdk-cmake, 10-31):
  W10 pointer/reference parameters 178 (core-math, splitting with sdk-cmake), W14 compiler_json_value_string 31 +
  json_string 2, `text_bytes` 29 (landed in #1021, after that census), W9 optionals 7 (sdk-cmake), W7 only `insert` left. The Compiler self-build has been red since #964 because the pinned runtime
  predates the layout budget raise; the CI keeper is re-pinning (run 41052101916).
- Item 3: #920 (44d0707ca) moves test running into `chibi build` and deletes tools/test_policy.py (-6.1k lines).
  The Makefile is gone and the Python is down to 2 files (tools/configure_inputs.py, tools/package_sdk.py; #983 032411a5a),
  both tied to the SDK CMake. Still left: 35 CMake files (strings-ints).
- Item 4: #884 (567fec819) moves program sessions to the authored compiler, which unblocks archiving src/language
  (coordinator, host-clock). #928 archived the renderer, player and UI host; #945 (game runtime) is merging.
- Item 5: `foreign import` parses (#933), foreign functions compile as native rows (#949), and foreign calls lower
  through llvm_native as declared symbols (#990, 88f5eea7f). Compile-time execution covers integer and bool functions with
  locals, if/while and calls (#988). Still needed: compile-time arrays and records, and a CI test linking a vendored archive.
  Generic records (#927), package-scope `when` (#938), `core:target` (#937) and
  compile-time work (#937) have landed. Still needed: full compile-time execution and `foreign import`.
- CI speed (2026-10-30): the Linux Release build went from 42 min to 14 min (#911, 6acdb392c, 062d54e04,
  ce7e95f20). The last green main (cd40d2194, 10-29) took 2.5 min because its seed matched the
  sources. Refreshing the seed removes recovery entirely until the compiler changes.
