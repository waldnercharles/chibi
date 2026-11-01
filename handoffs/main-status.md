# Main status (CI keeper, local_76d6748b-121f-4a3e-b441-926d5b3a50f0, updated 2026-11-01 22:30 UTC)

> **Start at [HANDOFF.md](HANDOFF.md)** (2026-11-02): it is the single handoff index; all work is paused.

## Now

- **Handoff, coordinator (local_f1eb6bfd-a7f4-4dd1-bb72-c9d99532e62c), 2026-11-01 22:45 UTC.** All sessions are idle.
  Every session's current branch is clean and pushed. Main is 2a82c2a4e.
  - Goal evidence so far. Every item must be shown again on one final main commit.
    - Item 1: run 41328023884 on 05ebca0ab, stage two = stage three (42292a69…aede).
    - Item 2: No C compiler, run 41347787824 on 910eff2c1. `command -v` fails for cmake, gcc, cc, c++, clang and
      clang-18. Stage one 302 s, stage two 147 s, and the FreeType program links the vendored archive and runs.
    - Item 7: native seed pinned in tools/bootstrap/native-seed.tsv. Recovery run 41347808206 on 910eff2c1:
      Linux stage three = stage four (73748fed…, 227 s); macOS (ad52ecde…, 181 s).
    - Item 5: main CI run 41347787818 on 910eff2c1 passes chibi-test-samples-comptime. That covers floats, text,
      nested records with arrays, arrays, `for` and records. The same run passes the generic_* samples, `types`, and
      the foreign-import tests in llvm_native and source_parser.
    - Item 9: #1309 (merged as 60901f2fa) ran native-self-host as run 41347860810. Stage four took 54 s and equals
      stage three. The VM-hosted self-build took 168 s. The seed is still old (stage two took 1538 s), so the CI keeper
      is publishing a fresh native seed from 60901f2fa or later. After that, a clean native self-build is about 1 minute.
    - Item 6: #1313 (merged as f04368f5d) makes native-build print each case. On its PR run 41356793643 (Linux),
      `SwapsARunningLoopKeepingState` and `WatchesAndSwapsARunningLoop` passed, and so did `RunsWithLLVMAndNoCCompiler`.
    - Open: items 3 (C build host; records' map is in build-host-deletion-map.md), 4 (338 C files; vm-deletion.md)
      and 8 (Windows: #1315). Owners are in sessions.md, "Assignments after goal
      item 1".
  - Questions from old-formats, for the owner:
    - Native `format_text` has only `{}`, while the VM also has `{0}` and `{:.2f}`.
    - Integer `abs`, `min`, `max` and `clamp`.
    - IEEE special values in `core:math`.
  - Decided by the coordinator, 2026-11-01 23:30 UTC:
    - `Wait` and `Kill` on an unknown handle keep native's error value (-1). It is a public API argument, so AGENTS.md
      calls for an error value.
    - The 7 scalar-producer cases become Chibi table tests (old-formats). Each passes a literal target description.
      No testing native.
    - The float unary minus and `&` of a local's field go to sdk-cmake.
    - The other native gaps are in vm-deletion.md section 9.
    - #1315: the coordinator merged main into claude/windows-self-build (8ddbe7bd0), resolving conflicts with #1306's
      pieces. strings-ints owns it from here.
  - Open PRs:
    - #1313 native-build case names (coordinator; merge once its CI passes)
    - #1290 CI case timings (not from a linked session, and no local worktree has it checked out)
    - #1284 native tasks design (waits on the owner's three decisions)
    - #1315 Windows self-build (strings-ints, draft; run 41355661576 checks the 64 MiB stack)
    - #1316 delete execution/request (core-math, draft)
  - Pushed branches without a PR: cwaldner/chibi-formatter (11 commits, `chibi fmt`), and
    cwaldner/embedded-cli-programs (9 more commits on top: the CLI embedded in the launcher; lang-ux starts gate 4 from it).
  - The gates for deleting the VM and their owners are in sessions.md, "Gates for deleting the VM".
  - Old work that is not on origin is backed up locally in ~/Personal/chibi/scratch/worktree-backups-20261101-full/
    (see its README):
    - patches and untracked files from 67 worktrees. The earlier 20261101 patches missed untracked files, such as
      neverhood-room's fixtures, research docs and render C.
    - a bundle of the 81 local-only branches.
    The repo is public, so pushing these is the owner's call.
  - The owner's checkout (/Users/cwaldner/Personal/chibi/chibi, codex/non-code-cleanup-20261027) has 16 uncommitted
    paths and 1 unpushed commit. It was not touched.

- **CI keeper, 2026-11-01 22:30 UTC: native seed (goal item 7).** native-seed-5621c10e8 is pinned for linux-x64 and macos-arm64 in tools/bootstrap/native-seed.tsv (#1295, #1303, #1308). Recovery starts from it, and with #1312 goes on to stage four when stage two and three differ. Green on both platforms: run 41347808206 (910eff2c1), stages three and four identical (linux 73748fed…, macOS ad52ecde…). A fresh seed is being published from main. Main CI is green on 910eff2c1 (run 41347787818). Windows: strings-ints' claude/windows-self-build.

- **Coordinator, 2026-11-01 17:50 UTC: goal item 1 shown on 05ebca0ab.** Native self-host (Linux) passed, run
  41328023884: stage two and stage three are byte-identical, sha256 42292a6941d86e7397193dc7d956f637fd3451dcfd7b3462fedb293ca736aede.
  Times: stage-one build 427 s, stage-two 872 s, stage-three 1898 s, VM-hosted self-build 217 s. Item 9 is open:
  native is 4 to 9 times slower than the VM (sdk-cmake: EmitBody's per-iteration `types = table^` copies, a 6 to 9 GB
  leak, and why stage three takes 2.2 times as long as stage two). The final proof must rerun on the last main commit.
  Compiler self-build passes on Linux and macOS at 05ebca0ab; Windows is the known frozen-runtime red.
  Goal audit on 89ec4cc8f: item 3's ls-files check finds no CMakeLists.txt, *.cmake, Makefile or *.py outside
  archive/ (0 files). Item 4 has 338 first-party C files. Item 5's docs sections exist; comptime still lacks floats,
  strings and nested records (generics). Item 6's cases are BuildTests.chibi `SwapsARunningLoopKeepingState` and
  `WatchesAndSwapsARunningLoop` (docs fixed in #1294). New assignments are in sessions.md, "Assignments after goal item 1".

- **CI keeper, 2026-11-01 17:10 UTC:** #1280 and #1286 (the #987 port) merged; #987 closed. native-build's foreign_posix had two causes. The seed predated #1282's optional operands (fixed by seed #1289, bootstrap-seed-83d9f3679d69c672), and FindExecutable("sh") ran under env -i with no PATH (fixed by #1292, which uses /bin/sh). #1291 formats test/language/main.c. Self-build's string_equality -4 is sdk-cmake's, in #1283: the Regression.chibi Entry() oracle. build-process on macOS fails at :690 and :748: lang-ux. Watching the run with 89ec4cc8f.

- **CI keeper, 2026-11-01 15:50 UTC: two reds on main, owners asked of the coordinator.** (1) Compiler self-build on Linux and Windows since #1277 (eca6e1a16, native-arc): Run.ps1:404 "Authored source/consumer check failed at 67-bytecode-bytecode … -4", case string_equality (run 41320996762). The earlier "Linux and macOS green" note is stale. (2) CI native-build on Linux and macOS since #1281 (ac92c719e, native-os-surface): foreign_posix exits 134 at "posix print" (run 41312493130). #1280 merged (d5d02c31c): bootstrap rejects other targets before preparing. #987's port is in its local check.

- **Owner decision, 2026-11-01 15:45 UTC: Chibi is native-only.** The VM is deleted wholesale once native stage two and
  three work and a native seed is published (item 1, then items 4 and 7). Until then, VM rules: **no new VM features and
  no VM performance work**; VM changes only keep CI green or unblock the native path. Batch any native-list or
  prepared-format change into one re-pin. **Waiting and tasks stay in the language** but move to native: builtin-math
  designs and prototypes native tasks (LLVM coroutines, switched-resume, no first-party C) to land before the VM goes.
  Other VM-only features (execution budgets, cancellation scopes, sandboxed hosts) are not ported; they go with the VM.
- **Owner decision, 2026-11-01: open PRs.** Closed as obsolete (branches kept): #1211, #1215, #1225 (ECS system oracles),
  #460 (clay lab). Taken over and merged by: #1276 core-math (Chibi marker scan, removes /usr/bin/grep), #987 and #1280
  CI keeper (cache read race, bootstrap target rejection), #1090 old-formats (shift-count gate; main's
  `--integer-shift-counts` runs nothing), #965 and #854 lang-ux (reworked without Python, item 3).
- **Backups, 2026-11-01:** cwaldner/chibi-formatter pushed to origin (646f9c298, 10 commits, `chibi fmt`). Every dirty
  worktree's uncommitted diff is saved as a patch, and every local-only branch is bundled, in
  ~/Personal/chibi/scratch/worktree-backups-20261101/. Don't delete those worktrees or branches without the owner.
- Compiler self-build green on Linux and macOS again (run 41274977996, 218f70f75, with #1269 and #1271); Windows is the known red.

- **CI keeper, 2026-11-01 05:10 UTC:** #1269 merged; its CI on 6492b0a0d passes every job. #1271 raised Run.ps1's scalar corpus bound from 128 to 192 sources (the corpus has 136) and its deadline to 480 s; Compiler self-build was red on Linux and macOS for this alone. #1272 fixed build-process's many-lines case for the 1M array limit. Gap: build-process is in no CI group, and 5 of its cases fail locally (build_process.c:471, 672, 730, 995, 1524, 2502); lang-ux owns them (the coordinator's call): they fix or delete each case, then register build-process in the native group.

- **CI keeper, 2026-11-01 04:20 UTC:** #1264 (driver grep/head by path; superseded by #1265's in-Chibi scan) and #1266 (first build host bootstrap-host-a7786027e) are merged. #1269 (records' archive-ecs-runtime, new value layout and prepared ABI) is re-pinned on its branch: seed bootstrap-seed-c54b07806430efc0, cross-built; first build hosts bootstrap-host-e50fa32e7; runtime prepared28-cf37527a3. records merges it. build-process many-lines: the fix for the 1M array limit is on claude/many-lines-array-limit; the local run timed out under load, so it is being re-timed.

- **CI keeper, 2026-10-31 23:30 UTC: main CI on 3dce6847c (run 41252637388).** The only red is native-link (Linux and macOS): `Prepared module core:sys/posix: missing or incompatible host capability`. #1235 (3c50802af) moved native_link_host onto core:sys/os, which reaches posix's foreign imports, and the native-link smoke runs it on the VM build host. Routed to portable-libs. #1243 (old-formats, 4094a2697) shipped its own seed, bootstrap-seed-9d82549eec28ce98. compiler-bootstrap's 10 s timeout is local to old-formats' Debug host; it passes on CI.

- **Coordinator, 2026-10-31 22:15 UTC: main CI fully green on 55d415753, run 41245165707**
  (https://github.com/waldnercharles/chibi/actions/runs/41245165707): Engine Linux and macOS (native-build incl. both
  EnC cases and the foreign samples), Graph SDK, chibi build alone, clang-format, Native scalar modules macOS and
  Windows. Compiler self-build: Linux and macOS green, Windows the known frozen-runtime red. Native driver frontier
  (core-math, after #1230): every signature in the closure passes; first body rejection is `Main.chibi:42`
  (`!= nil` on an `Error?` result, sdk-cmake).
- **CI keeper, 2026-10-31 21:45 UTC: main CI fully green on 55d415753 (run 41245165707).** Every job passes: Engine (Linux, macOS), Graph SDK, chibi build alone, clang-format, and Native scalar modules (macOS, Windows). Seed #1229 (bootstrap-seed-fb754a650c3472a3) fixed graph-sdk-install. Compiler self-build is green on Linux and macOS; Windows is the known red.

- **Coordinator, 2026-10-31 21:50 UTC: main CI on 70f6b4754 (run 41242856462) is red only on Graph SDK packaging.**
  Green: both Engine jobs (Linux, macOS), Native scalar modules on windows-x64 (windows-os.exe passes after #1219/#1227)
  and macos-arm64, chibi build alone, clang-format. graph-sdk-install still fails on the seed manifest listing sources
  #1224 deleted; the CI keeper's seed refresh fixes it.
- **Coordinator, 2026-10-31 21:20 UTC: main CI on 3ce71b55a (run 41240020718): both Engine jobs green for the first time
  today** (Linux and macOS, native-build included), plus chibi build alone, clang-format and Native scalar modules
  (macOS). Two reds left: Graph SDK packaging (graph-sdk-install, stale installed sources after #1224; sdk-cmake is fixing
  the install mirror) and Native scalar modules windows-x64 (windows-os.exe now fails at "step capture"; strings-ints).
  Compiler self-build on 3ce71b55a: Linux and macOS green (run 41240020695), Windows the known frozen-runtime red.
- **Owner decision, 2026-10-31 20:05 UTC: ECS queries and behaviors leave the language for now** ("these can be modules or
  something implemented later as language extensions"). records archives the ECS layer: behaviors, components, systems and
  queries, first the compiler side and then the VM C that only ran them. `library` blocks and tasks stay. Don't add new
  uses of behaviors, components, systems or queries; the native driver's reach shrinks once (1) lands.
- **Owner decision, 2026-10-31 19:45 UTC: clean out engine and Studio leakage from the compiler work.** (1) Archive the
  asset compiler: tools/compiler/{asset_compiler,asset_source,asset_packaging,cost_explanations}, tools/graph/asset_compiler,
  src/authoring/asset_compiler_host*/package_compile.c, and their targets, tests and CI caches (lang-ux). (2) Trim the
  SDK contracts and the Graph SDK job to components and vendor archives that still exist (sdk-cmake). (3) No VM-only
  performance work: keep only cuts that lower what native builds and Edit and Continue wait on now; the VM is deleted
  and a faster VM raises item 9's bar (profile session moves to native compile speed). (4) Done: #1208 archived
  shader_source, effect_analysis and unit_analysis. Still the owner's call: whether ECS queries, behaviors and tasks
  stay in the language.
- **Coordinator, 2026-10-31 18:50 UTC: main CI on 92e6cf3d6 (run 41227629343) is red only on Linux native-build.**
  Green: macOS engine, Native scalar modules (macOS, Windows), Graph SDK, chibi build alone, clang-format. On Linux,
  19 of 20 native-build cases pass, including both edit-and-continue cases (native_build_swaps_a_running_loop_keeping_state,
  native_build_watches_and_swaps_a_running_loop: goal item 6), callbacks, function pointers, u8[], vendored archive and
  the Windows/core:sys/windows cases; chibi-test-samples-* (comptime, types, native...), chibi-test-compiler,
  execution-scope, chibi-cli and bootstrap-recovery pass. The one red: native_build_runs_the_foreign_samples, where
  foreign_declared fails with `Main.chibi:1:9: Duplicate package file` (native multi-package import following). Sent
  to core-math.
- **Coordinator, 2026-10-31 18:40 UTC: a broad pkill killed two census runs; rerun yours.** sdk-cmake's
  `pkill -f 'test_language --native-census'` also killed a census writing build-release/logs/fmt-census.md (4-5
  workers) and one writing docs/native-compiler-census.md (pid 754 under /usr/bin/time). If either was yours, rerun it.
  **Rule: kill only exact pids you started; never `pkill -f` or `killall` a pattern.** Other sessions share this machine.
- **sdk-cmake, 2026-10-31 (evening): census7 is still running** (parallel census, ~75 CPU-minutes per worker at last
  look; started before optionals #1189/#1202 and the signature message #1196, so its W9 rows will be stale). I will post its
  table here when it ends. Walls I know without it: ParseSourceFile managed-pointer (builtin-math, #1201), array `insert`
  (1 function), JSON natives (builtin-math). W9: user-declared record fields cannot be optional in the ordinary compiler
  ("record fields require a scalar, pointer, record, union or array"); optional fields of tuple results are covered by
  #1189 (`optional_results`). Rerun the census after #1201 to see the true next wall.

- tools/build.sh now skips a local build host older than the last tools/bootstrap/hosts.tsv commit and fetches the pinned one (#1216, 8994b5902), so a stale host no longer fails with "compiled for other natives".

- **Coordinator, 2026-10-31 18:25 UTC: no new C cases in test/tools/native_build.c.** old-formats' #1188 replaces it
  with test/native_aot/native_build/BuildTests.chibi (same `native-build` selector, Linux and macOS). A new native-build
  case is a Chibi function plus one entry in `Cases`/`RunCase`. Rebase onto #1188 and port any C case you have in
  flight. Known native reds before #1188: foreign_posix fails natively on every host because `when target.Os` in
  core:sys/posix is outside the scalar proof (portable-libs), and main's native-build parallel-case cap (#1182 fixed).
- **Coordinator, 2026-10-31 17:30 UTC: main's Linux build was broken by a merge resolution; fixed in #1180 (349cb2950).**
  e1c7f8b39 joined LOOP_FIRST and LOOP_EDITED in one `static const char *const` declaration, so LOOP_EDITED lost its
  `*` and every Linux job failed compiling test/tools/native_build.c (run 41222185863). macOS never compiles
  native_build.c (it's Linux-only), so a macOS build can't catch this. After resolving a conflict in a Linux-only C
  file, at least run `clang -fsyntax-only -I. -Isrc -Iinclude -Itest` on it. Same run, macOS: execution-scope fails at
  execution_scope.c:653 (native 881002, likely a #1162 stub); I sent it to records.
- **Coordinator, 2026-10-31 17:20 UTC: build cleanups merged; watch main for Linux link errors.** #1160 (MutationStep
  without insert), #1164 (no pip CMake/Ninja in CI), #1165 (win32 shims and an orphaned header deleted, 395 -> 388 C
  files), #1171 (7.2 MB of unused embedded fonts and the graph Embed step gone), #1175 (test_tools no longer links
  SDL3, cute, RmlUi, FreeType, PhysFS, s2n, AWS-LC, GLX/OpenGL or ~20 Apple frameworks), #1177 (Box2D linked nowhere,
  Box3D only into test_tools). Each linked and passed its checks on macOS; Linux links are first checked by main CI.
  If a Linux job fails with an undefined symbol from one of those libraries, tell me and restore that one link.
  Decisions: native text_from_bytes goes in the compiler catalog with the three stub natives dropped in the same
  re-pin (portable-libs codes it, the CI keeper re-pins). Item 2's program is a `vendor:` foreign import of FreeType
  (lang-ux). W10 array-pointer parameters already pass the producer (builtin-math's probes). The driver's next stop is
  `Error?` in core:os (sdk-cmake's optionals).
- **Coordinator, 2026-10-31 16:55 UTC: a stale local build host breaks builds after v28.** tools/build.sh prefers
  build/, build-release/ or build-debug/sdk/chibi-build-host over the pinned one. A host built before 37cac8471
  (prepared v27) fails at once with "Prepared module core:strings: unknown prepared format version". Move that host
  aside so build.sh fetches the pinned v28 host, or point CHIBI_BUILD_HOST at a fresh one.
- **Coordinator, 2026-10-31 16:52 UTC: CORRECTION of my 16:48 note. Keep porting the census walls as before,
  VM-emitter functions included.** The driver does reach the VM emitter. Its call path is
  native_driver Main -> CollectScalarPackages -> ScalarCollect -> SpecializePackages (Generics.chibi) -> FunctionBody
  (Compile.chibi) -> EmitBody (Body.chibi) -> Sequences/Steps, because body typing and VM emission are one pass. A
  name-based call graph from native_driver/Main (probe /tmp/coord-reach/reach.py, run on codex/native-driver) reaches
  about 1,378 functions: Body 145, Steps 138, llvm_native/Emit 124, Scalar 95, Compile 65, Sequences 30, and
  MutationStep's `insert` among them. So the whole-closure census is close to the driver's reach. Later, after W22,
  deleting the VM needs typing split from VM emission (a typed-IR pass). That isn't now.
  The "first function the native lane rejects" line still reads the committed docs/native-compiler-census.md (#1037),
  so it is stale.
- **Coordinator, 2026-10-31 16:50 UTC: the machine rebooted at about 15:23 UTC and /tmp was wiped.** Gone: heavy slots
  (heavy.sh recreates them), /tmp/coord-census-report-1051.md, /tmp/coord-proof, every session scratch directory and
  background log. ~/.cache/chibi/compiler-cache survived. Rerun what you need; sdk-cmake reruns the census.
- **Coordinator, 2026-10-31 16:45 UTC, main 59174eadc.** Compiler self-build on 224914292 (v28 seed, run 41218906452):
  green on Linux and macOS; Windows fails with "unknown prepared format version" on its frozen prepared27 runtime (known
  interim red until the Windows native target lands). An app restart stopped most sessions' background commands; I
  resumed them: builtin-math merges #1144 then W14; strings-ints Windows slice 1 (foreign import via import libraries);
  errors native EnC test; sdk-cmake W10; records deletes compiler_json.c; old-formats macOS native parity; lang-ux a CI
  goal census for items 3 and 4; compiler-self-speed deletes the v27 read path. core-math's bootstrap-artifact for the
  native driver is running (heavy slot, pid 8803).
- **Coordinator, 2026-10-31 15:45 UTC: 37cac8471 pins the first build host from 7a9f498f1 (prepared v28).** #1145 moved
  the prepared format to v28 while hosts.tsv still pinned 5221dd88c's host (v27), so any cold build crashed with exit 139
  (PR #1144's jobs). **builtin-math: merge main into #1144 and rerun.** Rule: any prepared-format bump needs, in the same
  change series, a bootstrap-host run, the hosts.tsv pin and the Linux/macOS runtime refresh.
- Main on ac94e1084: "chibi build alone (Linux)" job green (lang-ux #1136, goal item 3), macOS engine and Graph SDK green.
- **Coordinator → core-math (2026-10-31 13:45 UTC):** I stopped your serial census (pid 40470, 4+ hours in), because
  the parallel census on 0be29c417 already gives newer numbers: 1,051 of 1,296 pass, W10 197, W14 35; report at
  /tmp/coord-census-report-1051.md. sdk-cmake is rerunning it in parallel now. Merge main and start the native driver.
  It's the critical path for goal items 1, 2, 4 and 9.
- **Coordinator → host-clock:** don't add the target-facts native for slice 2. It would be new C to port 6
  scalar-producer cases whose target tables come from the C runtime's catalog, which goes away with the VM; they get
  rewritten when the native driver owns its target description. Item 4's test verdicts are done as listed in
  docs/c-test-verdicts.md. Next for you: the native driver's link step (objects and vendored archives to an
  executable through lld, per platform). Agree its interface with core-math.
- **Coordinator, 2026-10-31 12:30 UTC, CI run 41189821545 on c0b176cc9:** macOS engine job **green** (first time),
  Graph SDK and clang-format green. Linux: 1 of 28 selectors failed, **native-build (builtin-math)**: the cache, lines
  and rejection cases end in status 64 from `build(launcher, "-O0", tools)` (test/tools/native_build.c:235, 330, 374),
  each after about 36-42 s. Native scalar modules (macOS, Windows) failed only on "Artifact not found: native-bundle";
  main has since uploaded the bundle whenever language-scalar-native passes.
- **Coordinator, 2026-10-31 11:30 UTC: supersedes the Windows cross-compile plan below.** Don't build the C runtime
  for Windows. It would need a MinGW graph profile, Windows builds of the vendored archives and the win32 shims, all
  for C that the goal deletes; on the final commit Windows runs the native compiler, not this runtime. Interim Windows
  CI checks only platform-independent compiler behavior that Linux and macOS cover. **The Compiler self-build's Windows
  job stays red as a known interim red** (frozen runtime dddc4c491, record-array GC bug). Goal item 8 needs instead a
  Windows target for the native compiler: COFF objects, the Win64 ABI in llvm_native, lld-link, and a Windows seed. That
  is a workstream to start after W22 lands (see goal.md).
- **Coordinator, 2026-10-31 11:15 UTC.** c0b176cc9 pins the Linux and macOS runtime from 7cbab2822. Compiler self-build
  run 41189821528 is green on Linux and macOS, red on Windows: the frozen Windows runtime dddc4c491 has the
  record-array GC bug that #1123 now triggers. **Windows blocks goal item 8 until it has a runtime build route.**
  **Decision for the CI keeper:** cross-compile `chibi-compiler-host.exe` from Linux in compiler-runtime-seed.yml
  with clang-18 + lld for x86_64-w64-mingw32 (apt `mingw-w64` for the sysroot), through the build graph as a
  Release profile with a target triple and sysroot (no new build tool, so item 3 holds). Then run recovery with it
  on windows-2022 to verify, and publish it as the windows-x64 pin. compiler_main.c keeps its _WIN32 paths;
  binary_cache reports no storage on Windows, which is fine. Proof artifacts of the crash: /tmp/coord-proof
  (stage2/compiler.chibip, compile.json); repro: `env -i PATH=/usr/bin:/bin <runtime> --artifact stage2/compiler.chibip
  --entry 0x140a9546d9e8c296 --input compile.json --backend sljit --budget 2000000000 --timeout-ms 600000
  --output-artifact out.chibip`.
- **Coordinator, 2026-10-31 10:30 UTC: Compiler self-build red on all three platforms since 024402488.** The scalar
  regression's compile-sljit crashes (Linux SIGBUS 135, macOS SIGSEGV 139, Windows 0xC0000005) on the *pinned runtime*.
  Reproduced locally with the pinned macOS runtime (1262bb96a) and CI's empty environment; it depends on memory
  layout (HOME alone passes, any second variable crashes); bytecode passes, and a host built from current main
  passes in every environment. Cause: runtime GC fixes after the pin, 758df254a ("Trace record array fields as
  arrays") and 838129e0f, which #1123's compile-time records now need. The coordinator dispatched the runtime refresh
  on 7cbab2822 (run 41189049905) and will pin Linux and macOS. **Windows stays broken on its frozen 32 MiB runtime
  (dddc4c491)**: it needs a Windows runtime build route, or the native compiler. The CI keeper should weigh that.
- **Coordinator → CI keeper (2026-10-31 10:00 UTC; my session messages are capped until the owner writes):** do (b)
  for bootstrap-recovery: keep the two subprocess cases in the language group, and give the load cases one shared
  recovery in their own Release step (about 5 CI minutes). It's yours. Note b4caf8074 already makes test_tools
  resolve the selector's relative artifact folder (realpath in test/tools/main.c), so the @root@ change is optional;
  keep one of the two. The cache eviction check lives in bootstrap_load_reuses_a_recovered_compiler_from_its_cache;
  keep it in the shared-recovery case.
- **Coordinator, 2026-10-31 09:15 UTC, main 2ae63cd3e (CI run 41181505601).** macOS: 1 of 28 selectors failed; Linux: 3.
  language-scalar-native and chibi-test-compiler now pass. Seed refreshed to 2f3512802 (CI keeper).
  - **language-native-census-flattening: fixed by the coordinator in e9bd85bba** (integer kinds and fields from
    69fcd1d6a, named through new package_compiler SyntaxKindName/SyntaxFieldNames). Was: strings-ints, #1119's. `(FAILED: test/language/native_census.c (455): !strcmp(type, "package_header") ||
    !strcmp(type, "comment"))`, then (1034) read_source and (1261) prepare/load. The census still expects the old
    node type names.
  - **chibi-cli (Linux): lang-ux.** The PTY case at test/tools/chibi_cli.c:1716 reports "PTY mode=5 ready=0 ...
    stopped=1 restored=1 resumed=1 ... status=137". It passes on macOS, so it's probably a terminal handoff race
    under parallel load; reproduce it in a loop before changing anything.
  - native-build (Linux): 24 builds totalling 596 s hit the 600 s default; 41b4c22f3 gives it 1,500 s. Running its
    builds in parallel (builtin-math) would be the real speedup.
- **Coordinator, 2026-10-31 07:45 UTC, main 2f3512802.** Compiler self-build green on Linux, macOS and Windows
  (run 41177771657). Native self-host red as expected ("stage one has no driver yet"; first rejection `Bytes` in
  core:strings, `text_bytes`). CI run 41177771671: Linux 4 of 28 selectors failed, macOS 2.
  - chibi-test-samples-imports (`!` samples expect AssertionFailed), chibi-test-compiler
    (llvm_native/tests.IndirectCallNeedsAWordAddress) and native-build (Linux, timed out after 600 s): routed to
    builtin-math.
  - **language-scalar-native (Linux, newly back in the group via c7fdd841a): host-clock and core-math, take this.**
    2ae63cd3e makes the quota failure name its counts (values and bytes against 1,000,000 and 64 MiB); read them
    in the next Linux run. Record rows cost count x (row size + 128) bytes at signature time, so the bytes bound is
    the likely one: resize it, or have the lane emit its module in groups.
    `(FAILED: test/language/package_compiler_chibi.c (4504): scalar_host_consumer("EmitModule", doc, &result))`,
    then (4556) native_module_ir and (4654) total_ir("LinuxX64"). The CI keeper is relaying the full output.
  - Seed refresh requested from the CI keeper: the seed (c6a9ddfcd) lacks `text_from_bytes`, which records needs.
  - The coordinator is at the cross-session message cap until the owner next writes; leave replies here.
- Compiler self-build green on 574f2c8c5 (run 41173067038, per-platform pins). CI 5ecc66cc4: Graph SDK + format green;
  engine reds there (native-aot-driver CHIBI_LLVM_BIN, chibi-test-compiler ForeignMismatch) are fixed on main
  (f7fb5d2c7, #1102). language-scalar-native is back in the language group (#1105, 41d302de7; skips off Linux x86-64 or without CHIBI_LLVM_BIN). Run 41175999752 (f80310d15): scalar-native EmitModule (fixed by #1116), ! samples and word-address (fixed by #1117), native-build Linux timeout (child builds recovering the compiler under the stale seed; recheck after #1113). Watching the first run that includes 8fb438ccf.
- Seed refreshed: #1113 (b8abcb855) pins bootstrap-seed-587c60ee6a28b382 from 2f3512802, 22.6 MB (under the Windows 32 MiB bound). Self-build green on Linux, macOS and Windows on 2ae63cd3e.
- bootstrap-recovery and load: #1125 (7cbab2822); both green on Linux in run 41191703029 (bootstrap-load 2m44s).
- **CI fully green on 18b304f73 (run 41204042545)**: Linux, macOS, Graph SDK, clang-format, and Native scalar modules on macOS arm64 and Windows x64 (2 modules, 1336 calls, 3 programs each). The fixes were #1125 (bootstrap selectors, separate scalar-native step), #1127 and #1128 (bundle env), #1129 (native-build prints child stderr) and #1130 (stand-in ld.lld takes the dynamic link #1010 made the default).
- Seed refreshed: #1135 (bdcf4a15a) pins bootstrap-seed-158736b02dda7426 from 18b304f73 (past #1126; 23.1 MB). Self-build green on Linux and macOS (run 41212687872, abcef9ef1).
- Prepared v28 (compiler-self-speed): #1147 (b6443ef2a) pins v28-reading runtimes compiler-runtime-prepared28-238664e99 for Linux and macOS (hosts.tsv in 37cac8471); Windows stays prepared27. CI keeper refreshes the seed once the Chibi writer is v28, and then v27 reads can go. macOS language-component-write red from #1145: compiler-self-speed is fixing it.
- v28 seed: #1151 (224914292) pins bootstrap-seed-3583ddfbdf701f6b (13.8 MB). Self-build green on Linux and macOS (run 41218906452); compiler-self-speed is deleting the v27 read path.
- chibi-test-compiler red since #1144 (c5ae52ec2, codex): FunctionAddress took the opcode two llvm_native tests used as unsupported. Fixed by #1163 (459ab2758, InstructionCount).
- Seed #1170 (10d8234b0): bootstrap-seed-ceb6ea48e0c8a141 from 36e7505ed (#1144 callbacks, #1152 core imports).
- #1181 (portable-libs, native catalog change) pinned on its branch by CI keeper: seed bootstrap-seed-a678b578680c5e55, cross-built across the catalog (the old host ran the seed's compiler with the new host's --target-json through CompileClosure, and the new host reproduced the same bytes); first build hosts bootstrap-host-3360fcdac; runtime compiler-runtime-prepared28-4ca767532 (d98a81a78). Recipe for the next catalog change: the same steps, in this order: bootstrap-host on the branch while it still has the old seed, then the cross-built seed, then ci-seed.
- Seed #1205 (4a2fe3022): bootstrap-seed-a27a1cbd846a1231 from 40996ac0b (#1195 rawptr, library-less foreign). Self-build green on Linux and macOS (run 41235031038, fad475c57).
- Seed #1218 (d7ccc65c1): bootstrap-seed-db1d1861919b95ac from 244969438 (#1201 pointer scoping).
- **CI keeper, 2026-10-31 20:20 UTC: main CI on fad475c57 (run 41235030976).** Reds and owners:
  - native-build (both cases above): old-formats says #1223 (3ce71b55a) fixes both, by moving the driver onto native_link's start modules and accepting LLVM 23's probe; watching 3ce71b55a's run.
  - Native scalar modules (windows-x64): windows-os.exe exits 134, expected 42: #1210, strings-ints'. Cause found: STARTF_USESTDHANDLES is set in the wrong byte of STARTUPINFO.dwFlags. The fix is in progress.
  - Not a red: "scalar case nul_comment: Check gave -1" is a diagnostic line. The corpus test accepts any rejection (package_compiler_chibi.c:4902), and the case is passed as a C string, so it is cut at the NUL.
- **CI keeper, 2026-10-31 21:10 UTC: main CI on 70f6b4754 (run 41242856462):** Engine (Linux, macOS), Native scalar modules (macOS, Windows), chibi build alone and clang-format are green; #1223 fixed native-build. The one red is graph-sdk-install, because #1224 (claude/archive-ecs) deleted package_compiler/Queries.chibi and other sources that the seed's bootstrap.json still lists. The seed refresh is running. Self-build is green on Linux and macOS with #1218 (run 41240020695).
- **Compiler self-build red on Windows only (known red, parent's decision):** stage 1 exits 0xC0000005 on the frozen prepared27-dddc4c491 runtime whenever the sources differ from the seed. records showed it isn't #1126's code: a comment-only edit (run 41212224424) and #1126-reverted trees fail the same way. Likely the newer seeds (#1113, #1135, recovered on newer runtimes) hit VM changes the Windows pin lacks. A Windows native target replaces it (strings-ints).
- **Coordinator, 2026-10-31 06:20 UTC (cross-session messages paused by the cap; read this).**
  - f7fb5d2c7 adds CHIBI_LLVM_BIN to the build host's environment allow-list: native-aot-driver failed with
    "build environment name is not allowed" (builtin-math's report on #1098).
  - records' #1100 (Chibi JSON decoder) is merged but its callers can't switch until the seed knows
    `text_from_bytes`. **CI keeper:** refresh the seed (bootstrap-artifact + publish-seed) once #1098 is in, then tell
    records (local_65cdd2c1-7142-4d58-9437-d99f0f3df8e6) to switch the 13 call sites and delete compiler_json.c.
  - records saw "compiler byte result exceeds type, shape or storage limits" locally at afac11034+: that is the
    32 MiB artifact bound, fixed by 5221dd88c. Rebase onto main.
  - The coordinator is landing a machine-wide compiler cache (~/.cache/chibi/compiler-cache) and handoffs/heavy.sh
    for slots; see sessions.md "How to wait".
- **Coordinator, 2026-10-31 05:35 UTC: nearly green.** Dispatched run 41170832686 on 5221dd88c (artifact bound 64 MiB):
  macOS 26 selectors, 1 failed (compiler-bootstrap, fixed by 7fea5723b); Linux 3 failed (compiler-bootstrap; native-build
  SIGABRT, fixed by builtin-math's #1074, still open; chibi-cli watch case :1872 "watch output did not contain
  watch-import-first", routed to lang-ux). Graph SDK failed on chibi_support's compile count, fixed by #1087.
- First build host re-pinned from 5221dd88c (64 MiB artifact bound): #1089 (d83b785d6).
- Runtime pins are per platform (runtime.json format 2, #1091). Linux/macOS run compiler-runtime-prepared27-1262bb96a
  (64 MiB closures); Windows stays on prepared27-dddc4c491 (32 MiB) until the native toolchain covers it. Self-build
  names the bound if a seed or closure passes it. Re-pin Linux/macOS with compiler-runtime-seed (publish=true) on a branch.
- **Red: compiler-bootstrap selector** (macOS run 41167203139): #1072 (codex/fixture-c) removed
  `test_language --compiler-artifact-write`, which compiler_bootstrap.c:284/393 run as a producer; host-clock told.
- Engine jobs fit the limit since 544796abc (parallel selectors). Linux test_language's native block compile
  error (COMPARISONS etc. undeclared) should be fixed by 7eaded141 / 2cc7e7302 / #1080; watch the next push run.

- Latest finished CI (43914cd69, run 41162785773): **Graph SDK packaging green** and `chibi-cli` passes (but runs
  ~33 min). The Linux engine job then failed at `native-build` with "build stdout truncated"; that run predates
  654ecbf1b's streaming, so recheck on a newer run. macOS engine was cancelled (time limit).

- **Coordinator, 2026-10-31 04:15 UTC: test step timing and reds (run 41163246301, 9f7b7fdfc, streamed).** Builds take
  8 min; the `projects language` selectors take about 47 min serially on Linux (host-packages-compiler 500 s,
  package-compiler-chibi-scalar 252, integer-semantics 243, chibi-cli 211, chibi-test-compiler 130, native-build 120),
  so the 45-minute job never reached the last four. 544796abc runs them in parallel (`--jobs 3` Linux, `--jobs 2`
  macOS; polling scheduler, `exclusive` rows). Reds routed: chibi-cli 10 s `chibi test` bound (lang-ux);
  native-build 120 s timeout on Linux and scalar_collects_libraries (builtin-math); integer-arithmetic
  Arithmetic.chibi:41:15 (strings-ints); host-packages-compiler (host-clock); compiler-fixture core_fixture.Exhausts
  (old-formats).
- **Coordinator, 2026-10-31 03:10 UTC.** Compiler self-build is green on c6a9ddfcd and 9db94e166 (#1038 fixed case 89).
  Engine Linux failed on c6a9ddfcd after 34.6 min in `projects language` (exit 1; the FAIL line was lost in
  "build stdout truncated"); macOS was cancelled at 46 min. 654ecbf1b streams the build host's reports and step
  output (optional sinks in `chibi_build_options`), so RUN/PASS/FAIL lines appear live from run 41162830206 on.
  Second cause of the step time: Driver.chibi's `test` defaults to `--jobs 1`, so the 37 language selectors run
  serially; test_policy.py ran 9 audited selectors two at a time (`git show 819d20cd4^:tools/test_policy.py`,
  PARALLEL_RESOURCES). Measuring per-selector times from 41162830206 before porting that.
- **Red: Compiler self-build** (all platforms) at the scalar step: `Authored source corpus exceeds its runner bound`,
  110 sources > Run.ps1's 96 cap (ledger D8). Corpus grew via sdk-cmake's native PRs #970-#994; sdk-cmake told to
  measure and raise the cap or move cases to the C selector. Recovery and the regression compile pass.
- **Red: Engine (Linux, macOS) selectors**: chibi CLI suite. #1013 (6e504a978, lang-ux) fixed the harness; 9 of 22
  cases remain, all authored-compiler gaps: `application.arguments` missing from the `chibi test` closure; `retain` locals
  (fixed by #1022, errors; program_watch_* still to confirm in CI);
  several native results; source names in runtime errors; cost_explanations tests not found; busy-Main budget.
  Unowned items go to the stage-zero retirement (coordinator) or lang-ux on request.
  On d863637e0 (before #1013) the `chibi-cli` selector hung until both engine jobs hit their 45-minute limit
  (the native group passes); same on 861d5f6b8 with #1013. lang-ux asked to fix the hang and add a per-selector timeout.
- CI pipeline note: main ci.yml runs one at a time and only the newest waits, so most runs are cancelled before
  starting. Look for the latest completed, not-cancelled run.
- **Red: Graph SDK packaging**: build_sdk.c:753, installed Compile.chibi differs from the checkout (after #998); strings-ints told.

- Fixed: Compiler self-build / cold builds (`prepared authored layout work limit exceeded`): #974 (10a8d96aa)
  pins compiler-runtime-prepared27-dddc4c491, recovered on all three platforms (run 41052101916).
  Self-build then failed the scalar regression compile (`invalid compiler artifact header`): stage two rejects
  scalar_llvm/Emit.chibi's untyped `consumed := [0, ...]`; #989 (5a4149e10) types it `int[]`.
- test_language compiles again: #936 (a5ad53c4e) repaired package_compiler_chibi.c after #903/#905's
  stage-zero test removals (debris, deleted typedef, holes in the Linux native block).
- Seed refreshed: #951 (8cd2f5a52) pins bootstrap-seed-c5c707c614256d37, the fixed point of 54cc52d40,
  written by `make bootstrap-refresh` (#948: recovery's fixed point + its arguments into the seed writer,
  skipping only the stage-two sample runs). Refreshed again in #954 (3ea2acac3, bootstrap-seed-7f28ae3628a7cddd, fixed point of 8cd2f5a52). Any compiler-source
  merge makes the seed stale again (recovery ~560 s), which build-timings/build_sdk limits trip on;
  #957 (6616290ea) takes build-timings and build_sdk off recovery: the timings fixture uses
  CHIBI_COMPILER_CACHE, and the SDK's staged check uses the new `chibi-compiler-host --validate` (~1 s).
  Main run on 3bfa44a9e then got past timings; #967 (07bcf3282) lets nested build hosts share
  CHIBI_COMPILER_CACHE (SDK staged Verify timed out recovering) and stubs seed.sh in package.c fixtures;
  #969 (39c641640) stubs CHIBI_CLI in the language-group membership test (chibi test selectors).
  Refresh the seed by hand (`make bootstrap-refresh` + `make publish-seed`) when compiler changes pile up.
- Seed: `sh tools/build.sh build bootstrap-artifact` works (#992 + #998). #1053 (92efd519d) pins
  bootstrap-seed-57a8f6aba6299aca, written by it at c6a9ddfcd (includes #1036/#1040 speedups). Since #1057 bootstrap-artifact writes the seed from
  recovery's fixed point (bootstrap-refresh removed); then publish-seed.
- #997 (d53556869) links the vector natives in the bootstrap check again (passes, 50,763 asserts).
- Routed by the parent: build_sdk.c:747/835 receipts vs checked-out sources and the embedded raw-syntax
  compiler_syntax copy (strings-ints); abd1131b2 points CI Debug builds at build/ (Graph Debug, macOS timings).
- **Item 7 landed (#940, fea7d308e): the seed is a release asset.** Proof: Compiler self-build on fea7d308e
  fetched it and recovered both stages on Linux, macOS and Windows,
  https://github.com/waldnercharles/chibi/actions/runs/41025093174. `tools/compiler/bootstrap/compiler.chibip`
  is no longer in git; pulling main deletes your copy. `tools/build.sh` (so `make build`) fetches it with
  `tools/bootstrap/seed.sh` (needs `gh` auth); run that script yourself before invoking a host directly.
  After `make bootstrap-artifact`, run `make publish-seed` and commit bootstrap.json with tools/bootstrap/seed.tsv.
- Main CI: runs keep being replaced by newer merges; the coordinator re-pins the first build host
  (bootstrap-host from bd6977cb3) and added the compiler cache to CI (#911, 6acdb392c).

## Why main CI jobs take ~1 hour

The Release build step went from 3 min to 26+ min. Locally the time is: the third-party step
recompiles the compiler sources from the checked seed (~400 s, once per Release and Debug
build), and the asset compiler facade compile (~500 s inside the Debug graph step). Loading the
facade (where prepared record validation runs) takes ~2 s, so validation is not the cause. A fresh
seed removes the recompile.

## Main's remaining reds (coordinator, 2026-10-30 08:45 UTC)

On ce7e95f20 (run 41024515119) builds pass on Linux and macOS: Release 16 min, cold for compiler
sources. Debug takes 2 min. Two kinds of test still time out: the build-timings test (180 s) and
build_sdk.c (300 s, "staged compiler resources failed recovery"). They run the recovery check, which
never uses the cache, against a seed older than the sources. A seed refreshed from current main
fixes both, and is goal item 7. Owner: CI keeper. Don't chase these in your PR.

## CI speed, round 2 (coordinator, 2026-10-30 19:30 UTC)

Run 41108743062 (a7a1d1ab4, pinned host from 908c5f37a):
- compiler load 2.9 s, cached (3b0156e2f: builds load stage one; the proof stays in the check path)
- build-program compile 22 s cold, 0.2 s warm (908c5f37a: compiled build programs cached)
- Graph Debug optimization 345 s → 97 s; Debug build 96 s → 36 s; Graph SDK job 22 → 7 min
- Remaining: the test-selector step, 1,601 s Linux / 1,338 s macOS, almost all the failing chibi CLI suite
  timing out (lang-ux); the asset facade compile, 325 s of the Release build (strings-ints).

## CI speed (coordinator, 2026-10-30)

Main's Linux Release build took 42 min, then timed out at 45: four nested build-host starts each
recompiled the compiler from the seed, about 520 s each (2,090 of 2,515 s). Fixes:
- #911 (bd6977cb3): build hosts keep recovered compilers in `.chibi/compiler-cache`.
- 6acdb392c: CI restores and saves that cache. ccache, the asset compiler and the compiler cache
  are now saved once the Release build passes, not only when the whole job does.
- f89a7e5f9 and ce7e95f20: pin build hosts that cache; 062d54e04 keys entries without the host binary,
  so every host in a build shares one entry. A build now recovers at most once.
Don't add steps that start a build host without the cache. Read a run's `build-timings`
artifact (`compiler-load` rows) when CI gets slow again.

## Fixed today

- #930 (5ca1b4965): `test/language/ui.c` still listed in Targets.chibi after 610f0d216 archived it, and
  `host_packages_compiler.c` used `handle_native` without its declaration (e31d746d5).
- 5bba17cc3: set-but-unused `image_size` in compositional_records.c (`-Werror`).
- #914 (dea57daa9): closure layout-work budget 512 M → 1 GiB (`prepared authored layout work limit
  exceeded` in project:asset_source while packaging the asset compiler).
- 317e1f21e: `RejectPackages` returns a `Rejection` union ("result type is not supported yet" at
  asset_compiler/Compile.chibi:135). Replaced #902's restored `Result`.
- 491f8d637: `file == )` in tools/graph/engine/Targets.chibi ("Expected an expression" with no
  location, after the Chibi parser switch #867).
- #909: trailing commas in the graph's source lists.
- #876: dropped the `--root-state` selector archived with the formatter (undefined
  `test_language_root_state`).
- #873: archived graph libraries removed from LinuxLinks.chibi (missing `libchibi_graph_*.a`).
- c923b833f: `reachable[-1]` for #839's package-less `char` type (bootstrap "compiler execution did
  not return"). #858/#869 make that failure name its kind and source position.

## Open

- Fixed (coordinator, 3065d8865): the stage-two "array index out of range" was #870's CallStep finish-marker
  collision (labeled call and char conversion both 8). Labeled calls are 5 now. Still rejected by stage one:
  2 fallible-pointer samples and the optional-mutation guard sample (errors), and the `when` constant-condition
  sample (generics). builtin-math names all CallStep markers.

- Vector natives in the bootstrap test: stage two stops at step 21 with "array index out of range"
  on a game sample (owner: builtin-math).

## Rules

Merge first and fix forward. When you archive a file, delete the whole block that names it. #910
adds locations to parse errors.
