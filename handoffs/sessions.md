# Who owns what (coordinator, 2026-10-30)

> **Start at [HANDOFF.md](HANDOFF.md)** (2026-11-02): it is the single handoff index; all work is paused.

Message the owner directly with SendMessage. Message the parent coordinator
(local_f1eb6bfd-a7f4-4dd1-bb72-c9d99532e62c) only for owner decisions, or when no session owns the work.
At the start of each turn, read `goal.md`, `main-status.md` and `focus.md` in this folder. They hold the
current priorities and main's state.

**Merge first (owner's goal rule).** Merge a PR as soon as your change builds and its own new checks pass
locally, or as soon as its CI failures are the same ones main has. Main's reds are not your gate, so don't
keep re-merging main to wait for green CI. Fix forward whatever your merge breaks. The pre-commit hook
(`.githooks/pre-commit`, clang-format 22 on staged C) runs in every worktree again since 2026-10-30
(core.hooksPath is `.githooks`). Don't bypass it with `--no-verify`.

**Archive rule (owner, 2026-10-30).** `archive/` keeps only code a Chibi version will still replace. Once
Chibi replaces an archived file, for example a C differential ported to `@test`, delete it from `archive/`
and update `archive/README.md` in the same PR.

**LLVM lane (coordinator, 2026-10-31).** The Emit.chibi window is no longer exclusive: sdk-cmake (W7), core-math
(W10/W22) and errors (native Edit and Continue) may write in the LLVM lane at once. Keep PRs small, merge them as
soon as they build and their new checks pass, and rebase rather than wait.

**Syntax kinds are integers (int-kinds, 2026-10-31, #1114, 5c4069828).** `Is`, `Field`, `HasToken` and
`reader.Field/Kind` take `syntax.CallExpression`, `syntax.FieldName` (syntax_diagnostics) and `tk.Question`, not strings;
`parser.KindName(kind)` spells one for a message; `raw.Parse` also returns `.Rejected(diagnostic)`. Compiler code still
written with `"call_expression"` fails to type-check after a merge; convert it the same way.

**Decisions (coordinator, 2026-10-31).** No incremental closure compile for the asset facade: it already has a
content cache, and the native compiler removes the cold cost. The src/language archive is split: coordinator
(source_package_* consumers), host-clock (test/language C tests), strings-ints (syntax side, then tree-sitter).

**Assignments (coordinator, 2026-10-31 05:30 UTC).** records: replace compiler_json_string and
compiler_json_value_string with Chibi (W14, 33 functions), then the native build runner. portable-libs: the engine
C archive, including tools/reflect (about 197 files left after #1081), then core:os. builtin-math: land #1029, #1045
and #1074, then `u8[]` as pointer plus length. core-math: the W22 sweep and the stage-one driver. sdk-cmake: W2,
optionals, W10 by-reference (#1084). 5221dd88c raised the compiler artifact bound to 64 MiB (the asset compiler
closure had passed 32 MiB).

**Shared compiler cache (coordinator, b4caf8074, 2026-10-31).** Recovered compilers now live in
~/.cache/chibi/compiler-cache (2 GiB budget, oldest written first; seeded with the newest 71 entries from every
worktree). After rebasing onto b4caf8074, delete your worktree's `.chibi/compiler-cache`. Disk is at 14 GiB free and
/private/tmp holds 12 GB of session scratch: delete your scratch directories once their check has run.

**Parallel native census (coordinator, 030152602, 2026-10-31).** `test_language --native-census` now forks
workers after the compiler loads (CHIBI_CENSUS_JOBS, default min(processors, 8); about 1.3 GB each). A full run took
2,124 s with 8 workers, against 3+ hours serially, and its report is in source order for any worker count. Its result
on 0be29c417's tree: **1,051 of 1,296 functions pass the producer**; W10 pointers block 197 and W14 native calls 35.
Rebase before your next census. A copy of the report is at /tmp/coord-census-report-1051.md.

**How to wait (coordinator, 2026-10-31; owner asked to cut idle time).**
- Heavy builds and benches run through `sh /Users/cwaldner/Personal/chibi/handoffs/heavy.sh "NAME: what" COMMAND...`,
  started in the background. It blocks until one of the 6 slots is free, runs the command, passes its exit status
  through and frees the slot on any exit; it takes over slots whose owner process died. Don't mkdir slots by hand,
  don't loop on `sleep`, and don't end a turn to wait for a slot: queue the build and do other work until its notice.
- Don't wait for CI to merge (merge-first): merge once your change builds and its own new checks pass locally,
  then watch the next main run.
- Keep your next two or three items in mind (your row below and goal.md). When one is blocked, start the next
  instead of ending the turn idle, and say which you switched to.
- Benches hold a slot for their whole run; keep them short and repeat only what the numbers need.
- Stop only processes you started, by exact pid. Never `pkill -f` or `killall` a pattern: every session shares this machine.

**Assignments after goal item 1 (coordinator, 2026-11-01 18:00 UTC).** Native stage two equals stage three on
05ebca0ab (run 41328023884). Next is the native seed, then deleting the VM. Each row below supersedes that session's
row in the table.
- sdk-cmake: item 9, producer side. The per-iteration `types = table^` copies in EmitBody, the 6 to 9 GB leak, and
  why stage three takes 1898 s against stage two's 872 s.
- profile-and-cut-the-comp: item 9, LLVM side. Split the native module and run opt and llc in parallel, `--jobs` = cores.
  Remove the driver's whole-module `build.read_file` (64 MiB cap). Files: tools/native_aot/driver and the module
  boundaries in llvm_native. Leave Body.chibi and Scalar.chibi to sdk-cmake. Work in your own worktree, never in
  /Users/cwaldner/Personal/chibi/chibi.
- cleanup-781 (CI keeper): the native seed. Publish the native compiler for linux-x64 and macos-arm64 as a Release asset
  pinned by SHA-256, and make recovery start from it (item 7). The bytecode seed stays only until the VM is deleted.
- records: the native build runner, as in docs/native-build-runner.md. Do a real engine build with the native graph,
  add the runner cache to `chibi build`, then delete the C build host.
- strings-ints: item 8 on Windows. Cross-link native stage two for windows-x64 with lld-link, run the self-build on
  windows-2022 so that stage two equals stage three, and replace the frozen-runtime Windows self-build job.
- lang-ux: item 2, after CI on #1285, #1287 and #1288. Add a job where cmake, gcc, cc and clang are unavailable, with
  `command -v` shown failing. It goes from a clean checkout through stage one and stage two with lld, then links and
  runs the `vendor:` FreeType program.
- old-formats: item 4, tests. Go through test/**/*.c (61 files) with docs/c-test-verdicts.md. Port to Chibi tests what
  checks a behavior that survives, and list the VM-only tests that are deleted with the VM.
- core-math: the VM deletion map, in handoffs/vm-deletion.md. List everything that still needs src/execution,
  src/runtime, src/types and src/host: callers, CI steps, tests and `chibi run` paths. Order it so that deleting the VM
  can land right after the native seed, and delete what is already dead.
- generics: item 5, compile-time execution of arbitrary pure Chibi. Add strings, nested records, arrays inside records,
  and floats (exact IEEE, no contraction). Stop VM-only inlining and jump-table work, because the VM is frozen.
- builtin-math: native tasks wait for the owner's three decisions on #1284.

**Gates for deleting the VM (coordinator, 2026-11-01 23:50 UTC).** These are handoffs/vm-deletion.md section 6, gate 0,
plus section 9. Each gate has one owner. Rows here supersede earlier ones.
- Gate 1, native seed: cleanup-781, in progress; Windows: strings-ints (#1315).
- Gate 2, the native driver does everything tools/native_aot's `Native` does: profile-and-cut-the-comp. That means `-g`
  line tables, shared libraries, vendored archives and ResolveLibraries, receipts and the output cache, `--target` cross
  links, and the static/dynamic choice. strings-ints also edits native_driver/Main.chibi for Windows, so merge small
  and often.
- Gate 3, the native build runner builds the engine and `chibi-build-host` goes: records, after gate 1.
- Gate 4, a native `chibi` with `run`, `test` and `--watch`: lang-ux, starting from cwaldner/embedded-cli-programs
  (pushed 2026-11-01, 9 commits over cwaldner/chibi-formatter). For `--watch`, errors makes native Edit and Continue
  retain records and strings, not only ints.
- Gate 5, the packaged package compiler (chibi_asset_compiler_host, asset_compiler_host*, package_compile.c,
  asset_compiler_packaged.c and Engine.chibi's generator step): `chibi run` and `chibi test` compile through it today,
  so it goes with gate 4. old-formats deletes it in lang-ux's native-`chibi` PR or right after it merges.
- Gate 6, the C tests: old-formats. Mostly done (#1300, #1304, #1307).
- Gate 7, native maps: core-math.
- Gate 8, native tasks: builtin-math prototypes LLVM switched-resume coroutines on a branch. It merges nothing until
  the owner answers #1284.
- Producer gaps (float unary minus, `&` of a local's field, leftover leaks): sdk-cmake.
After the gates, the deletion series A to G in section 6: core-math leads, and the CI keeper takes step A (workflows and pins).

| Name | Session | Owns |
|---|---|---|
| coordinator | local_636d1a98-8817-44c1-b7cb-142e4214f507 | stage-zero retirement (src/language, C prepare/compile in src/authoring), #884 program sessions; directs host-clock |
| host-clock | local_f7102551-def2-4c84-9964-62271c00fc32 | archiving stage-zero users for the coordinator |
| cleanup-781 (CI keeper) | local_76d6748b-121f-4a3e-b441-926d5b3a50f0 | main's health, CI, the bootstrap seed and pins |
| strings-ints | local_7a5d9239-f797-4568-a97d-409f481731be | removing all CMake (goal item 3); tree-sitter removal once src/language is archived |
| generics | local_c07d14d6-d2a7-40b3-96a5-fd90a6426bed | generic types, full compile-time execution, inlining, jump tables (`c-exit-language-features.md`) |
| records | local_65cdd2c1-7142-4d58-9437-d99f0f3df8e6 | the compiled-compiler cache (#911), linear prepared record validation, then the native build runner |
| builtin-math | local_ceeb0a96-9221-4dbb-b7e7-91f09781ca8d | the vector-pack bootstrap bug, then `foreign import` (W14) |
| core-math | local_0bc59849-4eab-44e0-bb66-3fcb77770d42 | native W12, W6, W10, census reruns; leads W22 (native stage one and two) |
| sdk-cmake | local_9bda275e-1395-469b-ae04-647849e6adf5 | native W7 arrays only (the critical path) |
| lang-ux | local_5bb0d058-8ce1-4555-a321-2bd6db5d5bd3 | `chibi build`/`run`/`test`; removing the Makefile and the remaining Python |
| portable-libs | local_815792be-ac71-46e3-8798-f72c507c83b1 | archiving the engine C the toolchain doesn't link, then `core:os` |
| errors | local_ce0f557f-e494-4e1e-846a-55c958e66c00 | Edit and Continue (goal item 6) |
| old-formats | local_5090eb91-c7c3-41a8-94f1-19abc87f5a22 | compiler samples done (347 in test/compiler/samples, `// error:` and `// fails:`); now porting test/tools C tests of Chibi build-graph code to @test |

**Seed and pin rules (CI keeper, 2026-10-31).** Put these in the same change series, or ask the CI keeper:
- Deleting or renaming a compiler source (tools/compiler, packages/core): refresh the seed (`tools/build.sh build bootstrap-artifact`, then `publish-seed`). bootstrap.json lists every source, and graph-sdk-install fails on a listed source that is gone (#1224).
- Bumping the prepared format: re-pin the first build host (bootstrap-host.yml, hosts.tsv) and the Linux and macOS compiler runtime (ci-seed), then refresh the seed.
- Changing the compiler host's native catalog: the seed is bound to the host's natives. On the PR branch, run bootstrap-host.yml while the branch still has the old seed, then cross-build the seed (the old host runs the seed compiler with the new host's --target-json through CompileClosure, and the new host must reproduce the bytes), then ci-seed. #1181 did it this way; ask the CI keeper.

Whoever breaks main fixes it. Whoever finds a break in someone else's area tells that owner with
the evidence (log lines, commit) and, if it blocks everyone, also tells the CI keeper.
