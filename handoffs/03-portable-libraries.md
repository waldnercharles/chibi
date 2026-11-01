# Handoff 03: Portable core + byte libraries (Codex session ...53d2fbb4)

Written 2026-10-28 ~22:50 MDT from the Codex log, `HANDOFF-codex-worktrees.md`, and read-only git/gh checks made at that time. The log ends at 19:59 MDT, so anything after that is only what I saw live.
Engine repo = `waldnercharles/chibi`. Studio is not involved.

- Session `01a180c5-6082-7f71-a1d1-644353d2fbb4`, 10-27 22:29 -> 10-28 19:59 MDT. Log: `/Users/cwaldner/.codex/sessions/2026/10/27/rollout-2026-10-27T22-29-41-01a180c5-6082-7f71-a1d1-644353d2fbb4.jsonl` (24 MB, 8,332 records, no other copies).
- It was spawned by the Codex coordinator (thread `01a180b0-3773-78a1-beeb-03420dfa7a21`, called "root" below), which sent 99 of the 138 incoming delegation messages the log records (the rest came from the strings, results, allocation-reporter, math, clock and vectors threads). Every hold below came from that coordinator or a sibling owner.
- The coordinator and sibling threads talk only through Codex's `send_message_to_thread`. A Claude agent cannot use that channel, so the owner (Charles) now has to relay or decide anything the coordinator used to grant.

## 1. Goal and scope

Opening brief from the coordinator, relaying the owner:
- "Own the portable standard-library program and parallel package work. The user approved expanding Odin-like library coverage as much as practical in parallel."
- "Preserve Chibi value semantics instead of cloning Odin's allocator/pointer APIs."
- "Prefer useful compatible functionality over exhaustive symbol-for-symbol parity."
- "Do not let LLVM-native completeness block VM-supported portable library work."
- The pinned Odin reference is `779c8c0a4778f994389e2a681ffc7b3cbd6c1f65`. No Odin code was copied; if you adapt any, keep the upstream notice.

The thread planned in its first turn, and the owner approved at 23:10 MDT on 10-27:
> "Approved. Implement this batch with those defaults, delegate the independent packages in parallel, and coordinate shared changes to avoid overlap with Claude and the sibling chats."

The approved batch is ten packages under `packages/core/` with adjacent `Tests.chibi` files:
- `bytes`, `encoding/endian`, `utf8`, `path/slashpath`, `math/bits`, `hash/fnv`, `hash/adler32`, `hash/crc32`, `encoding/hex`, `encoding/base64`.
- Approved defaults:
  - Buffers are `u8[]`. `strings.Bytes -> int[]` is unchanged.
  - Conversion between string and bytes is additive and copies.
  - Strict UTF-8: End, InvalidOffset, Malformed and Truncated statuses; failures consume nothing and return width 0.
  - Strict hex/Base64: no whitespace, no mixed alphabets, no partial output on error.
  - Paths are slash-only and lexical. `C:\foo` is relative.
  - Failed endian writes leave the buffer unchanged.

Later owner decisions, relayed by the coordinator and the results thread:
- "Okay, let's go with the Odin/Go style" (results thread). Fallible APIs return ordinary named values plus a final typed optional error, for example `(data:u8[], error:DecodeError?)`. There is no generic `Result`. Nil means success.
- Charles then asked that fallible functions opt in explicitly, which became `@fallible`. See section 7, because PR #813 may replace that.
- At 00:24 MDT on 10-28 the coordinator reported the owner's answers "Yes, coordinate all ten chats" and "Work as autonomously as possible please. You may commit, open PRs, review, and merge on my behalf." That superseded the first-turn no-messaging rule, but only for those ten Codex chats and only while their coordinator ran.

Scope today:
- **Five packages are split out** as PR #795: `bytes`, `math/bits`, `hash/fnv`, `hash/adler32`, `hash/crc32`. That is 34 public functions and 23 authored tests. They have no `char`, named-result or `@fallible` dependency.
- **Five packages are deferred**: `utf8`, `encoding/endian`, `encoding/hex`, `encoding/base64`, `path/slashpath`. They wait for the error convention, nominal `char` in stage one, and named results. 25 public functions need an error marker.

