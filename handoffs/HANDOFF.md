# Chibi handoff: start here (coordinator, 2026-11-02)

This is the single entry point. It links the per-session handoff files and the longer notes. The `handoffs/` folder
is local to this machine and isn't in any git repository.

## State at handoff

- origin/main is 8c76630ee (#1318). Its CI run, 41360645385, was queued at handoff.
- On 2026-11-02 the owner paused all work and cleared the goal (`/goal off`). No session is running.
- Every session's branch is committed and pushed, and its worktree is clean. The coordinator checked every worktree on
  2026-11-02. Work in progress is in draft PRs, listed below.
- Chibi is native-only (owner, 2026-11-01). The VM is frozen and gets deleted once the gates below are met. The owner's
  original goal, its nine items and the rules are in [goal.md](goal.md) and at the top of [main-status.md](main-status.md).

## Goal evidence so far

Each item must be shown again on one final main commit.

| Item | Best evidence | Still open |
|---|---|---|
| 1 Native stage two = stage three | run 41328023884 on 05ebca0ab (42292a69…aede); seed run 41357775253 on 60901f2fa (a012df37…) | Rerun on the final commit |
| 2 No C compiler in CI | no-c-compiler.yml, run 41347787824 on 910eff2c1 | Rerun on the final commit |
| 3 `chibi build` only, no CMake/Make/Python | 0 such files outside archive/ (89ec4cc8f) | The C build host is still used (gate 3) |
| 4 No first-party C | 338 files remain, mostly the VM | The VM deletion series ([vm-deletion.md](vm-deletion.md)) |
| 5 Generics, comptime, `foreign import` | chibi-test-samples-comptime and types, run 41347787818 on 910eff2c1 | Rerun on the final commit |
| 6 Edit and Continue | native-build cases pass, PR run 41356793643 (names logged since #1313) | Rerun on main |
| 7 Seed as a Release asset, recovery | run 41347808206 on 910eff2c1; fresh seed pins in #1326 | Merge #1326 |
| 8 CI green on Linux, macOS and Windows | Linux and macOS green | Windows: #1315 |
| 9 Native faster than the VM | #1309's run 41347860810: 54 s against 168 s | Rerun on main after #1326 |

## Resume in this order

1. Merge **#1326**, the fresh native seed pins. Recovery and native-self-host then reach the fixed point in about a
   minute. Collect items 1, 2, 7 and 9 from that main commit.
2. **#1315**, Windows self-build: read run 41358702975's windows-2022 job, then fix forward. When it's green, the CI keeper
   adds windows-x64 to native-seed.yml.
3. Gates for deleting the VM, each with one owner. They're listed in [sessions.md](sessions.md) under "Gates for
   deleting the VM", and the details are in [vm-deletion.md](vm-deletion.md) sections 6 and 9:
   - gate 2: native driver parity, #1324 (profile);
   - gate 3: the native build runner deletes `chibi-build-host`, after #1326 (records,
     [build-host-deletion-map.md](build-host-deletion-map.md));
   - gate 4: a native `chibi` with run, test and `--watch`, #1323 (lang-ux), plus Edit and Continue retaining records
     and strings, #1319 (errors);
   - gate 5: delete the packaged package compiler, with gate 4 (old-formats);
   - gate 7: native maps, #1325 (core-math);
   - gate 8: native tasks, #1320 (builtin-math). This waits for the owner (see below);
   - producer gaps: #1321 (sdk-cmake).
4. Then the deletion series A to G in vm-deletion.md section 6. core-math leads it, and the CI keeper takes step A.

## Open PRs

| PR | Branch | Owner (handoff) | State and next step |
|---|---|---|---|
| #1326 | claude/pin-native-seed-60901f2fa | CI keeper ([handoff](handoff-ci-keeper.md)) | Green, ready to merge |
| #1315 | claude/windows-self-build | strings-ints ([handoff](handoff-windows-self-build.md)) | Draft. Run 41358702975 in flight; stage three = stage four on Windows not yet known |
| #1325 | codex/native-maps | core-math ([handoff](handoff-core-math.md)) | Draft, first version with a shifting delete. v2 (compact dict) is prototyped in [native-maps-prototype/](native-maps-prototype/) and not yet run or ported |
| #1324 | claude/native-driver-options | profile ([handoff](handoff-profile-and-cut-the-comp.md)) | Draft, doesn't compile: Main.chibi:162 uses the keyword `library` as a variable name |
| #1323 | claude/native-chibi | lang-ux ([handoff](handoff-lang-ux.md)) | Draft, doesn't compile: rename Cli.chibi's `Compile` |
| #1322 | cwaldner/scalar-producer-tests | old-formats ([handoff](handoff-old-formats.md)) | Draft: the target description as test data only; the tables are not written |
| #1321 | claude/native-gaps | sdk-cmake ([handoff](handoff-sdk-cmake.md)) | Draft. Float negation done; `&point.x` (case field_reference) still rejected |
| #1320 | codex/native-tasks-prototype | builtin-math ([handoff](handoff-builtin-math.md)) | Draft, untested, not for merge until the owner answers #1284 |
| #1319 | claude/shared-programs | errors ([handoff](handoff-errors.md)) | Draft, unbuilt: scalar_llvm EmitShared |
| #1290 | perf/ci-real-case-timings | Not a linked session; no local worktree | Open |
| #1284 | codex/native-tasks | builtin-math | Design ledger, waiting on the owner |

Pushed branches without a PR: `cwaldner/chibi-formatter` (`chibi fmt`), and `cwaldner/embedded-cli-programs`, the CLI
embedded in the launcher, which #1323 builds on.

## Sessions

Message a session with SendMessage, using its id. Each one's next step is in its handoff file.

| Name | Session id | Handoff |
|---|---|---|
| cleanup-781 (CI keeper) | local_76d6748b-121f-4a3e-b441-926d5b3a50f0 | [handoff-ci-keeper.md](handoff-ci-keeper.md) |
| strings-ints | local_7a5d9239-f797-4568-a97d-409f481731be | [handoff-windows-self-build.md](handoff-windows-self-build.md) |
| core-math | local_0bc59849-4eab-44e0-bb66-3fcb77770d42 | [handoff-core-math.md](handoff-core-math.md) |
| profile-and-cut-the-comp | local_abadb27a-d55d-4774-bd11-fc6a8fa352db | [handoff-profile-and-cut-the-comp.md](handoff-profile-and-cut-the-comp.md) |
| lang-ux | local_5bb0d058-8ce1-4555-a321-2bd6db5d5bd3 | [handoff-lang-ux.md](handoff-lang-ux.md) |
| old-formats | local_5090eb91-c7c3-41a8-94f1-19abc87f5a22 | [handoff-old-formats.md](handoff-old-formats.md) |
| sdk-cmake | local_9bda275e-1395-469b-ae04-647849e6adf5 | [handoff-sdk-cmake.md](handoff-sdk-cmake.md) |
| builtin-math | local_ceeb0a96-9221-4dbb-b7e7-91f09781ca8d | [handoff-builtin-math.md](handoff-builtin-math.md) |
| errors | local_ce0f557f-e494-4e1e-846a-55c958e66c00 | [handoff-errors.md](handoff-errors.md) |
| records | local_65cdd2c1-7142-4d58-9437-d99f0f3df8e6 | Nothing open: #1299 is merged. Next is gate 3, after #1326 |
| generics | local_c07d14d6-d2a7-40b3-96a5-fd90a6426bed | Nothing open: comptime #1298, #1301, #1302 and #1305 are merged |
| coordinator (this file) | local_f1eb6bfd-a7f4-4dd1-bb72-c9d99532e62c | This file and [main-status.md](main-status.md) |

## Decisions waiting on the owner

- #1284, native tasks:
  1. Who owns tasks: the program, with scopes added later as library code?
  2. What happens on an Edit and Continue swap: do suspended tasks finish in the old code?
  3. Is LLVM 22 the minimum?
- Native `format_text` specifiers: `{0}`, `{:.2f}` and `{:x}`. The VM has them; native has only `{}`.
- Integer `abs`, `min`, `max` and `clamp`; IEEE special values in `core:math`.
- Whether to push the old local-only work to origin, under backup/ branches. The repo is public.

## Only on local disk

- **Backups** of old work that is not on origin: ~/Personal/chibi/scratch/worktree-backups-20261101-full/ (see its
  README). It has patches and untracked files from 67 old worktrees, plus a bundle of the 81 local-only branches.
- **The owner's checkout**, /Users/cwaldner/Personal/chibi/chibi, on codex/non-code-cleanup-20261027: 16 uncommitted
  paths and 1 unpushed commit. Untouched by agents.
- **Old dirty worktrees** under .worktrees/ (Codex branches from Oct 10 to 28) are left in place; their changes are in the
  backup.
- **Worth keeping**: [native-maps-prototype/](native-maps-prototype/) (core-math's v2 map), and ~/ec-h (errors' Linux
  cross-build harness, which can be rebuilt).
- **Disposable**: everything under /tmp, such as /tmp/w7, /tmp/w22-*, /tmp/ci-keeper, /tmp/compiler-self-speed and
  /tmp/coord-*. Each handoff file names what it had there. A reboot wipes it.

## Other notes in this folder

- [main-status.md](main-status.md): main's history and CI notes; the newest are at the top.
- [sessions.md](sessions.md): who owns what, seed and pin rules, and how to wait (heavy.sh).
- [vm-deletion.md](vm-deletion.md): the VM deletion map, its gates and landing order.
- [build-host-deletion-map.md](build-host-deletion-map.md): what still needs the C build host.
