# Handoff 02: Strings/integers migration to authored tests (PR #821)

Codex session `01a180c6-c4ac-7941-84c4-24e8cca4bb62` (spawned by the Coordinator, session `01a180b0-3773-78a1-beeb-03420dfa7a21`). Active 10-27 22:31 to 10-28 21:03 local (MDT). Log timestamps are UTC (MDT + 6h). State below was verified 10-28 about 22:55 local.

Log: `/Users/cwaldner/.codex/sessions/2026/10/27/rollout-2026-10-27T22-31-12-01a180c6-c4ac-7941-84c4-24e8cca4bb62.jsonl` (33 MB, single copy). Only three real user messages exist (the plan approval and two bare "Continue" nudges from the owner's phone). All other direction came from the Coordinator through `send_message_to_thread`.

## 1. Goal and scope

The Coordinator's spawn brief: "Own a new independent batch of C regressions migrated to authored Chibi @test." Its first turn had to be plan-only and stop for confirmation. The owner then answered:

> "Approved. Proceed with the strings migration in an isolated worktree and coordinate shared integration with the central compiler-test owner. Retire the C cases only after combined validation passes. Once done, move onto any other tests that can be safely expressed in chibi."

Authorizations relayed by the Coordinator (quoted from its messages; they were given to the Codex ten-chat setup, so get the owner's go-ahead before relying on them as a Claude agent):
- "Yes, coordinate all ten chats."
- "Work as autonomously as possible please. You may commit, open PRs, review, and merge on my behalf."
- "keep reviewing and merging completed work"; "authorized cleanup of finished chats/worktrees."

What the thread did, in order:
1. Strings: 174 `core:strings` rows plus 16 formatting rows moved to `test/core_strings/*.chibi`. Five C cases retired. Merged as #789.
2. 12 Markdown rows prepared but deferred (stage-one cannot compile a named array result). Their 14 C cases are intact.
3. Audit for the next slice, then the integer slice: 73 authored tests (`test/integer_arithmetic/Arithmetic.chibi`). Two C cases (118 lines) removed from `test/language/packed.c`. This is PR #821 (open, ready, green).
4. Coordinator-assigned repairs that blocked the lane: #799 (policy fixture readiness/membership recorder, merged) and #811 (PowerShell scalar transport fixture that blocked #803, merged; #809 closed as superseded).
5. A fixture correction inside #821 (private-host yield test).

Out of scope, per the Coordinator: production compiler/runtime, prepared format, seeds, bootstrap regeneration, new-package tests (portable-library chat), native ABI/ownership/failure/diagnostic tests (these stay C). The Coordinator's last instruction on this: "No new port batch yet; finish this reviewed C deletion and its landing/cleanup first, then select an eligible unclaimed group from current accepted language capabilities."

## 2. Ownership

PRIMARY worktree: `/Users/cwaldner/Personal/chibi/.worktrees/authored-strings-regressions/chibi`
- Branch `codex/authored-integer-regressions-20261028` (PR #821). The directory name is a leftover: the same worktree hosted `codex/authored-strings-regressions-20261027` (#789, merged) first.
- Nested registered worktree `.chibi/integer-migration/current/accepted498-source` (detached `498f1b9ae`, clean). It was the unchanged-baseline comparison input.

Finished, no longer primary (do not recreate):
- `.worktrees/scalar-powershell-fixture` (#811): removed. Archive with `RESTORE.md` at `/Users/cwaldner/Personal/chibi/.coordination/archives/2026-10-28/scalar-powershell-fixture/`. The PowerShell runtime it produced was copied into the integer worktree (`task-runtime/pwsh-osx`, 578 files verified).
- `.worktrees/policy-fixture-readiness` (#799): removed; the path is now an empty directory. Archive at `.coordination/archives/2026-10-28/policy-fixture-readiness/ARCHIVE.md`.
- Local refs kept on purpose: `codex/policy-fixture-readiness-20261028`, `codex/scalar-powershell-fixture-20261028`, `codex/policy-fixture-checkpoint-{42f,9ca}-20261028`, and checkpoints `codex/authored-integer-{checkpoint-7c6,checkpoint-7dff,pre-retirement-c62,retired-1256,27d8-1d00,yield-761e}-20261028`.

Secondary, created by this thread:
- `.worktrees/compiler-fixture-results/chibi`: branch `codex/compiler-fixture-results-20261028`, one unpushed commit `d3c476b0e` "Support multiple results in the compiler test fixture" on `286f69ddf`; clean. It is the multi-result fixture helper drafted for the results owner. Superseded: the results owner's final fixture files (physical optional result headers, `compiler_fixture_snapshots_optional_results`) replace it, and the log says the final files were hash-checked against it. Do not push it; remove the worktree once the results owner's change (#813 line) has landed.

Read-only or touched only through messages:
- `.worktrees/recoverable-errors/chibi` (results owner, never edit), `.worktrees/build-values-lifetime` (#803), `.worktrees/emit-body-finalization` (#807, archived).

Overlap with other threads (same shared files; all of them touch `test/language/main.c`, `test/language/CMakeLists.txt`, `tools/test_policy.py`, `tools/graph/engine/Targets.chibi`, so expect small textual conflicts):
- Coordinator (`01a180b0`): owns final landing, merge slots, heavy-producer slot allocation, and the ledger `/Users/cwaldner/Personal/chibi/.coordination/chibi-landing-ledger.md` (row "Migrate more C regression tests to Chibi").
- Portable libraries (`01a180c5-6082`, #795 draft): consumes this thread's fixture and `package_tests_find_collection`. #795 also edits `test/tools/build_process.c` (+95 lines at other hunks than #821's).
- Recoverable errors/results (`01a180c5-6e54`, #813 draft): owns `compiler_fixture_call_results` and `package_compiler_chibi.c`. It also needs the "authored compiler invocation" lane that #821's Chrome/Node runs used.
- Core math/collections (`01a180c6-be08`): #790 closed, superseded by #817 (merged); its math worktree edits `packed.c` at other hunks (float_math, lines ~687-709).
- CMake retirement (`01a184bd`): owns `docs/self-hosting-build.md` (#821 edits the yield paragraph), #814 (merged), #818 (open, ready; response files for long Graph links).
- Generics #810, compositional records #808, builtin math, host clock #784, main-runner-reload: no semantic overlap; only the shared registration files.

## 3. Verified state (what I saw)

Worktree `.worktrees/authored-strings-regressions/chibi`:
- Branch `codex/authored-integer-regressions-20261028`, HEAD `b63b55faef2bc53dae38026aa645d7cb21e22362`, tree `1a96d05b49628682e420fe44f9ef43fb3ea2c607`. `git status --short` is empty (`.chibi/` is ignored).
- 8 commits ahead of `origin/main`, 2 behind. Local `origin/main` is `d66cb3bebf33ff3c4e21ef53e3f3990a67ecc60c` (#816). Merge base is `907b1fe02` (#814).
- Branch tip equals the PR head on GitHub. Codex is no longer running in it (no processes).
- `.chibi/` is 4.4 GB. Disk: 24 GiB free (95% used). `git stash list` is shared by all 81 worktrees; never use stash.

PR waldnercharles/chibi#821 "Migrate integer arithmetic regressions to authored Chibi tests": OPEN, not draft, mergeable CLEAN, 12 files, +1040/-145.
- Checks on `b63b55fa` all green: normal CI run `40809763501` (Linux 31m19s, macOS 20m7s, Graph SDK Linux relocation, Studio startup, clang-format, native scalar macos-arm64/windows-x64) and compiler self-build `40809763540` (3 platforms plus comparison). Windows engine/Windows Studio/paired Studio were skipped by configuration.
- Reviews: waldnercharles COMMENTED (id `5692414235`, root's review of `b63b55fa`); Codex connector review summary: completed, no findings.
- Commits: `7dfffc801f` additive 73 tests and registrations; `2dbb4884b2`, `c62cbff648` accepted-main merges; `1256be8696` retire the two C cases; `1d00bcad56` accepted compiler reachability (#817, compiler 27d8); `5c22ba254a` accepted private build-host lifetime (#803, protocol 14); `761e2d0d30` yield fixture repair; `b63b55faef` accepted #814.

Disagreements with the log:
- Log's last message: "Fresh CI is running." Now: finished, all green.
- Log/ledger: CI ran on base `907b1fe0`. Main has since advanced by #816 (`d66cb3be`, merged 10-28 21:42 local, after #821's CI started at 21:01). #816 edits `tools/test_policy.py` (a different hunk, the `PARALLEL_RESOURCES` allowlist), `test/tools/test_policy.c`, `test/language/generic.c`, `docs/test-parallel-resources.md`. GitHub reports no conflict. The #821 + #816 combination has NOT been tested.
- The global handoff table lists `policy-fixture-readiness` and `scalar-powershell-fixture` as thread worktrees. Neither exists now (see section 2).

Related PRs: #789 MERGED `286f69ddf8e118b099bdb50aa17767f24079e011`; #799 MERGED `bdfb401e8d`; #811 MERGED `0b7bedb789` (tested tree `91fd9308`); #809 CLOSED; #795 open draft; #816 MERGED.

## 4. Done and left

Done (evidence from PR bodies and `.chibi/integer-migration/` receipts):
- #789: 174 string + 16 format tests; native BYTECODE/SLJIT plus both stage-one compiler backends on both targets (six paths) pass; ASan/UBSan, relocated-SDK and installed CLI pass; wrong-golden and raw-byte negative controls fail where they should. Added `compiler_fixture` collection seam and `package_tests_find_collection`.
- #821 evidence: 73 tests x 6 paths = 438 executions pass native (47.5s), Node Wasm (25.7s), focused Chrome (all 438); relocated installed SDK consumer plus `chibi test test/integer_arithmetic` pass; ASan/UBSan pass (240s); structural witness (48 typed helpers, 72 callers, six paths) and three negative controls (constant helper, removed call, wrong wide golden `54043195528445953`) fail as intended; retained `--row-array` 18 cases/17,701 assertions (was 20/17,853, difference is exactly the two retired cases). Private host `build-process` 49/49 cases, 9508 assertions; toolchain 11/11, 165 assertions; policy 70/70, 1012 assertions; after merging #814: package/Make fixtures 7/7 (125 assertions), native-profile 7/7.
- Evidence binding (do not relabel): numeric and installed-SDK runs are bound to `1d00bcad`, compiler SHA256 `27d8f3d0568e442e1a70b167c04ba97c671ea0d0b1e85e3a7493d88fc59bb912`, runtime manifest SHA256 `e1adf32b577f9a20993e9db40d4c3777d2b3a8325d1bb8a19a57bf2256f23348`. The yield repair was tested at staged tree `3a706346` and committed as `761e2d0d`. Frozen packets: `.chibi/integer-migration/current/frozen-1256-e933/` (31 checksums), `frozen-1d00-27d8`, `frozen-5c22-protocol14`, `frozen-761e-yield-repair`, `frozen-b63-accepted907`. PR body text: `current/integer-pr-body-final.md`.

Left, in order:
1. Land #821. The Coordinator owned landing ("root owns final merge"); its pattern was: finite CI watch, re-audit base/holds/server rules, expected-head merge, then check the merge tree equals the tested tree and record post-merge CI. Decide with the owner whether to (a) merge as is (GitHub says clean) and watch post-merge CI, or (b) refresh the branch onto `d66cb3be` first. If you refresh, new head = new CI; add the `ci-rerun` PR label to rerun failed jobs (see `.github/workflows/ci-control.yml`, `docs/testing.md` ~line 283). Whichever you pick, run the focused policy checks from section 6 on the combination, because #816 changed the policy allowlist and `test_policy.c`.
2. Post-merge cleanup of the 4.4 GB scratch (section 6). Archive receipts first, verify restore, then remove, following the Coordinator's archive pattern (`ARCHIVE.md`, `REMOVAL.json`, `SHA256SUMS`, `RESTORE.md`, `path-mapping`). Remove the nested `accepted498-source` worktree with `git worktree remove` before the parent.
3. Next batch (nothing started; Coordinator said select an unclaimed group only after #821 lands). Candidates from `.chibi/integer-migration/current/next-safe-batch/ASSESSMENT.md` (assessed on `498f1b9a`, source-only, no builds):
   - Markdown, 12 rows plus `docs_markdown_renders_tables` (19 C lines, `docs_markdown.c:526-542`). Blocked: stage-one `FunctionSignature` rejects `Page`'s named `string[]`/`int[]` results (`Compile.chibi` ~1734-1747: "named result type is not supported yet"). Drafts: `.chibi/strings-migration/markdown-pending/additive.patch`, `retire-markdown-c.patch`. Reassess only after the results owner's named-result support (#813, draft) is accepted on main. Markdown has no Node/Chrome selector; a port must state that gap.
   - Not ready: `typed_compile_time_values_preserve_width_and_float_remainder` (needs analyzer materialization), the float narrowing/widening cases (raw-bank witness), `float_math...`/`wide_literals...` (math owner), `generic.c compile_time_calls_follow_runtime_semantics` (generics owner).
   - Unit algebra (48 rows), type relations (70+33+9 rows), constant proof: assessed 10-28 before #694 merged and flagged as #694-owned or as losing native differential oracles. Re-audit before claiming them; `unit_algebra_chibi.c` retained-data/191-byte-string ownership case must stay C.
4. Optional polish: `docs/testing.md` documents the strings selector (lines ~65, ~371) but #821 added no entry for `language-integer-arithmetic`/`wasm-integer-arithmetic` (only `test/integer_arithmetic/README.md`).

## 5. Holds, freezes, coordination rules in force

- Published head `b63b55fa` is the reviewed, frozen candidate. Do not push other commits to it without telling the owner; any push invalidates root's review and the CI attribution.
- Keep evidence attribution exact: old results stay tied to their own commits (`7dfffc8`, `c62cbff`, `1256be8`, `1d00bcad`, `5c22ba25`, `761e2d0d`). The Coordinator: "do not relabel post-hoc hashes as pre-run observations"; do not repeat the unchanged numeric matrix or a full SDK build solely for a new base. Bind carried proofs to exact relevant inputs and let CI cover the combined build.
- No protocol-14 SDK repackage is claimed. Its freshness guard refused a stale generated-header dependency. Partial products of an interrupted broad `test_tools` build (it pulled in asset/font generators) are unaccepted and must not be used as a provider.
- Current accepted pins for this lane: compiler `27d8f3d0...`, runtime E1 (manifest `e1adf32b...`), private build protocol 14 (#803, `79743c56`), prepared format 26. No artifact, runtime, seed or SDK regeneration; no `ci-seed` label; no publication. `CHIBI_PROJECT_VERSION` bump is not applicable (tests only).
- Do not overlay held or unaccepted code via merge, cherry-pick or dependency (chibi `AGENTS.md`): #795, #801 (capability-effect/feature publication holds), #813, #810, #808, #791, #784. The integer package has no `#795` dependency (it imports nothing; uses typed local helpers, builtin operators and core `assert`).
- Heavy producers (SDK build, full sanitizer build, authored compiler runs, full Graph) were serialized by the Coordinator across all chats (JOBS=2, WEB_THREADS=0). Before launching one, check with the owner/Coordinator that none is running.
- Do not edit the results owner's fixture helper (`compiler_fixture_call_results`) from here (this thread's own `d3c476b0e` draft is dead); do not weaken `package_test_run` (it requires `FLOW_OK` and `returned`), do not change the 10,000-instruction per-test bound (`FIXTURE_TEST_BUDGET` default stays 20,000,000 for existing callers).
- #811 and #799 history: the PowerShell transport guard (10 s `tool_run`) and every deadline were left unchanged; never raise deadlines to make a fixture pass.
- Memory rules that apply: no `git stash` (shared list); report scratch left behind; merge own PRs when green (this PR was Codex-authored, so confirm).

## 6. How to validate

Environment (macOS arm64): clang/clang-format 22 at `/opt/homebrew/opt/llvm@22/bin/`; use Homebrew Python (3.12 shim or `/opt/homebrew/opt/python@3.13/bin/python3.13`), not `/usr/bin/python3` 3.9: the policy `test_runner` cleanup case fails under 3.9 (signal names vs numbers) and passes under 3.13; `EMSDK_PYTHON` for Wasm. PowerShell 7.x must be on PATH or the PowerShell transport tests are "unrun" (CI Linux runs them). A task-local copy exists: `.chibi/integer-migration/task-runtime/pwsh-osx` (7.6.9). Prepend it to PATH, keep the rest of PATH.

Commands used (all from the worktree root; set `CHIBI_LANGUAGE_BUILD_DIR=<dir>/test/language` or `CHIBI_WASM_BUILD_DIR=<abs dir>` as needed):
- Native build: `make build BUILD_DIR=<d> BUILD_TARGET=test_language BUILD_PROFILE=debug JOBS=4 LOCAL_CMAKE_FLAGS='-DCMAKE_BUILD_TYPE=Debug -DCHIBI_ENABLE_SANITIZERS=OFF'` (about 340 s cold). Run: `python3 tools/test_policy.py tool language-integer-arithmetic` (73 tests x 6 paths, about 48 s); also `tool language-compiler-fixture` (9/414) and `tool language-row-array` (18 cases).
- Sanitizer: same with `BUILD_PROFILE=sanitizer BUILD_HOST=<release-hosts>/sdk/chibi-build-host ASSET_GENERATOR_HOSTS=<release-hosts>/sdk`; build Release hosts first (`BUILD_TARGET='chibi-build-host chibi-compiler-host' BUILD_PROFILE=release`), otherwise the asset compiler is regenerated under ASan and takes most of the time. Cold 200-400 s; run 240-380 s.
- Wasm: `make wasm-language-build WASM_BUILD_DIR=<d> WEB_THREADS=0 JOBS=2`; then `tool wasm-integer-arithmetic`, `tool wasm-row-array`. Chrome: `python3 tools/test_policy.py tool wasm-browser` (needs a `make tools-build` driver; set `CHIBI_WASM_TEST_ROOT`).
- Tools: `make tools-build TOOLS_BUILD_DIR=<d> TOOLS_C_COMPILER=/opt/homebrew/opt/llvm@22/bin/clang JOBS=2`; `python3 tools/test_policy.py native <d>/test_tools --validation-plan` (5/84, 8 s) and `--test-runner` (70 cases, 90-120 s). Private host: `tool build-process` with `CHIBI_TOOLS_BUILD_DIR` and `CHIBI_BUILD_HOST` (49/49, 9508 assertions, 25 s). The plain tools build lacks the private-host tests; do not trigger the full `test_tools` target (it dragged in asset/font generation).
- Installed SDK: `make sdk-build SDK_BUILD_DIR=... SDK_REFLECT_BUILD_DIR=... SDK_CMAKE_FLAGS='-DCMAKE_BUILD_TYPE=Debug -DCHIBI_ENABLE_SANITIZERS=OFF'` (about 235 s), `tools/package_sdk.py --build ... --kind sdk`, move the prefix, configure `test/language` with `-DCHIBI_INSTALLED_SDK=ON -DChibi_DIR=...` and run `<prefix>/bin/chibi test test/integer_arithmetic`.
- Format/assets: `make check-format`; `--project-root test/integer_arithmetic --format --check` (Codex ran it with the SDK-built `chibi-compile`; repo `CLAUDE.md` names `build/bin/chibi_ctl`).

Not run (state this honestly if you report): broad `make test-native/test-materials/test-projects/test-language-all` locally (CI covered them on the pre-#816 base); Linux/Windows/GPU locally; the original broad Chrome acceptance (execution 82/83, program status 1 with truncation, query-program 87/88, row-builder status 2 all fail identically on accepted `498f1b9a`; only focused integer and row-array Chrome runs pass); protocol-14 SDK repackage.

Slow/flaky: cold native and sanitizer builds (5-7 min); Chrome acceptance is a known-red baseline; macOS CI "facade generation" timeouts hit #799 and #811 before fixtures ran (passed after rebasing on newer main, never waived); Linux transport fixture takes about 8 s against a 10 s guard; long worktree paths can break the Graph `test_tools` link (71,540 argv bytes against the 64 KiB admission). #818 (response files) is the fix; until then use a short `BUILD_DIR` and record that it is not path parity.

Scratch to clean after landing (report it, per chibi `CLAUDE.md`):
- `.chibi/integer-migration/` 4.3 GB: `current/` 2.6 GB (`sanitized` 808 MB, `sdk-build` 447 MB, `sdk-relocated` and `sdk-27d8-relocated` 207 MB each, `release-hosts-cmake`, `frozen-1256-artifacts` 161 MB, nested `accepted498-source` 136 MB), `historical/` 861 MB, `sdk-build`, `sdk-relocated`, `task-runtime` 184 MB, `release-hosts`; `.chibi/strings-migration/` 12 MB (logs, Markdown patches, `KNOWN-GAPS.md`, `RESULTS.md`); `.chibi/logs`. Keep the `frozen-*` packets, `RESULTS.md`, `CURRENT-VALIDATION.md`, `runs.jsonl`, `integer-pr-body-final.md` until archived.
- `/Users/cwaldner/Personal/chibi/.worktrees/policy-fixture-readiness` is an empty leftover directory.

## 7. Risks, open questions, what went wrong

- Untested combination: #821 + #816 (policy allowlist, `test_policy.c`). New `language-integer-arithmetic` is exclusive by default (not in `PARALLEL_RESOURCES`), which is the intended outer-exclusive behavior. Check any test that counts the language group (CI showed 52 selectors on main; this PR makes 53).
- Open: who lands #821 now that the Coordinator thread is stopping, and whether the owner wants a refresh onto `d66cb3be` first.
- Stage-one limits found (recorded in `.chibi/strings-migration/KNOWN-GAPS.md`; compiler owner has them): implicit-void `@test` returns leave `returned` false, so every authored `@test` ends with an explicit `return;` (the original failure looked like budget exhaustion at steps=1; a failing negative control proved the assertions run); untyped numeric literal in variadic `format_text("{}", 37)` is rejected, so format rows use typed locals (parity for direct untyped calls is not claimed); Markdown named array result.
- Resolved incidents: worktree tool failed ("Not a git repository") because the chat pointed at the workspace wrapper, so the worktree was made from the nested `chibi/` repo; contradictory Markdown C assertion (built-in type highlight then absent) repaired in `docs_markdown.c`; this thread's results-bank helper `d3c476b0` missed optional outputs (found by the results owner, who fixed it; this thread reviewed the correction source-only); Wasm blockers (`pin_string.owner` initializers fixed in #795's branch, compiled compiler exceeded the 65,536-instruction Wasm guard at EmitBody 68,898 instructions, fixed by #807 without raising the guard); two policy-fixture races fixed by #799; scalar PowerShell fixture timeout fixed by #811 (symbol hashing and `Get-FileHash` overhead); `TEST_JOBS=2` leaking into tooling runs; interrupted broad tools build; stale yield expectation in `test/tools/build_process.c` (the current prepared compiler supports yield, so direct yield must hit "a build program cannot wait" and unawaited `Wait()` must hit "a build program cannot start a task"; fixed in `761e2d0d` with exactly-one-entry on the selected backend and zero on the other).
- Evidence gap that was repaired: the first receipt packet left `final_validation` "pending" and rows naming `c62` without tree/log hashes. The frozen `1256/E933` packet marks binary hashes as post-run observations. Keep that distinction in any new claim.
- Broad Chrome acceptance stays red at baseline; it must not be presented as passing.

## 8. First 30 minutes

1. Confirm with the owner that the Codex threads (this one and the Coordinator) are stopped, and that you may land/clean up. Do not touch the worktree otherwise.
2. `git -C .../authored-strings-regressions/chibi status --short` and `rev-parse HEAD` (expect empty and `b63b55fa`); `gh pr view 821 --repo waldnercharles/chibi --json state,isDraft,mergeable,headRefOid,statusCheckRollup`.
3. `gh api repos/waldnercharles/chibi/commits/main --jq .sha`: if it is no longer `d66cb3be`, diff the new commits against the 12 files of #821 (`gh pr diff 821 --name-only`).
4. Read `.chibi/integer-migration/CURRENT-VALIDATION.md`, `RESULTS.md`, `current/integer-pr-body-final.md`, and `.coordination/chibi-landing-ledger.md` (row for #821 and its numbered item 2).
5. Decide merge-as-is vs refresh with the owner; if refreshing, work in the same worktree (no stash), keep the branch name, then run `--validation-plan`, `--test-runner` (with pwsh and Python 3.12/3.13), and `tool language-integer-arithmetic` plus `tool wasm-integer-arithmetic` only if inputs they depend on changed; request CI via the `ci-rerun` label; record the new head/tree.
6. Before any heavy build: `df -h` (24 GiB free), no other Codex/Claude producer running, JOBS=2.
7. After landing: verify merge tree equals tested tree, record post-merge CI, then archive receipts and remove the worktree (nested worktree first); update the ledger row.
8. Then start the Markdown/next-batch reassessment only if the owner asks (section 4 item 3).