## 2. Ownership

| Worktree | Branch | Role |
|---|---|---|
| `/Users/cwaldner/Personal/chibi/.worktrees/portable-byte-libraries/chibi` | `codex/portable-byte-libraries-post803-20261028` | PRIMARY. The five-package feature; the live frozen checkpoint. |
| same worktree | `codex/portable-byte-libraries-20261028` (local `3200134a8`; remote and PR #795 head `dea59917c`) | PRIMARY. The PR branch; the post-803 branch descends from it. |
| `/Users/cwaldner/Personal/chibi/.worktrees/portable-core-library/chibi` | `codex/portable-core-library-20261027` (at `286f69ddf`) | PRIMARY. Holds the only copy of the ten-package candidate, uncommitted (see the warning below). |

Touched or read only: `recoverable-errors`, `authored-strings-regressions`, `array-argument-effects` (looked up the owner of #801, no edits), `stage-one-char-parity/current-main`, `build-values-lifetime`.

Two traps in the coordinator's worktree table:
- `portable-core-library` is listed as "merged: clean up" (dirty 51, ahead 0). It is not clean. It has 50 staged, uncommitted files, including the entire deferred five (`utf8`, `endian`, `hex`, `base64`, `slashpath`), `docs/package-portable-batch.md`, and 25 `@fallible` marker patches. Do not delete it. The recovery copies are `.chibi/portable-core/library-candidate.patch` (sha256 `ab634535ab911795efa3118e2d9763de4c85732a8e9ce56043b2aea4989c9560`, 50 file diffs), `.chibi/portable-core/source-manifest.json`, `fallible-markers.{json,patch}` and `fallible-test-ack.patch` in the same worktree. Do not apply `fallible-test-ack.patch` again; its changes are already in the frozen source.
- `stage-one-char-parity/current-main` is also listed "merged: clean up". It holds the reviewed nominal-`char` patch that `utf8` needs: `.chibi/char-current.patch` sha256 `980880d6cd9bbb1ef1b04c38b6d130782cc4da2a6bfe3d2fe7cf7de17b3fe0e7`, branch `codex/stage-one-char-parity-current-20261028`, uncommitted, no PR. The coordinator owns it. Do not clean it.

Overlap with other threads:

| Thread | Overlap |
|---|---|
| Coordinator | Owns landing order, the heavy-producer slot, bootstrap/SDK publication and independent review. Reserved #795 as "first feature landing". |
| Strings-integers (#821) | Shared fixture hunks: `test/language/{main.c,CMakeLists.txt}`, `tools/graph/engine/Targets.chibi`, `tools/test_policy.py`. It wants #795's leading `int compiler_backend` signature on `package_compiler_closure` and will merge additive hunks after #795. |
| Recoverable errors / error type (#813) | Supplies `@fallible` (now maybe `@error`), the result banks and `package_compiler_closure`'s migration. The deferred five depend on it. |
| Builtin math / core-math (#791 held) | Shares `Generate.chibi` and the SDK core install. Its reachability work landed as #817. Math's fixture fixes landed as #800. |
| Host clock (#784) | Adds `packages/core/time` (picked up by our recursive core install) and a separate `host_clock` target. Keep both Targets entries. |
| Generics/callables (#810), compositional records (#808) | No code overlap. Records change the prepared format, which would invalidate this thread's runtime evidence. |
| cmake-retirement | #814 (merged) moved SDK reflection onto the Graph. Our CMake registrations still merge textually; see section 3. |
| Language-UX / main-runner-reload | No overlap seen in this log. |

## 3. Current verified state (my read-only checks)

Primary worktree, `portable-byte-libraries/chibi`:
- Branch `codex/portable-byte-libraries-post803-20261028`. Working tree clean; only ignored build output.
- HEAD `c9cfe668e15896936086e0b7431d71588532b4e9`, tree `0ebc105f1fb9a08394355833afdf7a739de39a2a`. Both match the log.
- 15 commits ahead of local `origin/main` (`d66cb3beb`, equal to GitHub main now). 13 commits on main are not in HEAD.
- The feature touches 43 files against merge-base `79743c567`.
- `git merge-tree` (legacy mode, no writes) predicts a clean merge with current main. Only `sdk/README.md` and `tools/test_policy.py` changed on both sides. That is a textual prediction, not a build.

Recent first-parent history:
- `c9cfe668e` callback test (95 lines, `test/tools/build_process.c`)
- `45e2dc192` merge of #803
- `3200134a8` merge of #817
- `3d72b51f0` merge of #807
- `2d10d6fc4` merge of #804
- `4413d960d` merge of #799
- `eb2a5cfdc` merge of #797
- `47090810d` merge of #800
- `5dfe4ea62` merge of #798
- `49039a8f2` merge of #796
- `dea59917c` last pushed head
- `1dce69547` merge of #694 into the feature
- `4800a2a9d` emitter fix
- `d5ac3310e` docs fix
- `e26a3cba3` original five-package commit

PR #795 (draft, base main): `gh` shows the remote head is still `dea59917c`, mergeable UNKNOWN.
- None of the later local merges (#800, #797, #799, #804, #807, #817, #803) or the callback test have been pushed.
- Checks on `dea59917c`: 10 success, 6 skipped, 1 failure. The failure is "Graph SDK packaging and relocation (Linux)", the existing 120-second `CompilerTools` timeout.
- The only PR comment is the coordinator's note that post-#797 main passed that SDK job with an identical 20,780,474-byte compiler artifact. That is baseline evidence, not clearance for #795.

Other PRs:
- #803 merged at `79743c567` (log agrees).
- #817 merged at `a253ef4b5`. #790 is closed as superseded by #817. The log's later messages already say this.
- #801 is still draft with 8 failing checks (head `563ba6c6`). The coordinator holds the local frozen forks `a1135ab`/`6c0cee1c`.
- #821 is open and mergeable, not draft.

GitHub main `d66cb3beb` (#816) is newer than the log's last "accepted main" (`79743c567`). In between it took #814 (CMake retirement, Graph SDK reflection) and #816.
- Where `origin/main` and `HEAD` differ, 18 files changed: Makefile, CI, `Engine/Options/Profile.chibi`, `Graph.chibi`, `test_policy.{py,c}` and others.
- The thread's coordination messages never mention #814 or #816, so they were not integrated or checked against the feature.

Checks the log reports (all tied to older heads):
- On `dea59917c`: native portable suite 5 cases / 1,798 assertions; compiler matrix 4 compiler/target pairs x 23 tests; ASan+UBSan portable 5/1,798 and asset host 12/110 (22 min); SDK relocation (1,690 entries, 34-API consumer, docs); strict Wasm portable tests; Linux and macOS engine CI; compiler self-build on 3 platforms; Studio.
- Open on `dea59917c`: compiled-Wasm compiler hits the `EmitBody` 65,536-instruction guard (fixed on main by #807, not rerun); clean-checkout bootstrap recovery hits its 50-second timeout; Linux SDK 120-second timeout.
- Post-803 heads `3200134a8` and `c9cfe668e`: source-only. Independent source review found nothing (Codex reviewer `/root/bits_hash`). Nothing was compiled or run. The 27 bootstrap source hashes were confirmed equal to the accepted manifest. This is not execution proof.

## 4. Done and left (ordered next actions)

Done:
- All ten packages implemented. At `286f69ddf` they passed 65 authored tests on bytecode, SLJIT and Wasm, plus 5 native boundary cases and relocated runtime-SDK imports. That candidate is uncommitted in `portable-core-library`.
- Five-package PR #795 is open as a draft. Its CI findings were each fixed:
  - Docs generator was missing `assert`: `src/authoring/project_compile.c` now gives `render_native` the standalone catalog.
  - Native scalar emitter compiled against every shipped core package: `tools/native_aot/Driver.chibi` now uses an explicit `EmitterCoreSources` list for both compilation and the cache key.
  - 69,038-byte link command exceeded the host's 65,536-byte limit: shorten build paths.
  - Many `pin_string` initializer warnings in Wasm: now covered by merged #800.
- Native work: `bytes.ToString` uses a new owned, byte-preserving `text_from_bytes` operation. Public and values hosts expose seven text operations; the checked compiler keeps its six-operation profile. Files: `src/runtime/text_{format,definition}.{c,h}`, `src/toolchain/compiler_host.c`.
- Generator source roles in `Generate.chibi`: compiler seed inputs, then facade inputs (including the private `cost_explanations/Explain.chibi`), then the complete public core snapshot. Agreed with the allocation-reporter, math and results threads.

Next actions, in order:
1. **Decide takeover rules.** With the coordinator gone, ask Charles who grants the producer slot, the landing slot, and #801 admission. See section 5. Do not assume "merge when green".
2. **Protect the deferred five.** Copy `library-candidate.patch`, `source-manifest.json` and the marker patches out of `portable-core-library/.chibi/portable-core/`, or commit them to a local-only branch there. Never `git stash` (shared stash list).
3. **Wait for #801 admission.** The coordinator's last instruction: "putting #801's current ABI/compiler admission ahead of a full #795 SDK run, because that prerequisite changes the runtime ABI and would invalidate a freshly produced old-target SDK." Keep heavy producers idle until then. The coordinator's helper froze the #801 work at `6c0cee1c`/tree `b9a91ec3` on accepted #797, and its 21:10 MDT message says it committed the generated compiler pair locally for review. Neither is on main. Do not adopt #801 source from this thread.
4. **Re-baseline on current main** (`d66cb3beb` or later). Merge it into a new frozen head. Earlier receipts stay historical. Keep the 43-line `package_compiler_closure` helper and the public-seven/private-six text split in `compiler_host.c`. Check that the CMake and Graph registrations still match the post-#814 SDK reflection route.
5. **Run the matching-source validation plan.** It is `.chibi/portable-byte-libraries/post803-matching-source-validation-plan.json` (sha256 `2d2c8a9e27cb2433e2935ecd2de312aa96a63e603df152e29c601f82e02c125e`), with phases 0-7 for the old head. Bind it to the new head. Details are in section 6.
6. **Push to PR #795.** `dea59917c` is an ancestor of HEAD, so a fast-forward push of the new head to `codex/portable-byte-libraries-20261028` works. Rewrite the PR body: it still says "Validation on clean dea59917c" and lists the EmitBody guard as a gate. Confirm CI, especially the Linux SDK job.
7. **Land only after** the owner's or coordinator's landing audit. Then pick up the deferred five, in this order:
   - Resolve the error convention (section 7).
   - Resolve nominal `char`: the char-parity patch is unmerged.
   - Apply the markers.
   - Validate the same native and Wasm matrix.
   - Add `char`-literal, package-identity and invalid dynamic-conversion cases.

## 5. Holds, freezes and coordination rules (do not violate silently)

Frozen and historical:
- `c9cfe668e` (tree `0ebc105f`) and `3200134a8` (tree `bf72e3ee`) are frozen. `dea59917c` (tree `e5cef072`) is the last proven head. Never restamp old receipts as proof for a new head. Any source change invalidates matching build and execution proof for the affected checks.
- Do not rebase or force-push `codex/portable-byte-libraries-20261028`. Preserve history. The post-803 branch exists so the prior checkpoint and its receipts stay intact.

Admitted combination:
- Compiler `.chibip` is 20,792,064 bytes, sha256 `27d8f3d0568e442e1a70b167c04ba97c671ea0d0b1e85e3a7493d88fc59bb912`. Its 27 recorded source hashes must match the manifest.
- Runtime pin E1: tag `compiler-runtime-prepared26-43eec64f98e6fb288935dacfcbf86296dbc43b02`. Prepared format 26. Private build protocol 14 (since #803).
- If all 27 inputs match, reuse the accepted compiler pair. Do not run another compiler fixed-point.
- Accepted E1 and private-six binaries are bootstrap-only. Final proof needs hosts built from the final #795 source (public text-seven, private text-six).
- Do not edit `tools/compiler/bootstrap/*` or `tools/compiler/runtime.json`. No artifact pin, regeneration, publication or push is authorized without root.
- Do not use an old protocol-13 harness.

Holds still active at the log's end:
- #801 ABI/effects and the SDK publication hold. The native-byte callback test is applied as source only; it runs against the final matching host. The coordinator said: "no public effects hold is waived".
- #791 math publication hold. #694 sequencing is cleared (it merged). Prepared27 and records holds sit with other threads.
- Do not bring held code (#801, #791, #810, #808, char patch) into #795 by merge, cherry-pick or dependency. Do not copy another thread's unaccepted artifacts.

Guards and limits:
- Do not raise the 256 Mi layout-work guard, the 65,536-instruction function guard, the 50-second recovery timeout, or the 120-second `CompilerTools` limit. Do not add warning suppressions, remove tests, bypass the asset host or fallbacks, or shrink the SDK component set. Preserve the `-Wmissing-field-initializers` and `-Wstring-concatenation` Wasm flags.
- Keep tests that use explicit `return;`. The harness mis-handles implicit void fallthrough. The minimal red case is in `.chibi/portable-byte-libraries/implicit-void-probe`; it is a separate compiler follow-up.

Shared-file rules:
- Add your own entries to shared files (`Targets.chibi`, `test/language/main.c`, `test_policy.py`). Do not claim `package_core*` wholesale; the strings thread owns `package_core.c`.
- `compiler_fixture.c/.h` belong to the strings thread; consume the landed wrapper (`compiler_fixture_tests_in_collection`).
- `package_tests_find_collection` landed in #789. Discovery stays project-only by default.
- The leading `int compiler_backend` parameter of `package_compiler_closure` must stay.
- The recoverable-errors thread owns the compiler result-bank shape (module, bytes, optional diagnostic).

Landing rules:
- The coordinator held a single engine landing slot, sequenced as #788, #794, then #795, with other work interleaved. Check with the owner before landing anything.
- The `chibi-merge-own-prs` memory (merge once green) does not override any hold above. Green CI alone is not clearance for #795.
- CLAUDE.md: independently review affected contracts, run selected checks on the exact clean candidate, land that tested combination atomically (CUT62).

## 6. How to validate

Local setup (from Claude memory): `/usr/bin/python3` is 3.9, so put a shim dir with `python3 -> /opt/homebrew/bin/python3.12` first on PATH. Wasm needs `EMSDK_PYTHON=/opt/homebrew/bin/python3.12`. Use `/opt/homebrew/opt/llvm@22/bin/clang-format`; the default clang-format is 23, and the log shows one wrong-formatter slip.

Commands the thread used (all through `make`, with `JOBS=3`):
- `make build-release RELEASE_BUILD_DIR=.chibi/b795 BUILD_HOST=<assigned protocol-14 chibi-build-host> BUILD_TARGET='chibi chibi-compile chibi_ctl chibi-compiler-host test_tools test_language'`
- `python3 tools/test_policy.py tool <selector>` for each of: `language-package-portable`, `language-package-portable-compiler`, `language-asset-compiler-host`, `language-package-core`, `language-package-tests`, `language-package-imports`, `language-package-snapshot-chibi`, `language-record-prepared`, `language-execution`, `language-native-calls`, `language-compiler-fixture`, `language-asset-packaging-chibi`, `chibi-cli`, `published-docs`, `native-aot-driver`, `build-process-files`.
- `python3 tools/test_policy.py tool build-sdk` (the `CompilerTools` 120-second gate; set `CHIBI_BUILD_TIMINGS` to capture stage timings).
- `python3 .chibi/portable-byte-libraries/verify-candidate.py --head <sha> --label <name> --build <dir> --kind native|wasm|sanitizer <selectors>`. It guards a clean head and tree and saves logs. It needs a separate record of how the binaries were built.
- Wasm: `make wasm-language-build WASM_BUILD_DIR=... JOBS=3`, then selectors `wasm-package-portable` and `wasm-package-portable-compiler`.
- Sanitizer: `make build BUILD_DIR=... BUILD_PROFILE=sanitizer BUILD_TARGET='test_language test_tools policy_worker chibi-build-host' ASSET_GENERATOR_HOSTS=<sdk dir>`. Run `build-process-files`, `language-package-portable`, `language-record-prepared` and `language-asset-compiler-host`. Record `ASAN_OPTIONS`, `UBSAN_OPTIONS` and argv.
- SDK: `make package-sdk SDK_BUILD_DIR=... SDK_REFLECT_BUILD_DIR=... SDK_PREFIX=... SDK_CMAKE_FLAGS='-DCMAKE_BUILD_TYPE=Release -DCHIBI_ENABLE_SANITIZERS=OFF'`. Then relocate with the original prefix hidden, and run the installed-language harness with `CHIBI_LANGUAGE_BUILD_DIR=<dir>`. The previous relocation script `cow-relocation-proof.py` hard-codes `dea`; adapt it into a new script.
- `make check-format`.
- Repo-wide groups from CLAUDE.md (`make test-native`, `test-materials`, `test-projects`, `test-language-all`, `test-tools`, and `make test-wasm` before landing C test changes): the Codex thread did NOT run them as aggregates, only the selectors above. Say so in the PR. Linux-only `native-build` (10 cases) was proven only in Linux CI on `dea59917c`; macOS skips it.

Not run on any post-803 head: every build, test, sanitizer, Wasm and SDK step. Not run anywhere: Windows, browser, renderer and Studio beyond CI.

Slow or flaky:
- ASan asset-host run: 22 min. Native sanitizer builds: several minutes.
- SDK `CompilerTools` times out at 120 seconds on Linux CI on `dea59917c`, on a prepared-writer-only branch (#802) and on fixture-only #804. It passed on post-#797 main. The cause is unproven.
- Clean-checkout recovery hits 50 seconds.
- The build-host argument limit is 65,536 bytes. Use short build paths like `.chibi/b795`.
- If a build dir fails with an ABI or protocol rejection, use a fresh directory. #796 and #803 changed the admission rules.

Scratch left behind (about 2.8 GB in the primary worktree; nothing running, I checked `ps`):
- `.chibi/{a795 (ASan, 267M), b795 (release build and host, 290M), n795, s795b/s795i/s795r (SDK), w795 (Wasm), x795}`.
- `.chibi/portable-byte-libraries/` (1.1 GB): `cow-release` 293M, `sdk-build` 214M, `sanitizer` 211M, `sanitizer-cmake`, `cow-release-cmake`, `wasm-language`, `sdk-installed`, `sdk-relocated`.
- `build-release` (501M) and `build-release-cmake` (49M).
- In `portable-core-library`: `build-core-cmake/` (144M, untracked) and `.chibi/` (81M; keep the patches and manifest).
- Receipts and patches (`*.json`, `*.patch`, `*.log`, `*.py` in `.chibi/portable-byte-libraries/`) are evidence. Keep them until the PR is final. Delete the binary and build directories afterward (repo rule: report any scratch left).

## 7. Open questions, risks, and past failures

Risks:
1. **The error marker may have changed.** The log plans 25 `@fallible` function markers (`fallible-markers.patch`). Draft PR #813 (`codex/recoverable-errors-forward-20261028`, head `e902fcf3`) says "Mark a package-owned struct or union with `@error`" and "The old function marker is rejected." The log contains no mention of `@error`. If #813 lands, the deferred five should put `@error` on their five `pub Error :: struct` types instead (Utf8.chibi:17, Endian.chibi:8, Hex.chibi:14, Base64.chibi:16, Paths.chibi:15). Ask Charles or confirm on main before applying anything.
2. **The `char` prerequisite is still open.** Stage-one nominal `char` parity exists only as an uncommitted patch. #794 landed only the native-call char guard. Its fixture fails the default asset build on layout work (256 Mi guard). `utf8` needs `Decode(...) char` and `Encode(char)` unchanged, with no int alias.
3. **Layout-work guard.** Char, generics and results candidates each hit "prepared authored layout work limit exceeded" in package_compiler dependency fingerprinting, near the 256 Mi budget. The final compiler combination must prove a normal asset build under its own profile; do not use #694's 512 Mi profile as proof.
4. **SDK timeout may recur.** The 120-second `CompilerTools` failure on #795 is shared baseline pressure, not a proven library defect. Capture timings; do not blind-rerun or raise limits.
5. **Records could invalidate evidence.** The compositional-records thread (Prepared27) would change the ABI. Rebuild everything and do not carry receipts across.
6. **Stale docs.** `docs/package-byte-libraries.md` and the PR body say validation was on `dea59917c`. The 21ba evidence is labelled historical. Update both.
7. **Unproven evidence.** The reviewer sign-offs in the log came from Codex subagents; you need fresh independent review. The 2.8 GB scratch is evidence for `dea59917c` only.

What went wrong in the log and how it was handled:
- A Wasm compiler test read bootstrap files through host paths. Fixed by invoking the shared fixture directly.
- UTF-8 validation made many temporary result records (7.7 MB of copies for 3 scans of 16 KiB). The records were removed, giving 196 bytes.
- An `&&`-skipped error acknowledgment appeared at 10 test sites. Fixed by making the checks explicit.
- CI docs and native-build failures. Both were reproduced and fixed with separate regression evidence (see section 4).
- The thread stopped once on a model-capacity error (08:39 MDT on 10-28) and was resumed.
- It spent time looking for #801's owner. It belongs to the coordinator's `stdlib_parallel_audit` subagent, not this thread. Stop if you hit the same question.

## 8. First 30 minutes

1. Read this file, then `docs/package-byte-libraries.md` in the primary worktree, and `AGENTS.md`/`CLAUDE.md` (identical). Do not use `/Users/cwaldner/Personal/chibi/chibi`; it is dirty on an unrelated branch. Work in the primary worktree after reading its status.
2. Run the read-only checks:
   - `git -C <primary> status --short --branch`; `git -C <primary> rev-parse HEAD HEAD^{tree}`.
   - `git -C <primary> fetch origin` is a ref update only; do it only if the owner agrees. Then `git rev-list --count HEAD..origin/main`.
   - `gh pr view 795 --repo waldnercharles/chibi --json state,isDraft,headRefOid,statusCheckRollup`.
   - `gh pr view 801`, `813` and `803`.
3. Back up the deferred five (section 4, item 2) before anything else.
4. Confirm with Charles: who now grants heavy-producer time and landing; whether #801 is admitted yet; whether `@error` (#813) replaces `@fallible`; whether #795 should be re-baselined now or wait for #801.
5. Check `ps` for builds writing under `.chibi/*795*`. Then read `.chibi/portable-byte-libraries/post803-matching-source-validation-plan.json` and `post803-source-prep-handoff.json`.
6. If cleared to build: new frozen head, fresh build directories with short paths, protocol-14 host, selectors from section 6, and a receipt per run (head, tree, argv, binary hashes, timings).
7. Memory: Hindsight initiative page `kp-b91cbc727b204bbe8246d0aa580b4735` ("Portable Chibi core library first batch") and three ingested documents hold this thread's decisions; verify them against source before relying on them.
8. While waiting, only source-only work is safe: review the 43-file diff against current main, refresh the docs and the PR body, and prepare the `@error` versus `@fallible` patch for the deferred five. Do not start producers, regenerate artifacts or push without clearance.
